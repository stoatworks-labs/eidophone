#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace eido
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

constexpr double kSizeLow = 0.10, kSizeHigh = 0.60;
constexpr double kThickLow = 0.3e-3, kThickHigh = 3.0e-3;
constexpr double kDriveHigh = 0.8;
//Q: a sand-loaded plate on a driver. An assumption; nothing is cited for it.
constexpr double kQLow = 5.0, kQHigh = 500.0;
//Gain: the steady peak acceleration, in g, at the loudest recent level.
constexpr double kGainLow = 0.5, kGainHigh = 40.0;
constexpr double kClarityLow = 0.5, kClarityHigh = 0.99;
constexpr double kAmountLow = 1024.0, kAmountHigh = static_cast< double >( kMaxGrains );
constexpr double kHopLow = 0.25, kHopHigh = 4.0;
constexpr double kAirHigh = 100.0;
constexpr double kGrainLow = 0.5, kGrainHigh = 4.0;

const char* const kShapeNames[]    = { "Square", "Circle" };
const char* const kMaterialNames[] = { "Aluminium", "Brass", "Steel" };
const char* const kBinLawNames[]   = { "Linear", "Log" };
const char* const kGrainNames[]    = { "Sand", "Powder", "Both" };
const char* const kViewNames[]     = { "Fit", "Fill" };

/// SPEC-eidophone.md, "Materials": [ASM6061], [C26000] (nu = E/2G - 1 from its
/// E and G), [ASM304] (E at the low end of 193-200 GPa). Colours chosen.
const MaterialData kMaterials[] = {
	{ "Aluminium 6061-T6", 68.9e9, 2700.0, 0.33, { 0.70, 0.72, 0.74 } },
	{ "Brass C26000", 110.317e9, 8530.0, 110317.0 / ( 2.0 * 41369.0 ) - 1.0, { 0.74, 0.60, 0.33 } },
	{ "Stainless 304", 193.0e9, 8000.0, 0.29, { 0.50, 0.52, 0.55 } },
};

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0 };
}

//---------------------------------------------------------------------------
// The defaults: a 30 cm square of 1 mm aluminium pushed at its centre,
// Q 40, 65,536 grains of sand. AGENTS.md ("Decisions") says how each was
// chosen; the demo-clip pass moved some of them.
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	option( PT_SHAPE, "Shape", "Plate", 0.0f, kShapeNames, static_cast< int >( Shape::Count ) ),
	standard( PT_SIZE, "Size", "Plate", ParamFromSize( 0.40 ) ),
	standard( PT_THICKNESS, "Thickness", "Plate", ParamFromThickness( 1.0e-3 ) ),
	option( PT_MATERIAL, "Material", "Plate", 0.0f, kMaterialNames, static_cast< int >( Material::Count ) ),
	standard( PT_DRIVE_AT, "Drive At", "Plate", ParamFromDriveAt( 0.0 ) ),
	standard( PT_DAMPING, "Damping", "Plate", ParamFromDamping( 40.0 ) ),

	{ PT_DEVICE, "Device", "Audio", Kind::Device, 0.0f, nullptr, kDeviceElements },
	event( PT_RESCAN, "Rescan", "Audio" ),
	standard( PT_GAIN, "Gain", "Audio", ParamFromGain( kDefaultGain ) ),
	standard( PT_CLARITY, "Clarity", "Audio", ParamFromClarity( 0.80 ) ),
	option( PT_BIN_LAW, "Bin Law", "Audio", 0.0f, kBinLawNames, static_cast< int >( BinLaw::Count ) ),
	{ PT_AUDIO, "Audio", "Audio", Kind::Buffer, 0.0f, nullptr, 0 },

	option( PT_GRAIN, "Grain", "Grains", 0.0f, kGrainNames, static_cast< int >( Grain::Count ) ),
	standard( PT_AMOUNT, "Amount", "Grains", ParamFromAmount( 65536 ) ),
	standard( PT_HOP, "Hop", "Grains", ParamFromHop( 1.0 ) ),
	standard( PT_AIR, "Air", "Grains", ParamFromAir( 20.0 ) ),
	event( PT_POUR, "Pour", "Grains" ),

	option( PT_VIEW, "View", "View", 0.0f, kViewNames, static_cast< int >( View::Count ) ),
	standard( PT_GRAIN_SIZE, "Grain Size", "View", ParamFromGrainSize( 1.5 ) ),
	standard( PT_MIX, "Mix", "View", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

const std::vector< unsigned int >& HostOrder( bool effect )
{
	auto build = []( bool over ) {
		std::vector< unsigned int > order;
		for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		{
			if( !over && id == PT_MIX )
				continue;
			order.push_back( id );
		}
		for( unsigned int id = PT_ABOUT_TEXT; id < PT_COUNT; ++id )
			order.push_back( id );
		return order;
	};
	static const std::vector< unsigned int > source = build( false ), over = build( true );
	return effect ? over : source;
}

int OptionIndex( float value, int count )
{
	if( !std::isfinite( value ) )
		return 0;
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

const MaterialData& MaterialOf( Material material )
{
	return kMaterials[ std::clamp( static_cast< int >( material ), 0, static_cast< int >( Material::Count ) - 1 ) ];
}

double PlateSpeed( const MaterialData& m, double thickness )
{
	return thickness * std::sqrt( m.E / ( 12.0 * m.rho * ( 1.0 - m.nu * m.nu ) ) );
}

double SizeFromParam( float v )
{
	return geometric( v, kSizeLow, kSizeHigh );
}
float ParamFromSize( double m )
{
	return inverseGeometric( m, kSizeLow, kSizeHigh );
}
double ThicknessFromParam( float v )
{
	return geometric( v, kThickLow, kThickHigh );
}
float ParamFromThickness( double m )
{
	return inverseGeometric( m, kThickLow, kThickHigh );
}
double DriveAtFromParam( float v )
{
	return linear( v, 0.0, kDriveHigh );
}
float ParamFromDriveAt( double f )
{
	return inverseLinear( f, 0.0, kDriveHigh );
}
double DampingFromParam( float v )
{
	return geometric( v, kQLow, kQHigh );
}
float ParamFromDamping( double q )
{
	return inverseGeometric( q, kQLow, kQHigh );
}
double GainFromParam( float v )
{
	return geometric( v, kGainLow, kGainHigh );
}
float ParamFromGain( double g )
{
	return inverseGeometric( g, kGainLow, kGainHigh );
}
double ClarityFromParam( float v )
{
	return linear( v, kClarityLow, kClarityHigh );
}
float ParamFromClarity( double c )
{
	return inverseLinear( c, kClarityLow, kClarityHigh );
}
int AmountFromParam( float v )
{
	return std::clamp( static_cast< int >( std::lround( geometric( v, kAmountLow, kAmountHigh ) ) ), static_cast< int >( kAmountLow ), kMaxGrains );
}
float ParamFromAmount( int grains )
{
	return inverseGeometric( grains, kAmountLow, kAmountHigh );
}
double HopFromParam( float v )
{
	return geometric( v, kHopLow, kHopHigh );
}
float ParamFromHop( double h )
{
	return inverseGeometric( h, kHopLow, kHopHigh );
}
double AirFromParam( float v )
{
	return linear( v, 0.0, kAirHigh );
}
float ParamFromAir( double a )
{
	return inverseLinear( a, 0.0, kAirHigh );
}
double GrainSizeFromParam( float v )
{
	return geometric( v, kGrainLow, kGrainHigh );
}
float ParamFromGrainSize( double px )
{
	return inverseGeometric( px, kGrainLow, kGrainHigh );
}

} // namespace eido
