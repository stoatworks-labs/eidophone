#include "Shaders.h"

namespace eido::shaders
{

const char* const kVersion = "#version 410 core\n";

const char* const kQuadVertex = R"(
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
)";

//---------------------------------------------------------------------------
// Shared by every pass.
//---------------------------------------------------------------------------
const char* const kCommon = R"(
const float kPi = 3.14159265358979;
const int kFieldSize = 256;
const int kStateSize = 512;
const int kMaxModes  = 66;

const int kHookHalfGate     = 16;
const int kHookDriftUp      = 32;
const int kHookLaunchA      = 64;
const int kHookPowderDown   = 128;
const int kHookAlphaThrough = 256;
const int kHookNoClamp      = 512;

uniform int Hooks;

bool hooked( int bit )
{
	return ( Hooks & bit ) != 0;
}

//The PCG output mix: integer, so every GPU draws the same numbers.
uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

//24 bits of a hash as a uniform number in ( 0, 1 ): never 0, so log() is safe.
float unit01( uint h )
{
	return ( float( h >> 8u ) + 0.5 ) * ( 1.0 / 16777216.0 );
}
)";

//---------------------------------------------------------------------------
// The model's acceleration: the bilinear interpolant of the field lattice,
// clamped at its edge. tools: edtest's fieldAt() is this in double.
//---------------------------------------------------------------------------
const char* const kField = R"(
uniform sampler2D Field;

//A( u ) and its derivatives along x and y, per plate unit.
void fieldAt( vec2 u, out vec2 A, out vec2 Ax, out vec2 Ay )
{
	vec2 t  = ( u + 1.0 ) * ( 0.5 * float( kFieldSize ) ) - 0.5;
	vec2 i0 = floor( t );
	vec2 f  = t - i0;
	ivec2 lo = clamp( ivec2( i0 ), ivec2( 0 ), ivec2( kFieldSize - 1 ) );
	ivec2 hi = clamp( ivec2( i0 ) + 1, ivec2( 0 ), ivec2( kFieldSize - 1 ) );
	vec2 A00 = texelFetch( Field, ivec2( lo.x, lo.y ), 0 ).xy;
	vec2 A10 = texelFetch( Field, ivec2( hi.x, lo.y ), 0 ).xy;
	vec2 A01 = texelFetch( Field, ivec2( lo.x, hi.y ), 0 ).xy;
	vec2 A11 = texelFetch( Field, ivec2( hi.x, hi.y ), 0 ).xy;
	vec2 bottom = mix( A00, A10, f.x );
	vec2 top    = mix( A01, A11, f.x );
	A = mix( bottom, top, f.y );
	float perUnit = 0.5 * float( kFieldSize );
	Ax = mix( A10 - A00, A11 - A01, f.y ) * perUnit;
	Ay = ( top - bottom ) * perUnit;
}
)";

//---------------------------------------------------------------------------
// 1. The field: sum_k a_k W_k on the lattice, in a fixed order.
//---------------------------------------------------------------------------
const char* const kFieldFragment = R"(
uniform sampler2DArray Modes;
uniform int ModeCount;
uniform vec2 Coef[ kMaxModes ];

out vec4 fragColour;

void main()
{
	ivec2 p = ivec2( gl_FragCoord.xy );
	vec2 sum = vec2( 0.0 );
	for( int k = 0; k < ModeCount; ++k )
		sum += Coef[ k ] * texelFetch( Modes, ivec3( p, k ), 0 ).r;
	fragColour = vec4( sum, 0.0, 1.0 );
}
)";

//---------------------------------------------------------------------------
// 2. Pour: uniform over the plate, a hair inside the rim.
//---------------------------------------------------------------------------
const char* const kPourFragment = R"(
uniform int Shape;    //0 square, 1 circle
uniform int GrainKind;//0 sand, 1 powder, 2 both (alternate grains)
uniform uint Seed;

out vec4 fragColour;

void main()
{
	ivec2 p    = ivec2( gl_FragCoord.xy );
	uint index = uint( p.y * kStateSize + p.x );
	//Salted apart from the substeps' stream, pcg( index ^ pcg( Step ) ): with
	//Seed == Step the first hop's noise WAS the poured position (edtest --hop
	//caught a -0.15 sigma pull down the gradient at Step 1).
	uint h1    = pcg( index ^ pcg( Seed ^ 0xa511e9b3u ) );
	uint h2    = pcg( h1 ^ 0x9e3779b9u );
	float u1 = unit01( h1 ), u2 = unit01( h2 );
	const float inset = 1.0 - 1.0 / 256.0;
	vec2 x;
	if( Shape == 1 )
	{
		float r  = sqrt( u1 ) * inset;
		float th = 2.0 * kPi * u2;
		x = r * vec2( cos( th ), sin( th ) );
	}
	else
		x = ( 2.0 * vec2( u1, u2 ) - 1.0 ) * inset;
	float kind = GrainKind == 1 ? 1.0 : ( GrainKind == 2 ? float( index & 1u ) : 0.0 );
	fragColour = vec4( x, 0.0, kind );
}
)";

//---------------------------------------------------------------------------
// 3. One substep. State ( x, y, a/g when last thrown or carried, kind ).
//---------------------------------------------------------------------------
const char* const kUpdateFragment = R"(
uniform sampler2D State;
uniform float Gravity; //m/s^2
uniform float OmegaD;  //rad/s
uniform float HalfSize;//m: plate units to metres
uniform float Hop;     //H
uniform float Air;     //P
uniform float Dt;      //s
uniform uint Step;     //the substep's number: the random numbers' seed
uniform int Shape;
uniform int Count;     //grains in use

out vec4 fragColour;

void main()
{
	ivec2 p   = ivec2( gl_FragCoord.xy );
	vec4 s    = texelFetch( State, p, 0 );
	int index = p.y * kStateSize + p.x;
	if( index >= Count )
	{
		fragColour = s;
		return;
	}
	vec2 A, Ax, Ay;
	fieldAt( s.xy, A, Ax, Ay );
	float a2     = dot( A, A );
	float gate   = hooked( kHookHalfGate ) ? 0.5 * Gravity : Gravity;
	bool powder  = s.w > 0.5;
	bool thrown  = a2 > gate * gate;
	//Newton: a grain leaves the plate only if the plate's downward
	//acceleration beats g. Sand that is not thrown does not move AT ALL.
	if( !powder && !thrown )
	{
		fragColour = s;
		return;
	}
	float a   = sqrt( a2 );
	vec2 grad = a > 0.0 ? vec2( dot( A, Ax ), dot( A, Ay ) ) / a : vec2( 0.0 );//per plate unit
	float w   = max( OmegaD, 1.0 );
	vec2 stepM = vec2( 0.0 );//metres
	if( thrown )
	{
		//Launch speed at separation, then a ballistic random walk: hops of
		//H v^2/g every 2v/g, a share of each down the gradient of a.
		float v = hooked( kHookLaunchA ) ? a / w : sqrt( max( a2 - gate * gate, 0.0 ) ) / w;
		float D = Hop * Hop * v * v * v / ( 8.0 * Gravity );
		float g = length( grad );
		vec2 n  = g > 0.0 ? grad / g : vec2( 0.0 );
		if( !powder )
			stepM += ( hooked( kHookDriftUp ) ? 1.0 : -1.0 ) * ( 0.5 * Hop * v * Dt ) * n;
		uint h1  = pcg( uint( index ) ^ pcg( Step ) );
		uint h2  = pcg( h1 ^ 0x9e3779b9u );
		float r  = sqrt( -2.0 * log( unit01( h1 ) ) );
		float th = 2.0 * kPi * unit01( h2 );
		stepM += sqrt( 2.0 * D * Dt ) * r * vec2( cos( th ), sin( th ) );
	}
	if( powder )
	{
		//Faraday's currents: up the gradient of the velocity amplitude,
		//u = P a grad( a ) / w^3, everywhere the plate moves.
		vec2 u = Air * a * ( grad / HalfSize ) / ( w * w * w );
		stepM += ( hooked( kHookPowderDown ) ? -1.0 : 1.0 ) * u * Dt;
	}
	vec2 d    = stepM / HalfSize;
	float len = length( d );
	const float maxStep = 1.0 / float( kFieldSize );//half a lattice texel
	if( len > maxStep && !hooked( kHookNoClamp ) )
		d *= maxStep / len;
	vec2 x = s.xy + d;
	//The rim reflects.
	if( Shape == 1 )
	{
		float r = length( x );
		if( r > 1.0 )
			x *= ( 2.0 - r ) / r;
	}
	else
	{
		if( x.x > 1.0 ) x.x = 2.0 - x.x;
		if( x.x < -1.0 ) x.x = -2.0 - x.x;
		if( x.y > 1.0 ) x.y = 2.0 - x.y;
		if( x.y < -1.0 ) x.y = -2.0 - x.y;
	}
	fragColour = vec4( x, a / Gravity, s.w );
}
)";

//---------------------------------------------------------------------------
// 4. The splat: one point per grain, no vertex buffer.
//---------------------------------------------------------------------------
const char* const kSplatVertex = R"(
uniform sampler2D State;
uniform vec2 Scale;     //plate units to clip space
uniform float PointSize;//px
uniform float Weight;   //a grain's area in pixels per point pixel

out vec2 kind;

void main()
{
	ivec2 p      = ivec2( gl_VertexID % kStateSize, gl_VertexID / kStateSize );
	vec4 s       = texelFetch( State, p, 0 );
	gl_Position  = vec4( s.xy * Scale, 0.0, 1.0 );
	gl_PointSize = PointSize;
	kind         = s.w > 0.5 ? vec2( 0.0, Weight ) : vec2( Weight, 0.0 );
}
)";

const char* const kSplatFragment = R"(
uniform float PointSize;

in vec2 kind;
out vec4 fragColour;

void main()
{
	float cover = 1.0;
	if( PointSize >= 2.0 )
	{
		float r = length( gl_PointCoord - vec2( 0.5 ) ) * PointSize;
		cover   = clamp( 0.5 * PointSize - r + 0.5, 0.0, 1.0 );
	}
	fragColour = vec4( kind * cover, 0.0, 0.0 );
}
)";

//---------------------------------------------------------------------------
// 5. The host's frame.
//---------------------------------------------------------------------------
const char* const kFinalFragment = R"(
uniform sampler2D Density;
uniform sampler2D InputTexture;
uniform ivec4 Viewport;   //x, y, width, height
uniform float PlateScale; //px per plate unit
uniform int Shape;
uniform vec3 PlateColour; //display code (chosen swatches)
uniform float DriveAt;
uniform int IsOver;
uniform float MixAmount;
uniform vec4 Cue;         //rgb, strength
uniform float CueWidth;   //px

out vec4 fragColour;

const vec3 kSand   = vec3( 0.93, 0.89, 0.80 );//pale quartz
const vec3 kPowder = vec3( 0.96, 0.80, 0.22 );//lycopodium

void main()
{
	ivec2 pix = ivec2( gl_FragCoord.xy ) - Viewport.xy;
	vec4 src  = IsOver == 1 ? texelFetch( InputTexture, pix, 0 ) : vec4( 0.0, 0.0, 0.0, 1.0 );
	if( IsOver == 1 && MixAmount <= 0.0 )
	{
		fragColour = src;//Mix 0: the clip, bit for bit
		return;
	}
	vec2 size  = vec2( Viewport.zw );
	vec2 q     = vec2( pix ) + 0.5 - 0.5 * size;//px from the centre
	vec2 u     = q / PlateScale;
	float edge = Shape == 1 ? length( u ) : max( abs( u.x ), abs( u.y ) );
	float inside = ( 1.0 - edge ) * PlateScale;//px inside the rim
	float mask   = clamp( inside + 0.5, 0.0, 1.0 );

	vec2 dens    = texelFetch( Density, pix, 0 ).xy;
	ivec2 behind = clamp( pix + ivec2( -1, 1 ), ivec2( 0 ), Viewport.zw - 1 );
	vec2 shade   = texelFetch( Density, behind, 0 ).xy;
	float covS   = ( 1.0 - exp( -dens.x ) ) * mask;
	float covP   = ( 1.0 - exp( -dens.y ) ) * mask;
	float shadow = 1.0 - exp( -( shade.x + 0.5 * shade.y ) );

	//The plate's surface, premultiplied: the material lit from above, or the clip.
	vec3 surface;
	float surfaceA;
	if( IsOver == 1 )
	{
		surface  = src.rgb;
		surfaceA = mask * src.a;
	}
	else
	{
		vec2 hot    = u - vec2( -0.35, 0.40 );
		float light = 1.0 - 0.22 * dot( u, u ) + 0.08 * exp( -8.0 * dot( hot, hot ) );
		surface     = PlateColour * light;
		float stem  = length( u - vec2( DriveAt, 0.0 ) ) * PlateScale - 0.035 * PlateScale;
		surface *= mix( 0.3, 1.0, clamp( stem + 0.5, 0.0, 1.0 ) );
		surfaceA = mask;
	}
	vec3 C  = surface * ( 1.0 - 0.45 * shadow ) * surfaceA;
	float A = surfaceA;
	C = C * ( 1.0 - covS ) + kSand * covS;
	A = A * ( 1.0 - covS ) + covS;
	C = C * ( 1.0 - covP ) + kPowder * covP;
	A = A * ( 1.0 - covP ) + covP;

	//The capture-state cue: a band along whichever comes first, the rim or
	//the frame's edge, so an Over that fills the frame still shows it.
	vec2 toFrame = 0.5 * size - abs( q );
	float closest = min( inside, min( toFrame.x, toFrame.y ) );
	float band   = Cue.a * clamp( CueWidth - closest, 0.0, 1.0 ) * mask;
	C = C * ( 1.0 - band ) + Cue.rgb * band;
	A = A * ( 1.0 - band ) + band;

	if( IsOver == 0 )
	{
		fragColour = vec4( C, 1.0 );//over black, opaque
		return;
	}
	vec3 rgb    = A > 0.0 ? C / A : vec3( 0.0 );
	float alpha = hooked( kHookAlphaThrough ) ? src.a : A;
	fragColour  = mix( src, vec4( rgb, alpha ), MixAmount );
}
)";

std::string Assemble( Pass pass )
{
	const std::string text = std::string( kVersion ) + kCommon;
	switch( pass )
	{
	case Pass::Field: return text + kFieldFragment;
	case Pass::Pour: return text + kPourFragment;
	case Pass::Update: return text + kField + kUpdateFragment;
	case Pass::Splat: return text + kSplatFragment;
	case Pass::Final: return text + kFinalFragment;
	}
	return text;
}

std::string SplatVertex()
{
	return std::string( kVersion ) + kCommon + kSplatVertex;
}

} // namespace eido::shaders
