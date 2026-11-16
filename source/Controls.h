#pragma once

#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse where a default or the harness needs it.
    Option parameters hold the element index (an option reads back 0..1
    whatever its count: map by index) -- except Device, whose elements carry
    `earshot::deviceKey( name )` so a composition remembers the device by
    name. Rescan and Pour are presses; Audio is Resolume's 64-bin FFT buffer.

    Units: metres for the plate (the operator reads cm and mm), newtons for
    the drive, a dimensionless multiplier for Hop and Air.
*/
namespace eido
{
enum ParamId : unsigned int
{
	PT_SHAPE = 0,
	PT_SIZE,
	PT_THICKNESS,
	PT_MATERIAL,
	PT_DRIVE_AT,
	PT_DAMPING,

	PT_DEVICE,
	PT_RESCAN,
	PT_GAIN,
	PT_CLARITY,
	PT_BIN_LAW,
	PT_AUDIO,

	PT_GRAIN,
	PT_AMOUNT,
	PT_HOP,
	PT_AIR,
	PT_POUR,

	PT_VIEW,
	PT_GRAIN_SIZE,
	PT_MIX,///< Over only

	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Event,
	Buffer,
	Device///< an option whose elements are the capture devices, filled at run time
};

struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

const ControlInfo& InfoOf( unsigned int id );

/// Host index -> ParamId for one plugin: the source has no Mix.
const std::vector< unsigned int >& HostOrder( bool effect );

/// An option's element index from whatever the host sent (index, rounded).
int OptionIndex( float value, int count );

enum class Shape
{
	Square = 0,
	Circle,
	Count
};

enum class Material
{
	Aluminium = 0,
	Brass,
	Steel,
	Count
};

enum class BinLaw
{
	Linear = 0,
	Log,
	Count
};

enum class Grain
{
	Sand = 0,
	Powder,
	Both,
	Count
};

enum class View
{
	Fit = 0,
	Fill,
	Count
};

/// E, rho, nu: SPEC-eidophone.md's table, each from a checked datasheet.
struct MaterialData
{
	const char* name;
	double E;   ///< Pa
	double rho; ///< kg/m^3
	double nu;
	double rgb[ 3 ];///< the source's plate colour, display sRGB (chosen)
};
const MaterialData& MaterialOf( Material material );

/// c = sqrt( D / rho h ) = h sqrt( E / ( 12 rho ( 1 - nu^2 ) ) ), m^2/s.
double PlateSpeed( const MaterialData& m, double thickness );

double SizeFromParam( float value );///< side or diameter, m
float ParamFromSize( double metres );
double ThicknessFromParam( float value );///< m
float ParamFromThickness( double metres );
double DriveAtFromParam( float value );///< fraction of the half-width along +x
float ParamFromDriveAt( double fraction );
double DampingFromParam( float value );///< Q
float ParamFromDamping( double q );
/// Gain: the steady-state peak acceleration over the plate, in g, that the
/// loudest recent level gives at the current frequency (0.5-40, geometric).
constexpr double kDefaultGain = 12.0;
double GainFromParam( float value );
float ParamFromGain( double g );
double ClarityFromParam( float value );///< earshot's NSDF threshold
float ParamFromClarity( double clarity );
int AmountFromParam( float value );///< grains
float ParamFromAmount( int grains );
double HopFromParam( float value );
float ParamFromHop( double hop );
double AirFromParam( float value );
float ParamFromAir( double air );
double GrainSizeFromParam( float value );///< px at 1080 lines
float ParamFromGrainSize( double px );

constexpr double kPi = 3.14159265358979323846;
/// The standard acceleration of gravity, exact by definition (NIST CODATA).
constexpr double kGravity = 9.80665;
/// Capture devices the menu has room for: an option's element count is fixed
/// when it is declared, so spare slots exist and Rescan fills them (spasis).
constexpr int kMaxDevices = 15;
constexpr int kDeviceElements = 2 + kMaxDevices;
constexpr int kMaxGrains = 512 * 512;

} // namespace eido
