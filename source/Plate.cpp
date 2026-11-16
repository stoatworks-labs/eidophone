#include "Plate.h"

#include <algorithm>
#include <cmath>

namespace eido
{

void Plate::SetTable( std::shared_ptr< const modes::Table > newTable )
{
	table = std::move( newTable );
	//The peak grid: the square's kPeakGrid^2 points over [ -1, 1 ]^2, or the
	//disc's that lie inside it plus as many on the rim.
	peakGridW.clear();
	if( table )
	{
		std::vector< std::pair< double, double > > points;
		const bool circle = table->shape == Shape::Circle;
		for( int j = 0; j < kPeakGrid; ++j )
			for( int i = 0; i < kPeakGrid; ++i )
			{
				const double x = -1.0 + 2.0 * i / ( kPeakGrid - 1 ), y = -1.0 + 2.0 * j / ( kPeakGrid - 1 );
				if( !circle || x * x + y * y <= 1.0 )
					points.emplace_back( x, y );
			}
		if( circle )
			for( int i = 0; i < 4 * kPeakGrid; ++i )
				points.emplace_back( std::cos( 2.0 * kPi * i / ( 4 * kPeakGrid ) ), std::sin( 2.0 * kPi * i / ( 4 * kPeakGrid ) ) );
		peakPoints = static_cast< int >( points.size() );
		peakGridW.reserve( table->modes.size() * points.size() );
		for( const modes::Mode& mode : table->modes )
			for( const auto& p : points )
				peakGridW.push_back( table->W( mode, p.first, p.second ) );
	}
	recompute();
	Reset();
}

double Plate::PeakPerNewton( double omegaD ) const
{
	if( omegaD <= 0.0 || peakPoints == 0 )
		return 0.0;
	std::vector< Complex > a( static_cast< size_t >( Count() ) );
	for( int k = 0; k < Count(); ++k )
		if( omega[ static_cast< size_t >( k ) ] == 0.0 || !( hooks & kHookPeakRigidOnly ) )
			a[ static_cast< size_t >( k ) ] = -omegaD * omegaD * SteadyAmplitude( k, 1.0, omegaD );//rigid: -w^2 x f/( -w^2 ) = f
	double best = 0.0;
	for( int p = 0; p < peakPoints; ++p )
	{
		Complex sum( 0.0, 0.0 );
		for( int k = 0; k < Count(); ++k )
			sum += a[ static_cast< size_t >( k ) ] * peakGridW[ static_cast< size_t >( k ) * peakPoints + p ];
		best = std::max( best, std::abs( sum ) );
	}
	return best;
}

void Plate::Configure( const PlateConfig& next )
{
	const bool same = configured && next.halfSize == config.halfSize && next.speed == config.speed && next.mass == config.mass
	                  && next.q == config.q && next.driveAt == config.driveAt;
	config     = next;
	configured = true;
	if( !same )
		recompute();
}

void Plate::recompute()
{
	const int n = table ? static_cast< int >( table->modes.size() ) : 0;
	omega.assign( static_cast< size_t >( n ), 0.0 );
	coupling.assign( static_cast< size_t >( n ), 0.0 );
	q.resize( static_cast< size_t >( n ), 0.0 );
	v.resize( static_cast< size_t >( n ), 0.0 );
	accel.resize( static_cast< size_t >( n ), Complex( 0.0, 0.0 ) );
	analytic.resize( static_cast< size_t >( n ), Complex( 0.0, 0.0 ) );
	zeta = 0.5 / std::max( config.q, 1.0 );
	const double scale = config.speed / ( config.halfSize * config.halfSize );
	for( int k = 0; k < n; ++k )
	{
		const modes::Mode& mode = table->modes[ static_cast< size_t >( k ) ];
		omega[ static_cast< size_t >( k ) ]    = mode.Lambda * scale;
		coupling[ static_cast< size_t >( k ) ] = table->W( mode, config.driveAt, 0.0 );
	}
}

void Plate::Reset()
{
	std::fill( q.begin(), q.end(), 0.0 );
	std::fill( v.begin(), v.end(), 0.0 );
	std::fill( accel.begin(), accel.end(), Complex( 0.0, 0.0 ) );
	std::fill( analytic.begin(), analytic.end(), Complex( 0.0, 0.0 ) );
	phase = 0.0;
}

Plate::Complex Plate::SteadyAmplitude( int k, double F0, double omegaD ) const
{
	const double w  = omega[ static_cast< size_t >( k ) ];
	const double W  = ( hooks & kHookCouplingOne ) ? 1.0 : coupling[ static_cast< size_t >( k ) ];
	const double f  = F0 * W / config.mass;
	if( w == 0.0 && ( hooks & kHookDropRigid ) )
		return Complex( 0.0, 0.0 );
	const double zw = ( hooks & kHookDampingAtDrive ) ? zeta * omegaD : zeta * w;
	return f / Complex( w * w - omegaD * omegaD, 2.0 * zw * omegaD );
}

void Plate::Advance( double dt, double F0, double omegaD )
{
	lastF0    = F0;
	lastOmega = omegaD;
	const double endPhase = phase + omegaD * dt;
	const Complex driveEnd = std::polar( 1.0, endPhase );
	const Complex driveStart = std::polar( 1.0, phase );
	for( int k = 0; k < Count(); ++k )
	{
		const size_t i  = static_cast< size_t >( k );
		const double w  = omega[ i ];
		const double W  = ( hooks & kHookCouplingOne ) ? 1.0 : coupling[ i ];
		const double f  = F0 * W / config.mass;
		if( w == 0.0 )
		{
			//Rigid: steady state, acceleration f e^{i phi}; displacement -f/w_d^2 e^{i phi}.
			const Complex a = ( hooks & kHookDropRigid ) ? Complex( 0.0, 0.0 ) : f * driveEnd;
			accel[ i ]      = a;
			analytic[ i ]   = omegaD > 0.0 ? -a / ( omegaD * omegaD ) : Complex( 0.0, 0.0 );
			q[ i ]          = analytic[ i ].real();
			v[ i ]          = ( Complex( 0.0, omegaD ) * analytic[ i ] ).real();
			continue;
		}
		//Damping rate: zeta w_k (the negative control puts w_d there).
		const double zw = ( hooks & kHookDampingAtDrive ) ? zeta * omegaD : zeta * w;
		const double zetaK = zw / w;
		const double wd    = w * std::sqrt( std::max( 1.0 - zetaK * zetaK, 1e-12 ) );
		const Complex G    = f / Complex( w * w - omegaD * omegaD, 2.0 * zw * omegaD );
		//The free part at the frame's start.
		const Complex zp0 = G * driveStart;
		const double h0   = q[ i ] - zp0.real();
		const double hd0  = v[ i ] - ( Complex( 0.0, omegaD ) * zp0 ).real();
		const double decay = std::exp( -zw * dt );
		const double c = std::cos( wd * dt ), s = std::sin( wd * dt );
		const double h  = decay * ( h0 * c + ( hd0 + zw * h0 ) / wd * s );
		const double hd = decay * ( hd0 * c - ( w * w * h0 + zw * hd0 ) / wd * s );
		const Complex zp = G * driveEnd;
		q[ i ]           = zp.real() + h;
		v[ i ]           = ( Complex( 0.0, omegaD ) * zp ).real() + hd;
		//Analytic signals at the end: z_h = h - i( h' + zeta w h )/w_dk, s = -zeta w + i w_dk.
		const Complex zh = Complex( h, -( hd + zw * h ) / wd );
		const Complex sk( -zw, wd );
		analytic[ i ] = zp + zh;
		accel[ i ]    = -omegaD * omegaD * zp + sk * sk * zh;
	}
	//Keep the phase small: the drive is periodic in it.
	phase = std::fmod( endPhase, 2.0 * kPi );
}

} // namespace eido
