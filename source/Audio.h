#pragma once

#include "Controls.h"

#include <earshot/earshot.h>

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
    the render thread): when the pitch is voiced, the drive is a force of
    Gain x 10^( levelDb / 20 ) newtons at 2 pi x pitch. Unvoiced, no force:
    the plate rings down and the sand freezes.

    **Resolume's FFT** (no device chosen, or a chosen one Opening, Missing or
    Failed): earshot's fallback has NO pitch. The plugin takes the strongest
    of the 64 bins (bin 0 left out) as a guess at the note, at that bin's
    centre under Bin Law, with a force of Gain x the bin's value. Nobody has
    measured Resolume's bins; under Linear every bass note lands in bin 0 or
    1. It is a guess, and the menu says so: "Resolume FFT (no pitch)".

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
	double F0    = 0.0;///< N
	double omega = 0.0;///< rad/s (held at its last value when there is no note)
	double hz    = 0.0;
	bool note    = false;///< a pitch (or, on the fallback, a bin) drove the plate
	bool fromDevice = false;
	int bin      = -1;///< fallback: the bin taken
};

/// Negative controls (bits of the plugin's hook word, above Plate's and the
/// shaders'). A shipped instance sets none.
enum AudioHook : int
{
	kHookHzAsOmega   = 1 << 10,///< the pitch in Hz taken as the angular frequency
	kHookBinZero     = 1 << 11,///< the fallback lets bin 0 (DC and the lowest bass) win
	kHookMenuByIndex = 1 << 12 ///< Rescan gives devices their position, not a key from the name
};

/// The centre of 64-bin bin j under the Bin Law, at earshot's assumed 44.1 kHz.
double BinHz( int j, BinLaw law, double sampleRate = 44100.0, int bins = 64 );

/// The drive from one earshot frame (device path) or the raw host bins
/// (fallback). `previousOmega` is held when there is no note.
Drive DriveFrom( const earshot::Frame& frame, const float* bins, int binCount, double gain, BinLaw law, double previousOmega, int hooks = 0 );

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

	/// Once per frame.
	Drive Update( double hostSeconds, const float* bins, int binCount, double gain, double clarity, BinLaw law );

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
	std::unique_ptr< earshot::Engine > engine;
	std::vector< DeviceSlot > slots;
	std::map< float, std::string > seen;///< every key met in this process, and its name
	float applied = 0.0f;
	std::string requested;
	bool feeding = false;
	double appliedClarity = -1.0;
	double lastOmega      = 0.0;
	earshot::CaptureState lastState = earshot::CaptureState::Closed;
};

} // namespace eido
