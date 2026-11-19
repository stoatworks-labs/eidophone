#include "Eidophone.h"

#include "Diag.h"
#include "GLState.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <utility>

using namespace ffglex;

namespace eido
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Host seconds a single frame may advance the plate by. The integrator is
/// exact for any step; this only stops a stalled host from ringing the plate
/// down in one frame.
constexpr double kMaxFrameDelta = 0.25;
/// Legendre degrees per direction for the square (SPEC-eidophone.md: the
/// lowest 66 converged to ~1e-7 at 36).
constexpr int kSquareDegree = 36;
constexpr int kElasticModes = shaders::kMaxModes - 2;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

/// Every pass here writes every texel it draws, but Apple's GL runs on Metal,
/// where binding a framebuffer LOADS the attachment unless it is cleared
/// first. A clear becomes the pass's load action. (Not for the grain state,
/// whose undrawn rows must survive: see drawUpdate.)
void discardTarget()
{
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );
static_assert( kMaxGrains == shaders::kStateSize * shaders::kStateSize, "Amount's ceiling is the state texture" );

//---------------------------------------------------------------------------
std::shared_ptr< const modes::Table > EidophonePlugin::TableFor( Shape shape, double nu )
{
	static std::mutex lock;
	static std::map< std::pair< int, double >, std::shared_ptr< const modes::Table > > cache;
	std::lock_guard< std::mutex > guard( lock );
	const auto key = std::make_pair( static_cast< int >( shape ), nu );
	auto found     = cache.find( key );
	if( found != cache.end() )
		return found->second;
	const auto start = wallSeconds();
	auto built       = std::make_shared< const modes::Table >( shape == Shape::Circle ? modes::BuildCircle( nu, kElasticModes )
	                                                                                  : modes::BuildSquare( nu, kSquareDegree, kElasticModes ) );
	diag::info( std::string( "mode table: " ) + ( shape == Shape::Circle ? "circle" : "square" ) + " nu=" + std::to_string( nu ) + ", "
	            + std::to_string( built->modes.size() ) + " modes in " + std::to_string( static_cast< int >( 1000.0 * ( wallSeconds() - start ) ) ) + " ms" );
	cache[ key ] = built;
	return built;
}

//---------------------------------------------------------------------------
EidophonePlugin::EidophonePlugin( bool effect ) :
	isEffect( effect ),
	hostOrder( HostOrder( effect ) )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );

	//The plate runs on the host's clock: its modes ring in seconds.
	SetTimeSupported( true );

	std::fill( std::begin( idToHost ), std::end( idToHost ), -1 );
	for( size_t i = 0; i < hostOrder.size(); ++i )
		idToHost[ hostOrder[ i ] ] = static_cast< int >( i );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;
	//The Over prints the clip on a plate that fills the frame.
	if( isEffect )
		params[ PT_VIEW ] = static_cast< float >( View::Fill );

	for( unsigned int host = 0; host < hostOrder.size(); ++host )
	{
		const unsigned int id = hostOrder[ host ];
		if( id >= PT_ABOUT_TEXT )
			break;
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( host, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( host, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Device:
		{
			//The elements carry device keys, not positions (a device is
			//remembered by name). The count is fixed here; Rescan refills them.
			SetOptionParamInfo( host, info.name, static_cast< unsigned int >( kDeviceElements ), 0.0f );
			const auto& slots = audio.Slots();
			for( int i = 0; i < kDeviceElements && i < static_cast< int >( slots.size() ); ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), slots[ static_cast< size_t >( i ) ].label.c_str(),
				                     slots[ static_cast< size_t >( i ) ].value );
			break;
		}
		case Kind::Event: SetParamInfo( host, info.name, FF_TYPE_EVENT, false ); break;
		case Kind::Buffer:
			//Resolume's FFT: an audio-source picker that it writes 64 bins into.
			SetBufferParamInfo( host, info.name, 64, FF_USAGE_FFT );
			for( int i = 0; i < 64; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), "", 0.0f );
			break;
		}
		SetParamGroup( host, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	const unsigned int aboutHost = static_cast< unsigned int >( idToHost[ PT_ABOUT_TEXT ] );
	SetParamInfo( aboutHost, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = aboutHost + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = aboutHost; i < hostOrder.size(); ++i )
		SetParamGroup( i, "About" );
}

void EidophonePlugin::refreshDeviceMenu()
{
	audio.Refresh();
	std::vector< std::string > names;
	std::vector< float > values;
	for( const DeviceSlot& slot : audio.Slots() )
	{
		names.push_back( slot.label );
		values.push_back( slot.value );
	}
	names.resize( kDeviceElements, "(none)" );
	values.resize( kDeviceElements, -99.0f );
	SetParamElements( static_cast< unsigned int >( idToHost[ PT_DEVICE ] ), names, values, true );
}

//---------------------------------------------------------------------------
GLint EidophonePlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}
void EidophonePlugin::uniform( const FFGLShader& shader, const char* name, int v )
{
	glUniform1i( where( shader, name ), v );
}
void EidophonePlugin::uniform( const FFGLShader& shader, const char* name, unsigned int v )
{
	glUniform1ui( where( shader, name ), v );
}
void EidophonePlugin::uniform( const FFGLShader& shader, const char* name, float v )
{
	glUniform1f( where( shader, name ), v );
}
void EidophonePlugin::uniform( const FFGLShader& shader, const char* name, float a, float b )
{
	glUniform2f( where( shader, name ), a, b );
}

//---------------------------------------------------------------------------
FFResult EidophonePlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) + ( isEffect ? " (Over)" : " (source)" ) );

	const std::string quadVertex  = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	const std::string splatVertex = shaders::SplatVertex();
	struct Stage
	{
		FFGLShader* shader;
		const std::string* vertex;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &fieldShader, &quadVertex, shaders::Pass::Field, "field" },
		{ &pourShader, &quadVertex, shaders::Pass::Pour, "pour" },
		{ &updateShader, &quadVertex, shaders::Pass::Update, "update" },
		{ &splatShader, &splatVertex, shaders::Pass::Splat, "splat" },
		{ &finalShader, &quadVertex, shaders::Pass::Final, "final" },
	};
	for( const Stage& stage : stages )
	{
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( stage.vertex->c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Eidophone: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}
	glGenVertexArrays( 1, &emptyVAO );
	glGenTextures( 1, &modeTexture );
	{
		GLint previous = 0;
		glGetIntegerv( GL_TEXTURE_BINDING_2D_ARRAY, &previous );
		glBindTexture( GL_TEXTURE_2D_ARRAY, modeTexture );
		glTexImage3D( GL_TEXTURE_2D_ARRAY, 0, GL_R32F, shaders::kFieldSize, shaders::kFieldSize, shaders::kMaxModes, 0, GL_RED, GL_FLOAT, nullptr );
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glBindTexture( GL_TEXTURE_2D_ARRAY, static_cast< GLuint >( previous ) );
	}
	if( emptyVAO == 0 || modeTexture == 0 )
	{
		diag::error( "could not allocate the mode texture" );
		DeInitGL();
		return FF_FAIL;
	}
	uploaded = nullptr;
	needPour = true;
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
void EidophonePlugin::ensureTable()
{
	const Shape shape       = static_cast< Shape >( OptionIndex( params[ PT_SHAPE ], static_cast< int >( Shape::Count ) ) );
	const Material material = static_cast< Material >( OptionIndex( params[ PT_MATERIAL ], static_cast< int >( Material::Count ) ) );
	if( table && shape == tableShape && material == tableMaterial )
		return;
	const double nu = MaterialOf( material ).nu;
	auto next       = TableFor( shape, nu );
	//A material of the same nu keeps the table (and the plate keeps ringing).
	if( next != table )
	{
		table = std::move( next );
		plate.SetTable( table );
	}
	tableShape    = shape;
	tableMaterial = material;
}

void EidophonePlugin::uploadModes()
{
	if( uploaded == table.get() || !table )
		return;
	std::vector< float > lattice( static_cast< size_t >( shaders::kFieldSize ) * shaders::kFieldSize );
	GLint previous = 0;
	glGetIntegerv( GL_TEXTURE_BINDING_2D_ARRAY, &previous );
	glBindTexture( GL_TEXTURE_2D_ARRAY, modeTexture );
	const int count = std::min( static_cast< int >( table->modes.size() ), shaders::kMaxModes );
	for( int k = 0; k < count; ++k )
	{
		table->Sample( table->modes[ static_cast< size_t >( k ) ], shaders::kFieldSize, lattice.data() );
		glTexSubImage3D( GL_TEXTURE_2D_ARRAY, 0, 0, 0, k, shaders::kFieldSize, shaders::kFieldSize, 1, GL_RED, GL_FLOAT, lattice.data() );
	}
	glBindTexture( GL_TEXTURE_2D_ARRAY, static_cast< GLuint >( previous ) );
	uploaded = table.get();
}

void EidophonePlugin::drawField()
{
	ScopedFBOBinding target( field.GetGLID(), ScopedFBOBinding::RB_REVERT );
	glViewport( 0, 0, shaders::kFieldSize, shaders::kFieldSize );
	discardTarget();
	glUseProgram( fieldShader.GetGLID() );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D_ARRAY, modeTexture );
	uniform( fieldShader, "Modes", 0 );
	const int count = static_cast< int >( frame.coef.size() );
	uniform( fieldShader, "ModeCount", count );
	std::vector< float > coef( 2 * shaders::kMaxModes, 0.0f );
	for( int k = 0; k < count; ++k )
	{
		coef[ static_cast< size_t >( 2 * k ) ]     = frame.coef[ static_cast< size_t >( k ) ].real();
		coef[ static_cast< size_t >( 2 * k + 1 ) ] = frame.coef[ static_cast< size_t >( k ) ].imag();
	}
	glUniform2fv( where( fieldShader, "Coef" ), shaders::kMaxModes, coef.data() );
	uniform( fieldShader, "Hooks", hooks );
	quad.Draw();
	glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
	glUseProgram( 0 );
}

void EidophonePlugin::drawPour( Shape shape, Grain grain )
{
	++pourSeed;
	for( PassBuffer& buffer : state )
	{
		ScopedFBOBinding target( buffer.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, shaders::kStateSize, shaders::kStateSize );
		discardTarget();
		glUseProgram( pourShader.GetGLID() );
		uniform( pourShader, "Shape", static_cast< int >( shape ) );
		uniform( pourShader, "GrainKind", static_cast< int >( grain ) );
		uniform( pourShader, "Seed", static_cast< unsigned int >( pourSeed ) );
		uniform( pourShader, "Hooks", hooks );
		quad.Draw();
		glUseProgram( 0 );
	}
	current = 0;
}

void EidophonePlugin::drawUpdate( int rows, int count )
{
	const int next = 1 - current;
	ScopedFBOBinding target( state[ next ].GetGLID(), ScopedFBOBinding::RB_REVERT );
	//No clear: the rows past Amount are not drawn, and they keep the grains
	//they had (a grain switched off by Amount comes back where it was).
	glViewport( 0, 0, shaders::kStateSize, rows );
	glUseProgram( updateShader.GetGLID() );
	bindUnit( 0, state[ current ].TextureID() );
	bindUnit( 1, field.TextureID() );
	uniform( updateShader, "State", 0 );
	uniform( updateShader, "Field", 1 );
	uniform( updateShader, "Gravity", static_cast< float >( kGravity ) );
	uniform( updateShader, "OmegaD", static_cast< float >( frame.omega ) );
	uniform( updateShader, "HalfSize", static_cast< float >( frame.halfSize ) );
	uniform( updateShader, "Hop", static_cast< float >( HopFromParam( params[ PT_HOP ] ) ) );
	uniform( updateShader, "Air", static_cast< float >( AirFromParam( params[ PT_AIR ] ) ) );
	uniform( updateShader, "Dt", static_cast< float >( shaders::kSubstep ) );
	uniform( updateShader, "Step", static_cast< unsigned int >( stepCounter ) );
	uniform( updateShader, "Shape", static_cast< int >( tableShape ) );
	uniform( updateShader, "Count", count );
	uniform( updateShader, "Hooks", hooks );
	quad.Draw();
	unbindTextureUnits( 2 );
	glUseProgram( 0 );
	current = next;
	++stepCounter;
}

void EidophonePlugin::drawSplat( int width, int height, int count )
{
	ScopedFBOBinding target( density.GetGLID(), ScopedFBOBinding::RB_REVERT );
	glViewport( 0, 0, width, height );
	discardTarget();
	glUseProgram( splatShader.GetGLID() );
	bindUnit( 0, state[ current ].TextureID() );
	uniform( splatShader, "State", 0 );
	const float sx = static_cast< float >( 2.0 * frame.plateScale / width );
	const float sy = static_cast< float >( 2.0 * frame.plateScale / height );
	uniform( splatShader, "Scale", sx, sy );
	//A grain under a pixel is a one-pixel disc at its share of the area, so it
	//still moves smoothly; Raster is whole and clears the disc's edge.
	const double size   = frame.pointSize;
	const double disc   = std::max( size, 1.0 );
	uniform( splatShader, "PointSize", static_cast< float >( disc ) );
	uniform( splatShader, "Raster", static_cast< float >( std::ceil( disc ) + 2.0 ) );
	uniform( splatShader, "Weight", static_cast< float >( ( size / disc ) * ( size / disc ) ) );
	uniform( splatShader, "Hooks", hooks );
	glEnable( GL_PROGRAM_POINT_SIZE );
	setAdditiveBlend();
	glBindVertexArray( emptyVAO );
	glDrawArrays( GL_POINTS, 0, count );
	glBindVertexArray( 0 );
	glDisable( GL_BLEND );
	glDisable( GL_PROGRAM_POINT_SIZE );
	unbindTextureUnits( 1 );
	glUseProgram( 0 );
}

void EidophonePlugin::drawFinal( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height )
{
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( finalShader.GetGLID() );
	bindUnit( 0, density.TextureID() );
	//Always a real texture on the clip's unit, even in the source: a sampler
	//on texture 0 is "unloadable" to Apple's GL (boreal's trap).
	bindUnit( 1, input != nullptr ? input->Handle : density.TextureID() );
	uniform( finalShader, "Density", 0 );
	uniform( finalShader, "InputTexture", 1 );
	glUniform4i( where( finalShader, "Viewport" ), hostViewport[ 0 ], hostViewport[ 1 ], width, height );
	uniform( finalShader, "PlateScale", static_cast< float >( frame.plateScale ) );
	uniform( finalShader, "Shape", static_cast< int >( tableShape ) );
	const MaterialData& m = MaterialOf( tableMaterial );
	glUniform3f( where( finalShader, "PlateColour" ), static_cast< float >( m.rgb[ 0 ] ), static_cast< float >( m.rgb[ 1 ] ),
	             static_cast< float >( m.rgb[ 2 ] ) );
	uniform( finalShader, "DriveAt", static_cast< float >( DriveAtFromParam( params[ PT_DRIVE_AT ] ) ) );
	uniform( finalShader, "IsOver", isEffect ? 1 : 0 );
	uniform( finalShader, "MixAmount", isEffect ? std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) : 1.0f );
	//The capture state, the one signal FFGL lets a plugin give: Silent (open
	//and dead, e.g. no microphone permission) pulses red; Missing and Failed
	//are amber. The host-FFT path and Running show nothing.
	float cue[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
	switch( audio.State() )
	{
	case earshot::CaptureState::Silent:
	{
		const double pulse = 0.5 + 0.5 * std::cos( kPi * now );//a 2 s period
		cue[ 0 ] = 1.0f, cue[ 1 ] = 0.12f, cue[ 2 ] = 0.08f;
		cue[ 3 ] = static_cast< float >( 0.35 + 0.65 * pulse );
		break;
	}
	case earshot::CaptureState::Missing:
	case earshot::CaptureState::Failed:
		cue[ 0 ] = 1.0f, cue[ 1 ] = 0.62f, cue[ 2 ] = 0.10f, cue[ 3 ] = 0.85f;
		break;
	default: break;
	}
	glUniform4fv( where( finalShader, "Cue" ), 1, cue );
	uniform( finalShader, "CueWidth", static_cast< float >( std::max( 2.0, 0.012 * height ) ) );
	uniform( finalShader, "Hooks", hooks );
	quad.Draw();
	unbindTextureUnits( 2 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
void EidophonePlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (rosette's, by way of millpond).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no dt across it
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

//---------------------------------------------------------------------------
FFResult EidophonePlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}

	//The host's state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = input != nullptr ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height = input != nullptr ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	//---------------------------------------------------------------------
	// Time.
	//---------------------------------------------------------------------
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	const double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow         = now;
	frame.dt        = dt;
	frame.now       = now;

	//---------------------------------------------------------------------
	// The plate: its modes, its configuration, the drive, the response.
	//---------------------------------------------------------------------
	ensureTable();
	const MaterialData& m  = MaterialOf( tableMaterial );
	const double size      = SizeFromParam( params[ PT_SIZE ] );
	const double thickness = ThicknessFromParam( params[ PT_THICKNESS ] );
	PlateConfig config;
	config.halfSize = 0.5 * size;
	config.speed    = PlateSpeed( m, thickness );
	config.mass     = m.rho * thickness * ( tableShape == Shape::Circle ? kPi * 0.25 * size * size : size * size );
	config.q        = DampingFromParam( params[ PT_DAMPING ] );
	config.driveAt  = DriveAtFromParam( params[ PT_DRIVE_AT ] );
	plate.Configure( config );

	float bins[ 64 ] = {};
	int binCount     = 0;
	if( const ParamInfo* info = FindParamInfo( static_cast< FFUInt32 >( idToHost[ PT_AUDIO ] ) ) )
	{
		binCount = static_cast< int >( std::min< size_t >( info->elements.size(), 64 ) );
		for( int i = 0; i < binCount; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}
	const BinLaw law = static_cast< BinLaw >( OptionIndex( params[ PT_BIN_LAW ], static_cast< int >( BinLaw::Count ) ) );
	//Resolume's bins are on the axis of its audio engine's rate (375 Hz a bin
	//at Arena's 48 kHz, measured), handed to earshot before they are read.
	//Until the host sends a rate, earshot's 48 kHz.
	if( !( hooks & kHookIgnoreHostRate ) )
		audio.SetHostRate( hostRate.load( std::memory_order_relaxed ) );
	frame.drive      = audio.Update( now, bins, binCount, ClarityFromParam( params[ PT_CLARITY ] ), law );
	//Gain is the steady peak acceleration the loudest recent level gives at
	//this frequency, so a note between modes shakes the plate as visibly as
	//one on a mode: the force is what it takes (Audio.h).
	frame.peakPerNewton = plate.PeakPerNewton( frame.drive.omega );
	frame.drive.F0      = frame.peakPerNewton > 0.0 ? GainFromParam( params[ PT_GAIN ] ) * kGravity * frame.drive.relative / frame.peakPerNewton : 0.0;
	plate.Advance( dt, frame.drive.F0, frame.drive.omega );

	const int modeCount = std::min( plate.Count(), shaders::kMaxModes );
	frame.coef.assign( static_cast< size_t >( modeCount ), std::complex< float >( 0.0f, 0.0f ) );
	if( testDrive.pure )
	{
		if( testDrive.mode >= 0 && testDrive.mode < modeCount )
			frame.coef[ static_cast< size_t >( testDrive.mode ) ] = std::complex< float >( static_cast< float >( testDrive.accel ), 0.0f );
		if( testDrive.mode2 >= 0 && testDrive.mode2 < modeCount )
			frame.coef[ static_cast< size_t >( testDrive.mode2 ) ] += std::complex< float >( static_cast< float >( testDrive.accel2 ), static_cast< float >( testDrive.accel2Imag ) );
		frame.omega = testDrive.omega;
	}
	else
	{
		for( int k = 0; k < modeCount; ++k )
		{
			const auto a = plate.Acceleration( k );
			frame.coef[ static_cast< size_t >( k ) ] = std::complex< float >( static_cast< float >( a.real() ), static_cast< float >( a.imag() ) );
		}
		frame.omega = frame.drive.omega;
	}
	frame.halfSize = config.halfSize;

	//---------------------------------------------------------------------
	// The view and the grains.
	//---------------------------------------------------------------------
	const View view = static_cast< View >( OptionIndex( params[ PT_VIEW ], static_cast< int >( View::Count ) ) );
	if( view == View::Fill )
		frame.plateScale = tableShape == Shape::Circle ? 0.5 * std::hypot( width, height ) : 0.5 * std::max( width, height );
	else
		frame.plateScale = 0.95 * 0.5 * std::min( width, height );
	frame.pointSize = GrainSizeFromParam( params[ PT_GRAIN_SIZE ] ) * height / 1080.0;
	frame.grains    = AmountFromParam( params[ PT_AMOUNT ] );

	const Grain grain = static_cast< Grain >( OptionIndex( params[ PT_GRAIN ], static_cast< int >( Grain::Count ) ) );
	if( static_cast< int >( grain ) != lastGrain || static_cast< int >( tableShape ) != lastShape )
		needPour = true;
	if( pourPending )
		needPour = true;

	int substeps = 0;
	if( forcedSubsteps >= 0 )
		substeps = forcedSubsteps;
	else
	{
		const double total = substepClock + dt;
		substeps           = static_cast< int >( std::floor( total / shaders::kSubstep + 1e-6 ) );
		substepClock       = std::max( 0.0, total - substeps * shaders::kSubstep );
		substeps           = std::min( substeps, shaders::kMaxSubsteps );//the excess is lost (below 15 fps)
	}

	//---------------------------------------------------------------------
	// The buffers, allocated before anything is bound.
	//---------------------------------------------------------------------
	if( !field.Ensure( shaders::kFieldSize, shaders::kFieldSize, GL_RG32F, PassBuffer::Sampling::Nearest )
	    || !state[ 0 ].Ensure( shaders::kStateSize, shaders::kStateSize, GL_RGBA32F, PassBuffer::Sampling::Nearest )
	    || !state[ 1 ].Ensure( shaders::kStateSize, shaders::kStateSize, GL_RGBA32F, PassBuffer::Sampling::Nearest )
	    || !density.Ensure( width, height, GL_RG16F, PassBuffer::Sampling::Nearest ) )
	{
		diag::error( "could not allocate the buffers" );
		return FF_FAIL;
	}

	uploadModes();
	drawField();
	frame.poured = false;
	if( needPour )
	{
		drawPour( tableShape, grain );
		needPour     = false;
		pourPending  = false;
		frame.poured = true;
		lastGrain    = static_cast< int >( grain );
		lastShape    = static_cast< int >( tableShape );
	}
	const int rows    = ( frame.grains + shaders::kStateSize - 1 ) / shaders::kStateSize;
	frame.substeps    = substeps;
	frame.firstStep   = stepCounter;
	for( int s = 0; s < substeps; ++s )
		drawUpdate( rows, frame.grains );
	drawSplat( width, height, frame.grains );
	drawFinal( input, hostViewport, pgl->HostFBO, width, height );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult EidophonePlugin::DeInitGL()
{
	uniformCache.clear();
	fieldShader.FreeGLResources();
	pourShader.FreeGLResources();
	updateShader.FreeGLResources();
	splatShader.FreeGLResources();
	finalShader.FreeGLResources();
	quad.Release();
	field.Destroy();
	state[ 0 ].Destroy();
	state[ 1 ].Destroy();
	density.Destroy();
	if( modeTexture != 0 )
	{
		glDeleteTextures( 1, &modeTexture );
		modeTexture = 0;
	}
	if( emptyVAO != 0 )
	{
		glDeleteVertexArrays( 1, &emptyVAO );
		emptyVAO = 0;
	}
	uploaded = nullptr;
	needPour = true;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult EidophonePlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void EidophonePlugin::SetSampleRate( unsigned int rate )
{
	//The SDK's own copy (CFFGLPlugin::sampleRate) starts uninitialised, so the
	//plugin keeps its own.
	CFFGLPlugin::SetSampleRate( rate );
	hostRate.store( rate, std::memory_order_relaxed );
}

char* EidophonePlugin::GetTextParameter( unsigned int index )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult EidophonePlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

void EidophonePlugin::SetById( unsigned int id, float value )
{
	if( id >= PT_COUNT || idToHost[ id ] < 0 )
		return;
	SetFloatParameter( static_cast< unsigned int >( idToHost[ id ] ), value );
}

FFResult EidophonePlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= hostOrder.size() )
		return FF_FAIL;
	const unsigned int id = hostOrder[ index ];

	// The About buttons open a browser and store nothing.
	if( id >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( id - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//Events act on the press, once: 1 on press, 0 on release, and a host is
	//free to restate either.
	if( id == PT_POUR )
	{
		const bool down = value >= 0.5f;
		if( down && !pourHeld )
			pourPending = true;
		pourHeld = down;
	}
	if( id == PT_RESCAN )
	{
		const bool down = value >= 0.5f;
		if( down && !rescanHeld )
			refreshDeviceMenu();
		rescanHeld = down;
	}
	if( id == PT_DEVICE )
		audio.Select( value );
	params[ id ] = value;
	return FF_SUCCESS;
}

float EidophonePlugin::GetFloatParameter( unsigned int index )
{
	return index < hostOrder.size() ? params[ hostOrder[ index ] ] : 0.0f;
}

} // namespace eido
