#include "Modes.h"

#include "Eigen.h"

#include <algorithm>
#include <cmath>

namespace eido::modes
{
namespace
{
constexpr double kRigidCut = 1e-3;///< R (square) below this is a rigid-body mode
constexpr double kOuterRadius = 1.05;///< circle: the radial table's reach (the lattice is zero beyond)
constexpr int kRadialPoints = 2048;

/// Gauss-Legendre nodes and weights on ( -1, 1 ), Newton on P_n.
void gaussLegendre( int n, std::vector< double >& x, std::vector< double >& w )
{
	x.assign( static_cast< size_t >( n ), 0.0 );
	w.assign( static_cast< size_t >( n ), 0.0 );
	for( int i = 0; i < n; ++i )
	{
		double z  = std::cos( kPi * ( i + 0.75 ) / ( n + 0.5 ) );
		double dp = 1.0;
		for( int it = 0; it < 100; ++it )
		{
			double p0 = 1.0, p1 = z;
			for( int k = 1; k < n; ++k )
			{
				const double p2 = ( ( 2.0 * k + 1.0 ) * z * p1 - k * p0 ) / ( k + 1.0 );
				p0              = p1;
				p1              = p2;
			}
			dp              = n * ( z * p1 - p0 ) / ( z * z - 1.0 );
			const double dz = p1 / dp;
			z -= dz;
			if( std::fabs( dz ) < 1e-16 )
				break;
		}
		x[ static_cast< size_t >( n - 1 - i ) ] = z;
		w[ static_cast< size_t >( n - 1 - i ) ] = 2.0 / ( ( 1.0 - z * z ) * dp * dp );
	}
}

/// Unnormalised Legendre P_k( t ) and two derivatives, k = 0..K-1, by the
/// three-term recurrence and P'_{k+1} = P'_{k-1} + ( 2k + 1 ) P_k (no 1/(1-t^2)).
void legendreRaw( int K, double t, double* p, double* dp, double* d2p )
{
	if( K <= 0 )
		return;
	p[ 0 ] = 1.0;
	if( dp )
		dp[ 0 ] = 0.0;
	if( d2p )
		d2p[ 0 ] = 0.0;
	if( K == 1 )
		return;
	p[ 1 ] = t;
	if( dp )
		dp[ 1 ] = 1.0;
	if( d2p )
		d2p[ 1 ] = 0.0;
	for( int k = 1; k + 1 < K; ++k )
	{
		p[ k + 1 ] = ( ( 2.0 * k + 1.0 ) * t * p[ k ] - k * p[ k - 1 ] ) / ( k + 1.0 );
		if( dp )
			dp[ k + 1 ] = dp[ k - 1 ] + ( 2.0 * k + 1.0 ) * p[ k ];
		if( d2p )
			d2p[ k + 1 ] = d2p[ k - 1 ] + ( 2.0 * k + 1.0 ) * dp[ k ];
	}
}

//---------------------------------------------------------------------------
// The square.
//---------------------------------------------------------------------------

/// A = int P~'' P~'', C = int P~'' P~, E = int P~' P~', by Gauss-Legendre with
/// N + 4 points (exact: the integrands are of degree at most 2N - 2).
struct SquareMatrices
{
	int N = 0;
	std::vector< double > A, C, E;
};

SquareMatrices squareMatrices( int N )
{
	SquareMatrices m;
	m.N = N;
	std::vector< double > xq, wq;
	gaussLegendre( N + 4, xq, wq );
	const size_t nn = static_cast< size_t >( N ) * N;
	m.A.assign( nn, 0.0 );
	m.C.assign( nn, 0.0 );
	m.E.assign( nn, 0.0 );
	std::vector< double > p( N ), dp( N ), d2p( N );
	for( size_t q = 0; q < xq.size(); ++q )
	{
		Legendre( N, xq[ q ], p.data(), dp.data(), d2p.data() );
		for( int a = 0; a < N; ++a )
			for( int b = 0; b < N; ++b )
			{
				const size_t ab = static_cast< size_t >( a ) * N + b;
				m.A[ ab ] += wq[ q ] * d2p[ a ] * d2p[ b ];
				m.C[ ab ] += wq[ q ] * d2p[ a ] * p[ b ];
				m.E[ ab ] += wq[ q ] * dp[ a ] * dp[ b ];
			}
	}
	return m;
}

/// K[ ( i, j ), ( k, l ) ] of SPEC-eidophone.md: A x I + I x A + nu( C x C^T + C^T x C ) + 2( 1 - nu ) E x E.
double kEntry( const SquareMatrices& m, double nu, const Hooks& hooks, int i, int j, int k, int l )
{
	const int N  = m.N;
	auto at      = [ N ]( const std::vector< double >& M, int r, int c ) { return M[ static_cast< size_t >( r ) * N + c ]; };
	double value = 0.0;
	if( j == l )
		value += at( m.A, i, k );
	if( i == k )
		value += at( m.A, j, l );
	if( !hooks.dropNu )
		value += nu * ( at( m.C, i, k ) * at( m.C, l, j ) + at( m.C, k, i ) * at( m.C, j, l ) );
	const double twist = hooks.twistHalf ? ( 1.0 - nu ) : 2.0 * ( 1.0 - nu );
	value += twist * at( m.E, i, k ) * at( m.E, j, l );
	return value;
}

struct Term
{
	int i, j;
	double w;
};

/// The reduced basis of one symmetry class: each vector a combination of one
/// or two tensor products, orthonormal.
std::vector< std::vector< Term > > classBasis( int N, int cls )
{
	std::vector< std::vector< Term > > basis;
	const double r = 1.0 / std::sqrt( 2.0 );
	auto paired    = [ & ]( int parity, int sign ) {
		for( int i = parity; i < N; i += 2 )
			for( int j = i; j < N; j += 2 )
			{
				if( i == j )
				{
					if( sign > 0 )
						basis.push_back( { { i, i, 1.0 } } );
				}
				else
					basis.push_back( { { i, j, r }, { j, i, sign * r } } );
			}
	};
	switch( cls )
	{
		case kSS: paired( 0, 1 ); break;
		case kSA: paired( 0, -1 ); break;
		case kOS: paired( 1, 1 ); break;
		case kOA: paired( 1, -1 ); break;
		case kOE:
			for( int i = 1; i < N; i += 2 )
				for( int j = 0; j < N; j += 2 )
					basis.push_back( { { i, j, 1.0 } } );
			break;
		default: break;
	}
	return basis;
}

/// Eigenvalues R (ascending) of one class, and the full N x N coefficient
/// vectors (unit norm in the orthonormal basis) if asked.
void solveClass( const SquareMatrices& m, double nu, int cls, const Hooks& hooks, std::vector< double >& values, std::vector< std::vector< double > >* modes )
{
	const auto basis = classBasis( m.N, cls );
	const int n      = static_cast< int >( basis.size() );
	std::vector< double > K( static_cast< size_t >( n ) * n, 0.0 );
	for( int p = 0; p < n; ++p )
		for( int q = p; q < n; ++q )
		{
			double s = 0.0;
			for( const Term& a : basis[ static_cast< size_t >( p ) ] )
				for( const Term& b : basis[ static_cast< size_t >( q ) ] )
					s += a.w * b.w * kEntry( m, nu, hooks, a.i, a.j, b.i, b.j );
			K[ static_cast< size_t >( p ) * n + q ] = K[ static_cast< size_t >( q ) * n + p ] = s;
		}
	std::vector< double > vectors;
	eigen::Symmetric( K, n, values, vectors );
	if( !modes )
		return;
	modes->clear();
	const int N = m.N;
	for( int e = 0; e < n; ++e )
	{
		std::vector< double > coef( static_cast< size_t >( N ) * N, 0.0 );
		for( int p = 0; p < n; ++p )
		{
			const double v = vectors[ static_cast< size_t >( p ) * n + e ];
			for( const Term& t : basis[ static_cast< size_t >( p ) ] )
				coef[ static_cast< size_t >( t.i ) * N + t.j ] += t.w * v;
		}
		//RMS 1 over the square: sum c^2 = 4. Sign: the largest coefficient positive.
		double norm = 0.0, big = 0.0;
		for( double c : coef )
		{
			norm += c * c;
			if( std::fabs( c ) > std::fabs( big ) )
				big = c;
		}
		const double scale = ( big < 0.0 ? -2.0 : 2.0 ) / std::sqrt( norm );
		for( double& c : coef )
			c *= scale;
		modes->push_back( std::move( coef ) );
	}
}

//---------------------------------------------------------------------------
// The circle.
//---------------------------------------------------------------------------

/// J_n by its power series where that is safe (( x/2 )^2 < n + 1: the terms
/// fall from the first, so nothing cancels), else by the trapezoid rule over
/// a period of Bessel's integral (1/pi) int_0^pi cos( n t - x sin t ) dt,
/// which converges exponentially once the point count passes x + n.
double besselJPositive( int n, double x )
{
	if( x == 0.0 )
		return n == 0 ? 1.0 : 0.0;
	const double h = 0.5 * x;
	if( h * h < n + 1.0 )
	{
		double term = 1.0;
		for( int k = 1; k <= n; ++k )
			term *= h / k;
		double sum = term;
		for( int k = 1; k < 200; ++k )
		{
			term *= -h * h / ( static_cast< double >( k ) * ( k + n ) );
			sum += term;
			if( std::fabs( term ) < 1e-17 * std::fabs( sum ) )
				break;
		}
		return sum;
	}
	const int M = 48 + static_cast< int >( 1.5 * ( std::fabs( x ) + n ) );
	double sum  = 0.0;
	for( int k = 0; k <= M; ++k )
	{
		const double t = kPi * k / M;
		const double f = std::cos( n * t - x * std::sin( t ) );
		sum += ( k == 0 || k == M ) ? 0.5 * f : f;
	}
	return sum / M;
}

/// a1, b2: the moment condition's J and I brackets; a2, b1: the shear's.
struct Brackets
{
	double a1, a2, b1, b2, I;
};

Brackets brackets( int n, double l, double nu, bool flip )
{
	const double J = BesselJ( n, l ), Jp = 0.5 * ( BesselJ( n - 1, l ) - BesselJ( n + 1, l ) );
	const double I = BesselI( n, l ), Ip = 0.5 * ( BesselI( n - 1, l ) + BesselI( n + 1, l ) );
	const double g  = 1.0 - nu;
	const double gm = flip ? -g : g;//the moment condition's ( 1 - nu ), for the negative control
	const double n2 = static_cast< double >( n ) * n;
	Brackets b;
	b.a1 = l * l * J + gm * ( l * Jp - n2 * J );
	b.b2 = l * l * I - gm * ( l * Ip - n2 * I );
	b.a2 = l * l * l * Jp + g * n2 * ( l * Jp - J );
	b.b1 = l * l * l * Ip - g * n2 * ( l * Ip - I );
	b.I  = I;
	return b;
}

/// The unnormalised radial function J_n( lr ) + C I_n( lr ) and its r-derivative.
double radialRaw( int n, double l, double C, double r, double* derivative )
{
	const double x = l * r;
	const double R = BesselJ( n, x ) + C * BesselI( n, x );
	if( derivative )
		*derivative = l * ( 0.5 * ( BesselJ( n - 1, x ) - BesselJ( n + 1, x ) ) + C * 0.5 * ( BesselI( n - 1, x ) + BesselI( n + 1, x ) ) );
	return R;
}
} // namespace

void Legendre( int N, double x, double* p, double* dp, double* d2p )
{
	legendreRaw( N, x, p, dp, d2p );
	for( int k = 0; k < N; ++k )
	{
		const double s = std::sqrt( ( 2.0 * k + 1.0 ) / 2.0 );
		p[ k ] *= s;
		if( dp )
			dp[ k ] *= s;
		if( d2p )
			d2p[ k ] *= s;
	}
}

double BesselJ( int n, double x )
{
	//J_-n = ( -1 )^n J_n; J_n( -x ) = ( -1 )^n J_n( x ).
	double sign = 1.0;
	if( n < 0 )
	{
		n = -n;
		if( n & 1 )
			sign = -sign;
	}
	if( x < 0.0 )
	{
		x = -x;
		if( n & 1 )
			sign = -sign;
	}
	return sign * besselJPositive( n, x );
}

double BesselI( int n, double x )
{
	//I_-n = I_n. The power series has no cancellation for x >= 0 (every term
	//positive), so it is used everywhere it is needed here (x < 80).
	n = std::abs( n );
	double sign = 1.0;
	if( x < 0.0 )
	{
		x = -x;
		if( n & 1 )
			sign = -1.0;
	}
	if( x == 0.0 )
		return n == 0 ? 1.0 : 0.0;
	const double h = 0.5 * x;
	double term    = 1.0;
	for( int k = 1; k <= n; ++k )
		term *= h / k;
	double sum = term;
	for( int k = 1; k < 400; ++k )
	{
		term *= h * h / ( static_cast< double >( k ) * ( k + n ) );
		sum += term;
		if( term < 1e-17 * sum )
			break;
	}
	return sign * sum;
}

double FrequencyEquation( int n, double lambda, double nu, bool flip )
{
	const Brackets b = brackets( n, lambda, nu, flip );
	//Divided by I_n( lambda ) lambda^3 > 0: the same roots, bounded values.
	return ( b.a1 * b.b1 - b.a2 * b.b2 ) / ( b.I * lambda * lambda * lambda );
}

std::vector< double > CircleRoots( int n, double nu, double lambdaMax, bool flip )
{
	std::vector< double > roots;
	//n = 0 and 1 have their rigid modes at lambda = 0; start clear of it. For
	//larger n the first root is near n (an edge wave), so start at n/2.
	const double start = std::max( 0.3, 0.5 * n );
	const double step  = 0.05;
	double a = start, fa = FrequencyEquation( n, a, nu, flip );
	while( a < lambdaMax )
	{
		const double b = std::min( a + step, lambdaMax ), fb = FrequencyEquation( n, b, nu, flip );
		if( ( fa < 0.0 ) != ( fb < 0.0 ) )
		{
			double lo = a, hi = b, flo = fa;
			for( int it = 0; it < 200 && hi - lo > 1e-15 * hi; ++it )
			{
				const double mid = 0.5 * ( lo + hi ), fm = FrequencyEquation( n, mid, nu, flip );
				if( ( fm < 0.0 ) == ( flo < 0.0 ) )
				{
					lo  = mid;
					flo = fm;
				}
				else
					hi = mid;
			}
			roots.push_back( 0.5 * ( lo + hi ) );
		}
		a  = b;
		fa = fb;
		if( b >= lambdaMax )
			break;
	}
	return roots;
}

std::vector< double > PolarRitz( int n, double nu, int K )
{
	//Weak form in r on ( 0, 1 ), basis f_k = r^n P_k( 2r^2 - 1 ):
	//K_ij = int [ lap_i lap_j - ( 1 - nu )( f''_i a_j + a_i f''_j ) + 2( 1 - nu ) n^2 b_i b_j ] r dr,
	//M_ij = int f_i f_j r dr, lap = f'' + f'/r - n^2 f/r^2, a = f'/r - n^2 f/r^2, b = f'/r - f/r^2.
	//The strain energy of w = f( r ) cos n theta, divided by pi (n >= 1) or 2 pi (n = 0) on both sides.
	std::vector< double > xq, wq;
	gaussLegendre( 240, xq, wq );
	const size_t kk = static_cast< size_t >( K ) * K;
	std::vector< double > Km( kk, 0.0 ), Mm( kk, 0.0 );
	std::vector< double > P( K ), dP( K ), d2P( K ), f( K ), f1( K ), f2( K ), lap( K ), av( K ), bv( K );
	const double g  = 1.0 - nu;
	const double n2 = static_cast< double >( n ) * n;
	for( size_t q = 0; q < xq.size(); ++q )
	{
		const double r = 0.5 * ( xq[ q ] + 1.0 ), w = 0.5 * wq[ q ] * r;
		legendreRaw( K, 2.0 * r * r - 1.0, P.data(), dP.data(), d2P.data() );
		const double rn   = std::pow( r, n );
		const double rn1  = n >= 1 ? std::pow( r, n - 1 ) : 0.0;
		const double rn2  = n >= 2 ? std::pow( r, n - 2 ) : 0.0;
		for( int k = 0; k < K; ++k )
		{
			//f = r^n P( t ), t = 2r^2 - 1, dt/dr = 4r.
			f[ k ]  = rn * P[ k ];
			f1[ k ] = n * rn1 * P[ k ] + 4.0 * rn * r * dP[ k ];
			f2[ k ] = n * ( n - 1 ) * rn2 * P[ k ] + ( 8.0 * n + 4.0 ) * rn * dP[ k ] + 16.0 * rn * r * r * d2P[ k ];
			//f'/r and f/r^2 without dividing a small r where the factor is analytic.
			const double f1r = n * rn2 * ( n >= 2 ? 1.0 : 0.0 ) * P[ k ] + ( n == 1 ? P[ k ] / r : 0.0 ) + 4.0 * rn * dP[ k ];
			const double fr2 = n >= 2 ? rn2 * P[ k ] : f[ k ] / ( r * r );
			lap[ k ]          = f2[ k ] + f1r - n2 * fr2;
			av[ k ]           = f1r - n2 * fr2;
			bv[ k ]           = f1r - fr2;
		}
		for( int i = 0; i < K; ++i )
			for( int j = 0; j < K; ++j )
			{
				const size_t ij = static_cast< size_t >( i ) * K + j;
				Km[ ij ] += w * ( lap[ i ] * lap[ j ] - g * ( f2[ i ] * av[ j ] + av[ i ] * f2[ j ] ) + ( n > 0 ? 2.0 * g * n2 * bv[ i ] * bv[ j ] : 0.0 ) );
				Mm[ ij ] += w * f[ i ] * f[ j ];
			}
	}
	std::vector< double > values;
	if( !eigen::Generalised( Km, Mm, K, values ) )
		return {};
	for( double& v : values )
		v = std::sqrt( std::sqrt( std::max( v, 0.0 ) ) );//omega^2 a^4 rho h / D = lambda^4
	for( double& v : values )
		v = v * v;//lambda^2
	return values;
}

std::vector< double > SquareClass( double nu, int N, int cls, const Hooks& hooks, std::vector< std::vector< double > >* modes )
{
	const SquareMatrices m = squareMatrices( N );
	std::vector< double > values;
	solveClass( m, nu, cls, hooks, values, modes );
	for( double& v : values )
		v = 4.0 * std::sqrt( std::max( v, 0.0 ) );
	return values;
}

double RayleighOfXY( double nu, int N, const Hooks& hooks )
{
	//w = x y = ( 2/3 ) P~_1( x ) P~_1( y ): one basis vector, so the quotient is K's diagonal entry.
	const SquareMatrices m = squareMatrices( N );
	return kEntry( m, nu, hooks, 1, 1, 1, 1 );
}

Table BuildSquare( double nu, int N, int count, const Hooks& hooks )
{
	Table table;
	table.shape            = Shape::Square;
	table.nu               = nu;
	table.N                = N;
	const SquareMatrices m = squareMatrices( N );
	std::vector< Mode > elastic;
	for( int cls : { kSS, kSA, kOE } )
	{
		std::vector< double > values;
		std::vector< std::vector< double > > vectors;
		solveClass( m, nu, cls, hooks, values, &vectors );
		for( size_t e = 0; e < values.size(); ++e )
		{
			if( values[ e ] < kRigidCut )
				continue;
			Mode mode;
			mode.Lambda = std::sqrt( values[ e ] );
			mode.paper  = 4.0 * mode.Lambda;
			mode.cls    = cls;
			mode.coef   = std::move( vectors[ e ] );
			elastic.push_back( std::move( mode ) );
		}
	}
	std::sort( elastic.begin(), elastic.end(), []( const Mode& a, const Mode& b ) { return a.Lambda < b.Lambda; } );
	if( static_cast< int >( elastic.size() ) > count )
		elastic.resize( static_cast< size_t >( count ) );

	//The rigid modes, exact: W = 1 (c_00 = 2) and W = sqrt 3 x (c_10 = 2).
	for( int tilt = 0; tilt < 2; ++tilt )
	{
		Mode mode;
		mode.cls  = kRigid;
		mode.n    = tilt;
		mode.coef.assign( static_cast< size_t >( N ) * N, 0.0 );
		mode.coef[ tilt ? static_cast< size_t >( N ) : 0 ] = 2.0;
		table.modes.push_back( std::move( mode ) );
	}
	for( Mode& mode : elastic )
		table.modes.push_back( std::move( mode ) );
	return table;
}

Table BuildCircle( double nu, int count, const Hooks& hooks )
{
	Table table;
	table.shape = Shape::Circle;
	table.nu    = nu;
	std::vector< Mode > elastic;
	//Chladni's law, lambda ~ ( pi/2 )( n + 2s ), puts the 64th cos-mode near
	//lambda = 26; collect everything below 45 and keep the lowest.
	const double lambdaMax = 45.0;
	for( int n = 0; n < 60; ++n )
	{
		const auto roots = CircleRoots( n, nu, lambdaMax, hooks.momentSignFlip );
		if( roots.empty() && n > 4 )
			break;
		for( size_t s = 0; s < roots.size(); ++s )
		{
			Mode mode;
			mode.cls       = kCircle;
			mode.n         = n;
			mode.s         = static_cast< int >( s ) + ( n < 2 ? 1 : 0 );
			mode.root      = roots[ s ];
			mode.Lambda    = roots[ s ] * roots[ s ];
			mode.paper     = mode.Lambda;
			const Brackets b = brackets( n, roots[ s ], nu, hooks.momentSignFlip );
			mode.C         = b.a1 / b.b2;
			elastic.push_back( std::move( mode ) );
		}
	}
	std::sort( elastic.begin(), elastic.end(), []( const Mode& a, const Mode& b ) { return a.Lambda < b.Lambda; } );
	if( static_cast< int >( elastic.size() ) > count )
		elastic.resize( static_cast< size_t >( count ) );

	//Mean square 1 over the disc: n >= 1, int R^2 r dr = 1 (cos^2 averages to
	//1/2 against the disc's 1/pi); n = 0, 2 int R^2 r dr = 1.
	std::vector< double > xq, wq;
	gaussLegendre( 160, xq, wq );
	for( Mode& mode : elastic )
	{
		double integral = 0.0;
		for( size_t q = 0; q < xq.size(); ++q )
		{
			const double r = 0.5 * ( xq[ q ] + 1.0 );
			const double R = radialRaw( mode.n, mode.root, mode.C, r, nullptr );
			integral += 0.5 * wq[ q ] * R * R * r;
		}
		if( mode.n == 0 )
			integral *= 2.0;
		mode.norm = 1.0 / std::sqrt( integral );
		//Sign: positive at the rim (theta = 0).
		if( radialRaw( mode.n, mode.root, mode.C, 1.0, nullptr ) < 0.0 )
			mode.norm = -mode.norm;
		//The radial table (R and dR/dr at kRadialPoints + 1 points to kOuterRadius) for Sample's Hermite interpolation.
		mode.coef.resize( 2 * ( kRadialPoints + 1 ) );
		for( int i = 0; i <= kRadialPoints; ++i )
		{
			const double r = kOuterRadius * i / kRadialPoints;
			double d       = 0.0;
			const double R = radialRaw( mode.n, mode.root, mode.C, r, &d );
			mode.coef[ static_cast< size_t >( 2 * i ) ]     = mode.norm * R;
			mode.coef[ static_cast< size_t >( 2 * i + 1 ) ] = mode.norm * d;
		}
	}
	//The rigid modes: W = 1 and W = 2 r cos theta = 2x.
	for( int tilt = 0; tilt < 2; ++tilt )
	{
		Mode mode;
		mode.cls = kRigid;
		mode.n   = tilt;
		table.modes.push_back( std::move( mode ) );
	}
	for( Mode& mode : elastic )
		table.modes.push_back( std::move( mode ) );
	return table;
}

double Table::Radial( const Mode& mode, double r, double* derivative ) const
{
	if( mode.cls == kRigid )
	{
		if( derivative )
			*derivative = mode.n == 1 ? 2.0 : 0.0;
		return mode.n == 1 ? 2.0 * r : 1.0;
	}
	double d       = 0.0;
	const double R = radialRaw( mode.n, mode.root, mode.C, r, derivative ? &d : nullptr );
	if( derivative )
		*derivative = mode.norm * d;
	return mode.norm * R;
}

double Table::W( const Mode& mode, double x, double y ) const
{
	if( shape == Shape::Square )
	{
		std::vector< double > px( static_cast< size_t >( N ) ), py( static_cast< size_t >( N ) );
		Legendre( N, x, px.data() );
		Legendre( N, y, py.data() );
		double sum = 0.0;
		for( int i = 0; i < N; ++i )
		{
			double row = 0.0;
			for( int j = 0; j < N; ++j )
				row += mode.coef[ static_cast< size_t >( i ) * N + j ] * py[ static_cast< size_t >( j ) ];
			sum += row * px[ static_cast< size_t >( i ) ];
		}
		return sum;
	}
	if( mode.cls == kRigid )
		return mode.n == 1 ? 2.0 * x : 1.0;
	const double r = std::hypot( x, y );
	return Radial( mode, r ) * std::cos( mode.n * std::atan2( y, x ) );
}

void Table::Gradient( const Mode& mode, double x, double y, double& wx, double& wy ) const
{
	if( shape == Shape::Square )
	{
		std::vector< double > px( static_cast< size_t >( N ) ), py( static_cast< size_t >( N ) ), dx( static_cast< size_t >( N ) ), dy( static_cast< size_t >( N ) );
		Legendre( N, x, px.data(), dx.data() );
		Legendre( N, y, py.data(), dy.data() );
		wx = wy = 0.0;
		for( int i = 0; i < N; ++i )
			for( int j = 0; j < N; ++j )
			{
				const double c = mode.coef[ static_cast< size_t >( i ) * N + j ];
				wx += c * dx[ static_cast< size_t >( i ) ] * py[ static_cast< size_t >( j ) ];
				wy += c * px[ static_cast< size_t >( i ) ] * dy[ static_cast< size_t >( j ) ];
			}
		return;
	}
	if( mode.cls == kRigid )
	{
		wx = mode.n == 1 ? 2.0 : 0.0;
		wy = 0.0;
		return;
	}
	const double r  = std::max( std::hypot( x, y ), 1e-12 );
	const double th = std::atan2( y, x );
	double dR       = 0.0;
	const double R  = Radial( mode, r, &dR );
	const double c = std::cos( mode.n * th ), s = std::sin( mode.n * th );
	wx = dR * ( x / r ) * c + mode.n * R * s * y / ( r * r );
	wy = dR * ( y / r ) * c - mode.n * R * s * x / ( r * r );
}

void Table::Sample( const Mode& mode, int S, float* out ) const
{
	const double h = 2.0 / S;
	if( shape == Shape::Square )
	{
		//Separable: T[ a ][ j ] = sum_i P_i( x_a ) c_ij, then out[ b ][ a ] = sum_j T[ a ][ j ] P_j( y_b ).
		std::vector< double > P( static_cast< size_t >( S ) * N ), T( static_cast< size_t >( S ) * N, 0.0 );
		for( int a = 0; a < S; ++a )
			Legendre( N, -1.0 + ( a + 0.5 ) * h, &P[ static_cast< size_t >( a ) * N ] );
		for( int a = 0; a < S; ++a )
			for( int i = 0; i < N; ++i )
			{
				const double p = P[ static_cast< size_t >( a ) * N + i ];
				const double* c = &mode.coef[ static_cast< size_t >( i ) * N ];
				double* t       = &T[ static_cast< size_t >( a ) * N ];
				for( int j = 0; j < N; ++j )
					if( c[ j ] != 0.0 )
						t[ j ] += p * c[ j ];
			}
		for( int b = 0; b < S; ++b )
			for( int a = 0; a < S; ++a )
			{
				double sum      = 0.0;
				const double* t = &T[ static_cast< size_t >( a ) * N ];
				const double* p = &P[ static_cast< size_t >( b ) * N ];
				for( int j = 0; j < N; ++j )
					sum += t[ j ] * p[ j ];
				out[ static_cast< size_t >( b ) * S + a ] = static_cast< float >( sum );
			}
		return;
	}
	for( int b = 0; b < S; ++b )
		for( int a = 0; a < S; ++a )
		{
			const double x = -1.0 + ( a + 0.5 ) * h, y = -1.0 + ( b + 0.5 ) * h;
			const double r = std::hypot( x, y );
			double value   = 0.0;
			if( mode.cls == kRigid )
				value = mode.n == 1 ? 2.0 * x : 1.0;
			else if( r < kOuterRadius )
			{
				//Cubic Hermite on the radial table (error ~ dr^4 R''''/384, about 1e-10 here).
				const double u = r / kOuterRadius * kRadialPoints;
				const int i    = std::min( static_cast< int >( u ), kRadialPoints - 1 );
				const double t = u - i, dr = kOuterRadius / kRadialPoints;
				const double* c = &mode.coef[ static_cast< size_t >( 2 * i ) ];
				const double p0 = c[ 0 ], m0 = c[ 1 ] * dr, p1 = c[ 2 ], m1 = c[ 3 ] * dr;
				const double t2 = t * t, t3 = t2 * t;
				const double R  = ( 2 * t3 - 3 * t2 + 1 ) * p0 + ( t3 - 2 * t2 + t ) * m0 + ( -2 * t3 + 3 * t2 ) * p1 + ( t3 - t2 ) * m1;
				value           = R * std::cos( mode.n * std::atan2( y, x ) );
			}
			out[ static_cast< size_t >( b ) * S + a ] = static_cast< float >( value );
		}
}

} // namespace eido::modes
