/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call. (woofer's glue, which is
    mirrorball's and fountain's, with eidophone's names, its two plugins and
    earshot's feed.)

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Eidophone.cpp` and everything it calls -- the plate's
    modes and its oscillators, the audio law and earshot under it -- compiled
    UNMODIFIED into the same WebAssembly module as this file (see
    demo/tools/build-wasm.sh for the list). This file only plays the host:

      - it constructs an `EidophonePlugin`, the source or the Over, as
        SourcePlugin.cpp's and EffectPlugin.cpp's registrations would, and
        calls InitGL / DeInitGL on it;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (CFFGLPluginManager::GetParamName and the rest,
        the SDK compiled unmodified too), so the page's panel is built from
        the plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, SetTime, the Audio
        buffer's elements (Resolume's FFT) and ProcessOpenGL with no input
        (the source) or one input texture (the Over), as tools/edtest's Rig
        does.

    Two things here are the plugin's HARNESS rather than a host, as edtest's
    Rig::Init has them:

      - the clock: after InitGL the instance is told its host clock is in
        seconds (`SetClockScaleForTest( 1.0 )`, exactly what Rig::Init calls),
        instead of voting on seconds-or-milliseconds against the wall clock for
        its first frames as it does in Resolume;
      - the audio. edtest never opens a device; it hands the plugin its audio
        through earshot's feed() (`AudioInput::Feed`, which is
        `Engine::feedBegin` and `Engine::feed`), the path a device's samples
        take inside earshot, each frame fed up to the end of its interval. So
        does this page. The page's track is a sequence of held notes, each made
        by edtest's own `sine()` (copied from tools/edtest/main.cpp character
        for character and held to it by demo/tools/check_shaders.py) at
        edtest's -12 dBFS, with a raised-cosine fade at each end; the notes'
        frequencies come from the plugin's own mode table, by
        `defaultToneHz()`'s arithmetic (that function copied the same way,
        and the track's last note is it: `edtest --tone 0`). A file the
        visitor chooses is decoded by the page and reaches feed() through
        `ed_feed`, as `edtest --wav` hands a WAV over. And `ed_host_bins`
        writes `edtest --bins J`'s made-up 64-bin buffer into the Audio
        parameter (the formula held to edtest's), so the plugin's own fallback
        runs on it.

    No audio device exists in this module: earshot is built with
    EARSHOT_CAPTURE=OFF (NoCapture.cpp; miniaudio is not compiled), and
    `ed_prepare` sets EIDOPHONE_NO_DEVICES=1, verify.sh's switch, so the
    plugin's AudioInput::Refresh never asks for a list. Nothing calls
    selectDevice: the page never sets Device.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the five entry points that cannot be passed
    straight through.
*/
#include "Controls.h"
#include "Diag.h"
#include "Eidophone.h"
#include "Modes.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace eido;

namespace
{
using Floats = std::vector< float >;

//---------------------------------------------------------------------------
// tools/edtest/main.cpp's sine and its default tone, verbatim
// (check_shaders.py compares).
//---------------------------------------------------------------------------
std::vector< float > sine( double hz, double db, int rate, double seconds, double phase = 0.0 )
{
	const size_t n = static_cast< size_t >( seconds * rate );
	std::vector< float > out( 2 * n );
	const double amp = std::pow( 10.0, db / 20.0 );
	for( size_t i = 0; i < n; ++i )
		out[ 2 * i ] = out[ 2 * i + 1 ] = static_cast< float >( amp * std::sin( phase + 2.0 * kPi * hz * static_cast< double >( i ) / rate ) );
	return out;
}

double defaultToneHz()
{
	const MaterialData& m = MaterialOf( Material::Aluminium );
	const auto table      = EidophonePlugin::TableFor( Shape::Square, m.nu );
	const double L = 0.5 * SizeFromParam( InfoOf( PT_SIZE ).defaultValue );
	const double c = PlateSpeed( m, ThicknessFromParam( InfoOf( PT_THICKNESS ).defaultValue ) );
	double best = 400.0, bestErr = 1e300;
	for( const modes::Mode& mode : table->modes )
	{
		if( mode.cls != modes::kSS )
			continue;
		const double hz = mode.Lambda * c / ( L * L ) / ( 2.0 * kPi );
		if( std::fabs( std::log( hz / 400.0 ) ) < bestErr )
		{
			bestErr = std::fabs( std::log( hz / 400.0 ) );
			best    = hz;
		}
	}
	return best;
}

//---------------------------------------------------------------------------
// The page's track. Its choices, not the plugin's: which notes, how long,
// the fades and the gaps. Every sample is edtest's sine().
//---------------------------------------------------------------------------
constexpr int kFeedRate = 48000;
constexpr double kToneDb = -12.0;  ///< edtest --tone's level
constexpr double kFade   = 0.05;   ///< s, a raised cosine at each end of a note
constexpr double kGap    = 1.5;    ///< s of zeros after each note: under earshot's 3 s to Silent

/// The default plate's centre-driven (SS-class) modes, ascending, in Hz: the
/// modes a push at the centre rings. defaultToneHz()'s arithmetic, every mode.
std::vector< double > centreModesHz()
{
	const MaterialData& m = MaterialOf( Material::Aluminium );
	const auto table      = EidophonePlugin::TableFor( Shape::Square, m.nu );
	const double L = 0.5 * SizeFromParam( InfoOf( PT_SIZE ).defaultValue );
	const double c = PlateSpeed( m, ThicknessFromParam( InfoOf( PT_THICKNESS ).defaultValue ) );
	std::vector< double > out;
	for( const modes::Mode& mode : table->modes )
		if( mode.cls == modes::kSS )
			out.push_back( mode.Lambda * c / ( L * L ) / ( 2.0 * kPi ) );
	std::sort( out.begin(), out.end() );
	return out;
}

struct Note
{
	double hz;
	double seconds;
	int start;///< its first frame in the track
	int frames;
};

struct Track
{
	std::vector< Note > notes;
	Floats samples;///< interleaved stereo at kFeedRate
	int Frames() const
	{
		return static_cast< int >( samples.size() / 2 );
	}
};

const Track& track()
{
	static Track t;
	if( !t.notes.empty() )
		return t;
	const std::vector< double > centre = centreModesHz();
	const double d2 = 440.0 * std::pow( 2.0, ( 38 - 69 ) / 12.0 );//MIDI 38
	const double plan[][ 2 ] = {
		{ d2, 7.0 },              //a bass note between the first two centre modes
		{ centre.at( 1 ), 7.0 },  //the second centre mode: the four-pointed star
		{ centre.at( 2 ), 9.0 },
		{ centre.at( 3 ), 10.0 },
		{ defaultToneHz(), 12.0 },//edtest --tone 0: the centre mode nearest 400 Hz
	};
	for( const auto& row : plan )
	{
		Note note { row[ 0 ], row[ 1 ], t.Frames(), 0 };
		Floats tone = sine( note.hz, kToneDb, kFeedRate, note.seconds );
		const int n = static_cast< int >( tone.size() / 2 );
		const int ramp = static_cast< int >( std::lround( kFade * kFeedRate ) );
		for( int i = 0; i < n; ++i )
		{
			const int edge = std::min( i, n - 1 - i );
			if( edge >= ramp )
				continue;
			const float g = static_cast< float >( 0.5 - 0.5 * std::cos( kPi * edge / ramp ) );
			tone[ static_cast< size_t >( 2 * i ) ] *= g;
			tone[ static_cast< size_t >( 2 * i + 1 ) ] *= g;
		}
		note.frames = n;
		t.samples.insert( t.samples.end(), tone.begin(), tone.end() );
		t.samples.resize( t.samples.size() + static_cast< size_t >( 2 * std::lround( kGap * kFeedRate ) ), 0.0f );
		t.notes.push_back( note );
	}
	return t;
}

//---------------------------------------------------------------------------
// One plugin instance and the host-side structs it is handed.
//---------------------------------------------------------------------------
struct Instance
{
	explicit Instance( bool effect ) :
		plugin( effect )
	{
	}
	EidophonePlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	/// The feed's absolute sample clock at kFeedRate: samples [0, sent) have
	/// gone to feed(), as edtest's Feeder counts them.
	uint64_t sent = 0;

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}

/// The ParamId behind a host index of this instance's plugin.
unsigned int idOf( Instance* instance, int index )
{
	const auto& order = HostOrder( instance->plugin.IsEffect() );
	return index >= 0 && static_cast< size_t >( index ) < order.size() ? order[ static_cast< size_t >( index ) ] : PT_COUNT;
}
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------

/// Before the first instance: give the plugin's log (Diag.cpp) somewhere to
/// go, a directory in the page's in-memory file system named through the
/// override Diag.cpp reads (its own `mkdir -p` goes through system(), which a
/// browser does not have); and EIDOPHONE_NO_DEVICES=1, verify.sh's switch, so
/// AudioInput::Refresh never asks earshot for a device list (a build with no
/// capture backend would return none anyway).
EMSCRIPTEN_KEEPALIVE void ed_prepare()
{
	setenv( "EIDOPHONE_LOG_DIR", "/eidophone/logs", 1 );
	setenv( "EIDOPHONE_NO_DEVICES", "1", 1 );
	mkdir( "/eidophone", 0755 );
	mkdir( "/eidophone/logs", 0755 );
}

/// SW Eidophone (0) or SW Eido Over (1), as its registration constructs it.
EMSCRIPTEN_KEEPALIVE Instance* ed_new( int effect )
{
	return new Instance( effect != 0 );
}

EMSCRIPTEN_KEEPALIVE void ed_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass, then edtest's Rig::Init's
/// clock preset. 1 on success.
EMSCRIPTEN_KEEPALIVE int ed_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	if( instance->initialised )
		instance->plugin.SetClockScaleForTest( 1.0 );
	return instance->initialised ? 1 : 0;
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ed_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* ed_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int ed_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ed_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float ed_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int ed_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ed_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float ed_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE int ed_param_usage( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamUsage( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int ed_min_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMinInputs() );
}

EMSCRIPTEN_KEEPALIVE int ed_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ed_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float ed_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

/// One frame: SetTime, then ProcessOpenGL into whatever the page has bound
/// (the canvas: HostFBO 0). The Over is handed one input of clipWidth x
/// clipHeight whose GL name is `clip` (a texture the page registered with
/// emscripten's GL tables); the source none. 1 on success.
EMSCRIPTEN_KEEPALIVE int ed_process( Instance* instance, double seconds, int clip, int clipWidth, int clipHeight )
{
	instance->plugin.SetTime( seconds );
	if( instance->plugin.IsEffect() )
	{
		instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
		instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
		instance->input.Handle                                 = static_cast< GLuint >( clip );
		instance->process.numInputTextures                     = 1;
		instance->process.inputTextures                        = instance->inputs;
	}
	else
	{
		instance->process.numInputTextures = 0;
		instance->process.inputTextures    = nullptr;
	}
	instance->process.HostFBO = 0;
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// The audio, as edtest hands it over. Each source is a fresh instance (the
// page's rule): a feed, once begun, is what earshot's Engine reads until a
// selectDevice, and nothing here selects.
//---------------------------------------------------------------------------

/// Build the page's track (once per module), and say how long it is in
/// frames at 48 kHz.
EMSCRIPTEN_KEEPALIVE int ed_track_frames()
{
	return track().Frames();
}

EMSCRIPTEN_KEEPALIVE int ed_track_note_count()
{
	return static_cast< int >( track().notes.size() );
}

/// Note i: 0 its frequency in Hz, 1 its length in s, 2 its first frame, 3 its
/// frames (the gap after it is not counted).
EMSCRIPTEN_KEEPALIVE double ed_track_note( int i, int what )
{
	const auto& notes = track().notes;
	if( i < 0 || i >= static_cast< int >( notes.size() ) )
		return std::numeric_limits< double >::quiet_NaN();
	const Note& n = notes[ static_cast< size_t >( i ) ];
	switch( what )
	{
	case 0: return n.hz;
	case 1: return n.seconds;
	case 2: return n.start;
	case 3: return n.frames;
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// The track's samples, interleaved stereo float at 48 kHz, for anything
/// checking the page from outside (the crosscheck hands them to edtest --wav).
EMSCRIPTEN_KEEPALIVE const float* ed_track_samples()
{
	return track().samples.data();
}

/// The centre-driven modes of the default plate, by index (Hz), for the page's
/// notes on the track; NaN past the last.
EMSCRIPTEN_KEEPALIVE double ed_centre_mode_hz( int i )
{
	static const std::vector< double > centre = centreModesHz();
	return i >= 0 && i < static_cast< int >( centre.size() ) ? centre[ static_cast< size_t >( i ) ] : std::numeric_limits< double >::quiet_NaN();
}

/// Begin the feed with no samples yet: earshot's feedBegin( 48000, 2 ), its
/// state Running and the frame's source the device path from here on.
EMSCRIPTEN_KEEPALIVE void ed_feed_begin( Instance* instance )
{
	static const float none[ 2 ] = { 0.0f, 0.0f };
	instance->plugin.Audio().Feed( none, 0, kFeedRate, 2 );
}

/// The page's track, looped, up to absolute sample `end` at kFeedRate:
/// samples [sent, end) into feed(), as edtest's Feeder::Before feeds a clip.
/// The page's `end` is (its clock + 1/60 s) x 48 000, rounded: edtest feeds
/// frame f up to llround( ( f + 1 ) / fps x rate ), the end of its interval.
EMSCRIPTEN_KEEPALIVE int ed_feed_track( Instance* instance, double end )
{
	const Track& t     = track();
	const double wanted = std::floor( end + 0.5 );
	if( !( wanted > static_cast< double >( instance->sent ) ) || t.Frames() == 0 )
		return 0;
	int left = static_cast< int >( std::min( wanted - static_cast< double >( instance->sent ), 4.0 * kFeedRate ) );
	const int total = left;
	while( left > 0 )
	{
		const int at   = static_cast< int >( instance->sent % static_cast< uint64_t >( t.Frames() ) );
		const int take = std::min( left, t.Frames() - at );
		instance->plugin.Audio().Feed( &t.samples[ static_cast< size_t >( 2 * at ) ], take, kFeedRate, 2 );
		instance->sent += static_cast< uint64_t >( take );
		left -= take;
	}
	return total;
}

/// A file the visitor chose, decoded by the page: `frames` interleaved frames
/// of `channels` at `rate`, into feed() as `edtest --wav` hands a WAV over.
EMSCRIPTEN_KEEPALIVE void ed_feed( Instance* instance, const float* interleaved, int frames, int rate, int channels )
{
	instance->plugin.Audio().Feed( interleaved, frames, rate, channels );
}

/// edtest --bins J: nothing fed; Resolume's FFT buffer only, with bin J at
/// 0.5, bin 0 at 0.9 and the rest at 0.02, written once, as edtest writes it
/// before its first frame. The plugin's fallback then takes the strongest bin
/// but 0 as its guess at the note.
EMSCRIPTEN_KEEPALIVE void ed_host_bins( Instance* instance, int hostBin )
{
	const int host = instance->plugin.HostIndexOf( PT_AUDIO );
	for( int j = 0; j < 64; ++j )
		instance->plugin.SetParamElementValue( static_cast< unsigned int >( host ), static_cast< unsigned int >( j ),
		                                 j == hostBin ? 0.5f : ( j == 0 ? 0.9f : 0.02f ) );
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one Eidophone.cpp's ProcessOpenGL applies to it. Metres for Size and
// Thickness, a fraction of the half-width for Drive At, Q for Damping, g for
// Gain, grains for Amount, px at 1080 lines for Grain Size. NaN where the
// plugin uses the host's 0..1 as it is (Mix), or an index.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double ed_convert( Instance* instance, int index, float v )
{
	switch( idOf( instance, index ) )
	{
	case PT_SIZE: return SizeFromParam( v );
	case PT_THICKNESS: return ThicknessFromParam( v );
	case PT_DRIVE_AT: return DriveAtFromParam( v );
	case PT_DAMPING: return DampingFromParam( v );
	case PT_GAIN: return GainFromParam( v );
	case PT_CLARITY: return ClarityFromParam( v );
	case PT_AMOUNT: return AmountFromParam( v );
	case PT_HOP: return HopFromParam( v );
	case PT_AIR: return AirFromParam( v );
	case PT_GRAIN_SIZE: return GrainSizeFromParam( v );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// And back: the host's 0..1 for a value in the plugin's units, by the
/// inverse Controls.cpp pairs with each conversion (the one its constructor
/// sets the defaults with). NaN for a parameter with no conversion.
EMSCRIPTEN_KEEPALIVE double ed_param_from( Instance* instance, int index, double value )
{
	switch( idOf( instance, index ) )
	{
	case PT_SIZE: return ParamFromSize( value );
	case PT_THICKNESS: return ParamFromThickness( value );
	case PT_DRIVE_AT: return ParamFromDriveAt( value );
	case PT_DAMPING: return ParamFromDamping( value );
	case PT_GAIN: return ParamFromGain( value );
	case PT_CLARITY: return ParamFromClarity( value );
	case PT_AMOUNT: return ParamFromAmount( static_cast< int >( std::lround( value ) ) );
	case PT_HOP: return ParamFromHop( value );
	case PT_AIR: return ParamFromAir( value );
	case PT_GRAIN_SIZE: return ParamFromGrainSize( value );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

//---------------------------------------------------------------------------
// What the line under the picture reports: the plugin's own state, through
// the accessors its harness reads (LastFrame(), PlateState(), Audio(),
// HostNow(), StepCounter()).
//---------------------------------------------------------------------------

/// The last frame: 0 the drive's Hz, 1 a note set it this frame (1) or not,
/// 2 no pitch yet and a partial seeded it, 3 from the device path (feed()),
/// 4 the fallback's bin (-1 none), 5 the loudness, 6 relative, 7 F0 in N,
/// 8 substeps run, 9 grains, 10 poured this frame, 11 the plate's half-size
/// in m, 12 dt.
EMSCRIPTEN_KEEPALIVE double ed_frame( Instance* instance, int what )
{
	const FrameData& f = instance->plugin.LastFrame();
	switch( what )
	{
	case 0: return f.drive.hz;
	case 1: return f.drive.note ? 1.0 : 0.0;
	case 2: return f.drive.seeded ? 1.0 : 0.0;
	case 3: return f.drive.fromDevice ? 1.0 : 0.0;
	case 4: return f.drive.bin;
	case 5: return f.drive.loudness;
	case 6: return f.drive.relative;
	case 7: return f.drive.F0;
	case 8: return f.substeps;
	case 9: return f.grains;
	case 10: return f.poured ? 1.0 : 0.0;
	case 11: return f.halfSize;
	case 12: return f.dt;
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// The elastic mode of the plate as it is now nearest the drive's frequency,
/// among those the drive point couples to (|W_k(x_d)| over a thousandth of
/// the largest): its frequency in Hz, or NaN. The page's arithmetic on
/// Plate's own Omega() and Coupling().
EMSCRIPTEN_KEEPALIVE double ed_nearest_mode_hz( Instance* instance, double hz )
{
	const Plate& plate = instance->plugin.PlateState();
	double largest     = 0.0;
	for( int k = 0; k < plate.Count(); ++k )
		if( plate.Omega( k ) > 0.0 )
			largest = std::max( largest, std::fabs( plate.Coupling( k ) ) );
	double best = std::numeric_limits< double >::quiet_NaN(), err = 1e300;
	for( int k = 0; k < plate.Count(); ++k )
	{
		if( plate.Omega( k ) <= 0.0 || std::fabs( plate.Coupling( k ) ) < 1e-3 * largest )
			continue;
		const double f = plate.Omega( k ) / ( 2.0 * kPi );
		if( hz > 0.0 && std::fabs( std::log( f / hz ) ) < err )
		{
			err  = std::fabs( std::log( f / hz ) );
			best = f;
		}
	}
	return best;
}

EMSCRIPTEN_KEEPALIVE int ed_mode_count( Instance* instance )
{
	const modes::Table* table = instance->plugin.ModeTable();
	return table ? static_cast< int >( table->modes.size() ) : 0;
}

/// earshot's CaptureState, as a number (Closed, Opening, Running, Silent,
/// Missing, Failed), and its name.
EMSCRIPTEN_KEEPALIVE int ed_capture_state( Instance* instance )
{
	return static_cast< int >( instance->plugin.Audio().State() );
}
EMSCRIPTEN_KEEPALIVE const char* ed_capture_state_name( Instance* instance )
{
	return earshot::toString( instance->plugin.Audio().State() );
}
/// The host's time as the plugin reads it, seconds.
EMSCRIPTEN_KEEPALIVE double ed_host_now( Instance* instance )
{
	return instance->plugin.HostNow();
}
/// Grain-update substeps run since this instance started (the Step uniform's
/// next value: each substep's random numbers are seeded by it).
EMSCRIPTEN_KEEPALIVE double ed_step_counter( Instance* instance )
{
	return static_cast< double >( instance->plugin.StepCounter() );
}
/// Samples fed so far at 48 kHz, by ed_feed_track.
EMSCRIPTEN_KEEPALIVE double ed_fed( Instance* instance )
{
	return static_cast< double >( instance->sent );
}

/// The plugin's log file (Diag.cpp), in the page's in-memory file system.
EMSCRIPTEN_KEEPALIVE const char* ed_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

} // extern "C"
