#include "Audio.h"

#include "Diag.h"

#include <earshot/testing.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace eido
{
namespace
{
BackendFactory g_factory = nullptr;

constexpr const char* kHostLabel    = "Resolume FFT (no pitch)";
constexpr const char* kDefaultLabel = "System default";
constexpr const char* kEmptySlot    = "(none)";

/// EIDOPHONE_NO_DEVICES=1: never ask the OS for its devices (verify.sh sets it
/// for oxbow, so no step of verification talks to CoreAudio at all; woofer's).
bool devicesDisabled()
{
	const char* v = std::getenv( "EIDOPHONE_NO_DEVICES" );
	return v != nullptr && *v != '\0' && std::strcmp( v, "0" ) != 0;
}

earshot::Config makeConfig()
{
	earshot::Config c;
	c.maxChannels = 2;
	return c;
}
} // namespace

double BinHz( int j, BinLaw law, double sampleRate, int bins )
{
	//earshot's HostAnalyser::binHz, the same two laws: bin j's centre.
	const double nyquist = 0.5 * sampleRate;
	const double u       = ( j + 0.5 ) / std::max( 1, bins );
	if( law == BinLaw::Log )
		return 20.0 * std::pow( nyquist / 20.0, u );
	return u * nyquist;
}

Drive DriveFrom( const earshot::Frame& frame, const float* bins, int binCount, BinLaw law, double previousOmega, int hooks )
{
	Drive d;
	d.omega = previousOmega;
	if( frame.source == earshot::Source::Device )
	{
		d.fromDevice = true;
		//The loudness: the band envelopes' RMS, voiced or not.
		double e2 = 0.0;
		for( int b = 0; b < frame.bandCount; ++b )
			e2 += static_cast< double >( frame.bands[ b ].envelope ) * frame.bands[ b ].envelope;
		d.loudness = std::sqrt( e2 );
		if( frame.pitch.valid && frame.pitch.voiced && frame.pitch.hz > 0.0f )
		{
			d.note  = true;
			d.hz    = frame.pitch.hz;
			d.omega = ( hooks & kHookHzAsOmega ) ? d.hz : 2.0 * kPi * d.hz;
		}
		else if( previousOmega <= 0.0 )
		{
			//No pitch yet: the strongest partial in the pitch range (loudest first).
			for( int i = 0; i < frame.partialCount; ++i )
			{
				const double hz = frame.partials[ i ].hz;
				if( hz >= earshot::Config().pitchMinHz && hz <= earshot::Config().pitchMaxHz )
				{
					d.seeded = true;
					d.hz     = hz;
					d.omega  = 2.0 * kPi * hz;
					break;
				}
			}
		}
		else
			d.hz = previousOmega / ( 2.0 * kPi );
		return d;
	}
	if( frame.source != earshot::Source::Host || bins == nullptr || binCount < 2 )
		return d;
	//The fallback: no pitch exists. The strongest bin (bin 0 left out: it is
	//DC and everything below its upper edge) is the guess.
	int best         = -1;
	double bestValue = 0.0;
	for( int j = ( hooks & kHookBinZero ) ? 0 : 1; j < binCount; ++j )
	{
		const double value = bins[ j ];
		if( std::isfinite( value ) && value > bestValue )
		{
			bestValue = value;
			best      = j;
		}
	}
	if( best < 0 )
		return d;
	d.note     = true;
	d.bin      = best;
	d.hz       = BinHz( best, law, earshot::HostBins().sampleRate, binCount );//44.1 kHz, assumed
	d.omega    = 2.0 * kPi * d.hz;
	d.loudness = bestValue;
	return d;
}

double ReleasePeak( double peak, double loudness, double dt, int hooks )
{
	const double released = ( hooks & kHookPeakHeld ) ? peak : peak * std::exp( -std::max( dt, 0.0 ) / kPeakRelease );
	return std::max( { loudness, released, kPeakFloor } );
}

void AudioInput::SetBackendFactoryForTest( BackendFactory factory )
{
	g_factory = factory;
}

const char* AudioInput::HostLabel()
{
	return kHostLabel;
}

AudioInput::AudioInput()
{
	if( g_factory != nullptr )
		engine = std::make_unique< earshot::Engine >( makeConfig(), g_factory() );
	else
		engine = std::make_unique< earshot::Engine >( makeConfig() );
	if( !engine->ok() )
		diag::error( "earshot could not allocate: the plate will not be driven" );
	Refresh();
}

AudioInput::~AudioInput() = default;

void AudioInput::Refresh()
{
	slots.clear();
	slots.push_back( { kHostLabel, "", 0.0f } );
	slots.push_back( { kDefaultLabel, earshot::kDefaultDevice, earshot::deviceKey( earshot::kDefaultDevice ) } );
	std::vector< earshot::DeviceInfo > devices;
	if( g_factory != nullptr )
	{
		//A throwaway backend of the harness's kind: it lists, it opens nothing.
		if( auto lister = g_factory() )
			devices = lister->enumerate();
	}
	else if( !devicesDisabled() )
		devices = earshot::listDevices();
	int listed = 0;
	for( const earshot::DeviceInfo& d : devices )
	{
		if( static_cast< int >( slots.size() ) >= kDeviceElements )
			break;
		const float key = ( hooks & kHookMenuByIndex ) ? static_cast< float >( slots.size() ) : earshot::deviceKey( d.name.c_str() );
		seen[ key ]     = d.name;
		slots.push_back( { d.name.substr( 0, 40 ), d.name, key } );
		++listed;
	}
	//Spares: distinct values no key can take (keys are 0 .. 2^23 - 1), never "".
	for( int spare = 1; static_cast< int >( slots.size() ) < kDeviceElements; ++spare )
		slots.push_back( { kEmptySlot, "", -static_cast< float >( spare ) } );
	diag::info( "audio devices: " + std::to_string( listed ) + " listed" );

	//A device remembered by key and now present again: open it by name.
	if( applied > 0.0f )
	{
		const auto known = seen.find( applied );
		if( known != seen.end() && known->second != requested )
		{
			requested = known->second;
			feeding   = false;
			engine->selectDevice( requested.c_str() );
			diag::info( "audio device after rescan: " + requested );
		}
	}
}

void AudioInput::Select( float value )
{
	if( value == applied || !std::isfinite( value ) )
		return;
	applied = value;
	feeding = false;//any select ends a feed
	std::string name;
	if( value <= 0.0f )
		name.clear();//the host FFT, or a spare
	else if( value == earshot::deviceKey( earshot::kDefaultDevice ) )
		name = earshot::kDefaultDevice;
	else
	{
		const auto known = seen.find( value );
		//A key with no device met in this process: ask for a name that cannot
		//match, so earshot says Missing (never the default). Rescan resolves it
		//once the device is back.
		name = known != seen.end() ? known->second : "(missing device " + std::to_string( static_cast< long >( value ) ) + ")";
	}
	requested = name;
	engine->selectDevice( name.c_str() );
	diag::info( "audio device: " + ( name.empty() ? std::string( kHostLabel ) : name ) );
}

const earshot::Frame& AudioInput::Frame() const
{
	return engine->frame();
}

earshot::CaptureState AudioInput::State() const
{
	return engine->captureState();
}

void AudioInput::Feed( const float* interleaved, int frames, int rate, int channels )
{
	if( !feeding )
	{
		engine->feedBegin( rate, channels );
		feeding = true;
	}
	engine->feed( interleaved, frames );
}

Drive AudioInput::Update( double hostSeconds, const float* bins, int binCount, double clarity, BinLaw law )
{
	if( clarity != appliedClarity )
	{
		earshot::Config c = engine->config();
		c.pitchClarity    = static_cast< float >( clarity );
		engine->configure( c );
		appliedClarity = clarity;
	}
	const earshot::Frame& f = engine->update( hostSeconds, bins, binCount );
	if( f.capture != lastState )
	{
		earshot::Status status;
		engine->status( status );
		std::string line = std::string( "capture " ) + earshot::toString( lastState ) + " -> " + earshot::toString( f.capture );
		if( status.requested[ 0 ] )
			line += std::string( " requested=\"" ) + status.requested + "\"";
		if( status.opened[ 0 ] )
			line += std::string( " opened=\"" ) + status.opened + "\"";
		if( status.error[ 0 ] )
			line += std::string( " error=\"" ) + status.error + "\"";
		if( f.capture == earshot::CaptureState::Silent || f.capture == earshot::CaptureState::Missing || f.capture == earshot::CaptureState::Failed )
			diag::warn( line );
		else
			diag::info( line );
		lastState = f.capture;
	}
	//A new stream (another device, a restart) starts without a pitch.
	if( f.generation != generation )
	{
		generation = f.generation;
		lastOmega  = 0.0;
	}
	Drive d   = DriveFrom( f, bins, binCount, law, lastOmega, hooks );
	lastOmega = d.omega;
	const double dt = lastSeconds >= 0.0 ? hostSeconds - lastSeconds : 0.0;
	lastSeconds     = hostSeconds;
	peak            = ReleasePeak( peak, d.loudness, dt, hooks );
	d.relative      = d.loudness >= kGate ? d.loudness / peak : 0.0;
	return d;
}

} // namespace eido
