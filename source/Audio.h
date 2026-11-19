#pragma once

#include "Controls.h"

#include <earshot/earshot.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace earshot::testing
{
class CaptureBackend;
}

/**
    Where the drive comes from: earshot.

    **A device** (earshot's own capture, opened on earshot's worker, never on
    the render thread): a voiced pitch (NSDF clarity >= Clarity) sets the
    drive's frequency to 2 pi x pitch, and it holds until the next voiced
    pitch; until the first, the strongest partial between earshot's pitch
    limits stands in. The loudness is the band envelopes' RMS (earshot's
    5 ms attack, 150 ms release, so onsets punch), every frame, voiced or not.

    **Resolume's FFT** (no device chosen, or a chosen one Opening, Missing or
    Failed): earshot's fallback has NO pitch. The plugin takes the strongest
    of the 64 bins (bin 0 left out) as a guess at the note, at that bin's
    centre under Bin Law, and the bin's value as the loudness. The bins' axis
    is the host's audio engine rate (FFGL SetSampleRate, handed in by
    SetHostRate; earshot's 48 kHz until a host sends one). Arena 7.29.0 was
    measured (earshot's AGENTS.md, "Resolume's 64 bins, measured"): linear,
    bin j = [j, j+1) x 375 Hz at its 48 kHz engine, also for a 44.1 kHz
    clip, which is Bin Law Linear's centre ( j + 0.5 ) x 375 Hz. v0.1.0 read
    them at 44.1 kHz, 1.47 semitones flat (x 0.919). Under Linear every bass
    note lands in bin 0 or 1. It is a guess, and the menu says so: "Resolume
    FFT (no pitch)".

    **The level** either way is the loudness over its own running peak
    (released with a time constant of kPeakRelease; never under kPeakFloor,
    so at most 40 dB of make-up; nothing under kGate). The plugin turns it
    into a force: Gain is the steady-state peak acceleration, in g, that the
    loudest recent passage gives at the current frequency (Plate::
    PeakPerNewton). Resolume's demo WAVs are quiet (Synth 001 near -41 dBFS)
    and rarely voiced, and drove nothing under the spec's fixed force (AGENTS.md).

    **The menu** (spasis's rules, earshot's INTEGRATING.md recipe): element 0
    "Resolume FFT (no pitch)" = 0, element 1 "System default" =
    deviceKey( kDefaultDevice ), then each device with deviceKey( name ), then
    "(none)" spares with distinct negative values, kDeviceElements in all. A
    device is remembered by name. `Select` acts only when the value changes,
    so the first frame never calls selectDevice( "" ), which would end a
    feed() (woofer's trap).
*/
namespace eido
{

using BackendFactory = std::unique_ptr< earshot::testing::CaptureBackend > ( * )();

struct DeviceSlot
{
	std::string label;///< what the menu shows
	std::string name; ///< what selectDevice is given ("" for the host FFT)
	float value;      ///< the element's value
};

/// The plate's drive for one frame.
struct Drive
{
	double F0       = 0.0;///< N: filled by the plugin from `relative`, Gain and the plate
	double omega    = 0.0;///< rad/s (held at its last value when there is no note)
	double hz       = 0.0;
	double loudness = 0.0;///< equivalent sine amplitude (full scale 1; the bin's raw value on the fallback)
	double relative = 0.0;///< loudness over its running peak, 0..1: what Gain scales
	bool note       = false;///< a pitch (or, on the fallback, a bin) set the frequency this frame
	bool seeded     = false;///< no pitch yet: the strongest partial set it
	bool fromDevice = false;
	int bin         = -1;///< fallback: the bin taken
};

/// Negative controls (bits of the plugin's hook word, above Plate's and the
/// shaders'). A shipped instance sets none.
enum AudioHook : int
{
	kHookHzAsOmega   = 1 << 10,///< the pitch in Hz taken as the angular frequency
	kHookBinZero     = 1 << 11,///< the fallback lets bin 0 (DC and the lowest bass) win
	kHookMenuByIndex = 1 << 12,///< the menu values devices by position, not by name
	kHookPeakHeld    = 1 << 14,///< the running peak never released (1 << 13 is the shaders')
	kHookIgnoreHostRate = 1 << 15,///< the host's sample rate never reaches the bins' axis (earshot's 48 kHz stays)
	kHookAssumed441  = 1 << 16 ///< the bins read on a 44.1 kHz axis whatever the host says (v0.1.0's)
};

/// The running peak: released with this time constant, never under the floor
/// (-40 dBFS: at most 40 dB of make-up, so a hiss at -60 drives a tenth of
/// Gain), and no level at all under the gate (-60 dBFS, earshot's pitch gate).
constexpr double kPeakRelease = 8.0; ///< s
constexpr double kPeakFloor   = 0.01;
constexpr double kGate        = 0.001;

/// The centre of bin j of `bins` under the Bin Law, on the axis of a host
/// whose audio engine runs at `sampleRate` (Nyquist = sampleRate / 2).
double BinHz( int j, BinLaw law, double sampleRate, int bins = 64 );

/// The frequency and loudness from one earshot frame (device path) or the
/// raw host bins (fallback), read on `hostRate`'s axis. `previousOmega` is
/// held when there is no note; `relative` is left to AudioInput, which keeps
/// the running peak.
Drive DriveFrom( const earshot::Frame& frame, const float* bins, int binCount, BinLaw law, double hostRate, double previousOmega, int hooks = 0 );

/// One step of the running peak: max( loudness, peak e^( -dt / kPeakRelease ) ),
/// never under kPeakFloor.
double ReleasePeak( double peak, double loudness, double dt, int hooks = 0 );

class AudioInput
{
public:
	/// Harness: every AudioInput constructed afterwards uses this backend (a
	/// scripted device) instead of miniaudio and lists its devices.
	static void SetBackendFactoryForTest( BackendFactory factory );
	/// Harness: the label the menu shows for element 0.
	static const char* HostLabel();

	AudioInput();
	~AudioInput();

	/// Enumerate again (constructor and Rescan). Never per frame. If the
	/// applied value now names a listed device, it is selected by name.
	void Refresh();
	const std::vector< DeviceSlot >& Slots() const
	{
		return slots;
	}
	/// The Device value the host stored; acts only when it changes.
	void Select( float value );
	/// What the last Select asked earshot for ("" = the host FFT).
	const std::string& Requested() const
	{
		return requested;
	}

	/// The host's audio engine rate (FFGL SetSampleRate): the axis of its FFT
	/// bins, for earshot's host law and the fallback's note. Applied when it
	/// changes, and logged once per change; 0, or a rate no engine runs at,
	/// keeps what earshot has (48 kHz until a host says otherwise).
	void SetHostRate( unsigned int rate );
	/// The rate the host's bins are read at.
	double HostRate() const;

	/// Once per frame.
	Drive Update( double hostSeconds, const float* bins, int binCount, double clarity, BinLaw law );

	const earshot::Frame& Frame() const;
	earshot::CaptureState State() const;
	earshot::Engine& Engine()
	{
		return *engine;
	}

	/// Harness: audio as if from a device, without one (earshot's feed()).
	void Feed( const float* interleaved, int frames, int rate, int channels );

	int hooks = 0;///< AudioHook bits (harness only)

private:
	earshot::Config wanted;///< what earshot is given: construction, Clarity and the host's rate all edit this one
	std::unique_ptr< earshot::Engine > engine;
	std::vector< DeviceSlot > slots;
	std::map< float, std::string > seen;///< every key met in this process, and its name
	float applied = 0.0f;
	std::string requested;
	bool feeding = false;
	double appliedClarity = -1.0;
	double lastOmega      = 0.0;
	double peak           = kPeakFloor;
	double lastSeconds    = -1.0;
	unsigned int hostRate = 0;///< the last rate handed to earshot, 0 none
	int heldBin           = -1;///< the fallback's note: the bin taken, and since when
	double heldSince      = 0.0;
	int loggedBin         = -1;///< the last held note the log was told of
	uint32_t generation   = 0;
	earshot::CaptureState lastState = earshot::CaptureState::Closed;
};

} // namespace eido
