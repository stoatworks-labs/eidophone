#pragma once

#include "Modes.h"

#include <complex>
#include <memory>
#include <vector>

/**
    The plate's response to the drive: one damped resonator per mode,
    integrated exactly across each frame.

    q_k'' + 2 zeta w_k q_k' + w_k^2 q_k = F0 cos phi( t ) W_k( x_d ) / M,
    zeta = 1/2Q for every mode. Within a frame F0 and the drive's angular
    frequency are constant, so the solution is the steady part
    Re[ G_k e^{i phi} ], G_k = ( F0 W_k( x_d ) / M ) / ( w_k^2 - w_d^2 + 2i zeta w_k w_d ),
    plus a free damped part matched to ( q, q' ) at the frame's start: no
    timestep, no drift, nothing frame-rate dependent for a drive that changes
    on frame boundaries common to the rates compared.

    The rigid-body modes (w = 0) are taken in steady state: acceleration
    F0 W( x_d ) / M e^{i phi}, the uniform shake a wrong note gives.

    All in double, on the CPU. The GPU gets the analytic acceleration of every
    mode, a_k (complex), and sums a_k W_k( x ) on the field lattice.
*/
namespace eido
{

/// Negative controls (bits, the plugin's hook word).
enum PlateHook : int
{
	kHookDampingAtDrive = 1 << 0,///< zeta w_d where zeta w_k belongs
	kHookCouplingOne    = 1 << 1,///< W_k( x_d ) taken as 1 for every mode
	kHookDropRigid      = 1 << 2 ///< no rigid-body response
};

struct PlateConfig
{
	double halfSize  = 0.15; ///< L, m: the half-side, or the radius
	double speed     = 1.5;  ///< c = sqrt( D / rho h ), m^2/s
	double mass      = 0.24; ///< M = rho h A, kg
	double q         = 40.0; ///< Q
	double driveAt   = 0.0;  ///< x_d in plate units along +x
};

class Plate
{
public:
	using Complex = std::complex< double >;

	/// A new mode table (Shape or Material changed): the plate is at rest.
	void SetTable( std::shared_ptr< const modes::Table > table );
	const modes::Table* TablePtr() const
	{
		return table.get();
	}
	/// Size, Thickness, Damping, Drive At. Keeps the modal state.
	void Configure( const PlateConfig& config );

	/// Advance by dt seconds under a drive of amplitude F0 (N) at angular
	/// frequency omegaD (rad/s), both constant over dt.
	void Advance( double dt, double F0, double omegaD );

	/// Back to rest, phase 0.
	void Reset();

	int Count() const
	{
		return static_cast< int >( omega.size() );
	}
	double Omega( int k ) const
	{
		return omega[ static_cast< size_t >( k ) ];
	}
	double Coupling( int k ) const
	{
		return coupling[ static_cast< size_t >( k ) ];
	}
	double Zeta() const
	{
		return zeta;
	}
	double Phase() const
	{
		return phase;
	}
	double Displacement( int k ) const
	{
		return q[ static_cast< size_t >( k ) ];
	}
	double Velocity( int k ) const
	{
		return v[ static_cast< size_t >( k ) ];
	}
	/// The analytic acceleration of mode k at the end of the last Advance, m/s^2
	/// per unit W (so the plate's acceleration is sum_k a_k W_k( x )).
	Complex Acceleration( int k ) const
	{
		return accel[ static_cast< size_t >( k ) ];
	}
	/// The analytic displacement z_k = z_p + z_h (the harness's energy shares).
	Complex AnalyticDisplacement( int k ) const
	{
		return analytic[ static_cast< size_t >( k ) ];
	}
	/// The closed-form steady-state complex amplitude of mode k at this drive.
	Complex SteadyAmplitude( int k, double F0, double omegaD ) const;
	const PlateConfig& Config() const
	{
		return config;
	}
	double LastF0() const
	{
		return lastF0;
	}
	double LastOmega() const
	{
		return lastOmega;
	}

	int hooks = 0;

private:
	void recompute();

	std::shared_ptr< const modes::Table > table;
	PlateConfig config;
	bool configured = false;
	double zeta     = 1.0 / 80.0;
	std::vector< double > omega, coupling, q, v;
	std::vector< Complex > accel, analytic;
	double phase     = 0.0;
	double lastF0    = 0.0;
	double lastOmega = 0.0;
};

} // namespace eido
