#include "Eigen.h"

#include <algorithm>
#include <cmath>

namespace eido::eigen
{
namespace
{
/// Householder tridiagonalisation (EISPACK tred2, JAMA's arrangement).
/// V holds A on entry and the accumulated transformation on exit; d, e the
/// diagonal and the subdiagonal.
void tred2( int n, std::vector< double >& V, std::vector< double >& d, std::vector< double >& e )
{
	auto at = [ & ]( int r, int c ) -> double& { return V[ static_cast< size_t >( r ) * n + c ]; };
	for( int j = 0; j < n; ++j )
		d[ j ] = at( n - 1, j );
	for( int i = n - 1; i > 0; --i )
	{
		double scale = 0.0, h = 0.0;
		for( int k = 0; k < i; ++k )
			scale += std::fabs( d[ k ] );
		if( scale == 0.0 )
		{
			e[ i ] = d[ i - 1 ];
			for( int j = 0; j < i; ++j )
			{
				d[ j ]      = at( i - 1, j );
				at( i, j ) = 0.0;
				at( j, i ) = 0.0;
			}
		}
		else
		{
			for( int k = 0; k < i; ++k )
			{
				d[ k ] /= scale;
				h += d[ k ] * d[ k ];
			}
			double f = d[ i - 1 ];
			double g = std::sqrt( h );
			if( f > 0.0 )
				g = -g;
			e[ i ]     = scale * g;
			h          = h - f * g;
			d[ i - 1 ] = f - g;
			for( int j = 0; j < i; ++j )
				e[ j ] = 0.0;
			for( int j = 0; j < i; ++j )
			{
				f          = d[ j ];
				at( j, i ) = f;
				g          = e[ j ] + at( j, j ) * f;
				for( int k = j + 1; k <= i - 1; ++k )
				{
					g += at( k, j ) * d[ k ];
					e[ k ] += at( k, j ) * f;
				}
				e[ j ] = g;
			}
			f = 0.0;
			for( int j = 0; j < i; ++j )
			{
				e[ j ] /= h;
				f += e[ j ] * d[ j ];
			}
			const double hh = f / ( h + h );
			for( int j = 0; j < i; ++j )
				e[ j ] -= hh * d[ j ];
			for( int j = 0; j < i; ++j )
			{
				f = d[ j ];
				g = e[ j ];
				for( int k = j; k <= i - 1; ++k )
					at( k, j ) -= ( f * e[ k ] + g * d[ k ] );
				d[ j ]     = at( i - 1, j );
				at( i, j ) = 0.0;
			}
		}
		d[ i ] = h;
	}
	for( int i = 0; i < n - 1; ++i )
	{
		at( n - 1, i ) = at( i, i );
		at( i, i )     = 1.0;
		const double h = d[ i + 1 ];
		if( h != 0.0 )
		{
			for( int k = 0; k <= i; ++k )
				d[ k ] = at( k, i + 1 ) / h;
			for( int j = 0; j <= i; ++j )
			{
				double g = 0.0;
				for( int k = 0; k <= i; ++k )
					g += at( k, i + 1 ) * at( k, j );
				for( int k = 0; k <= i; ++k )
					at( k, j ) -= g * d[ k ];
			}
		}
		for( int k = 0; k <= i; ++k )
			at( k, i + 1 ) = 0.0;
	}
	for( int j = 0; j < n; ++j )
	{
		d[ j ]         = at( n - 1, j );
		at( n - 1, j ) = 0.0;
	}
	at( n - 1, n - 1 ) = 1.0;
	e[ 0 ]             = 0.0;
}

/// Implicit QL on the tridiagonal (EISPACK tql2), then an ascending sort.
void tql2( int n, std::vector< double >& V, std::vector< double >& d, std::vector< double >& e )
{
	auto at = [ & ]( int r, int c ) -> double& { return V[ static_cast< size_t >( r ) * n + c ]; };
	for( int i = 1; i < n; ++i )
		e[ i - 1 ] = e[ i ];
	e[ n - 1 ] = 0.0;
	double f = 0.0, tst1 = 0.0;
	const double eps = std::ldexp( 1.0, -52 );
	for( int l = 0; l < n; ++l )
	{
		tst1  = std::max( tst1, std::fabs( d[ l ] ) + std::fabs( e[ l ] ) );
		int m = l;
		while( m < n )
		{
			if( std::fabs( e[ m ] ) <= eps * tst1 )
				break;
			++m;
		}
		if( m >= n )
			m = n - 1;
		if( m > l )
		{
			int iterations = 0;
			do
			{
				++iterations;
				double g = d[ l ];
				double p = ( d[ l + 1 ] - g ) / ( 2.0 * e[ l ] );
				double r = std::hypot( p, 1.0 );
				if( p < 0.0 )
					r = -r;
				d[ l ]           = e[ l ] / ( p + r );
				d[ l + 1 ]       = e[ l ] * ( p + r );
				const double dl1 = d[ l + 1 ];
				double h         = g - d[ l ];
				for( int i = l + 2; i < n; ++i )
					d[ i ] -= h;
				f += h;
				p                = d[ m ];
				double c = 1.0, c2 = 1.0, c3 = 1.0;
				const double el1 = e[ l + 1 ];
				double s = 0.0, s2 = 0.0;
				for( int i = m - 1; i >= l; --i )
				{
					c3         = c2;
					c2         = c;
					s2         = s;
					g          = c * e[ i ];
					h          = c * p;
					r          = std::hypot( p, e[ i ] );
					e[ i + 1 ] = s * r;
					s          = e[ i ] / r;
					c          = p / r;
					p          = c * d[ i ] - s * g;
					d[ i + 1 ] = h + s * ( c * g + s * d[ i ] );
					for( int k = 0; k < n; ++k )
					{
						h              = at( k, i + 1 );
						at( k, i + 1 ) = s * at( k, i ) + c * h;
						at( k, i )     = c * at( k, i ) - s * h;
					}
				}
				p      = -s * s2 * c3 * el1 * e[ l ] / dl1;
				e[ l ] = s * p;
				d[ l ] = c * p;
			} while( std::fabs( e[ l ] ) > eps * tst1 && iterations < 100 );
		}
		d[ l ] = d[ l ] + f;
		e[ l ] = 0.0;
	}
	for( int i = 0; i < n - 1; ++i )
	{
		int k    = i;
		double p = d[ i ];
		for( int j = i + 1; j < n; ++j )
			if( d[ j ] < p )
			{
				k = j;
				p = d[ j ];
			}
		if( k != i )
		{
			d[ k ] = d[ i ];
			d[ i ] = p;
			for( int j = 0; j < n; ++j )
				std::swap( at( j, i ), at( j, k ) );
		}
	}
}
} // namespace

void Symmetric( const std::vector< double >& a, int n, std::vector< double >& values, std::vector< double >& vectors )
{
	vectors = a;
	values.assign( static_cast< size_t >( n ), 0.0 );
	if( n == 0 )
		return;
	std::vector< double > e( static_cast< size_t >( n ), 0.0 );
	tred2( n, vectors, values, e );
	tql2( n, vectors, values, e );
}

bool Generalised( const std::vector< double >& k, const std::vector< double >& m, int n, std::vector< double >& values )
{
	//Cholesky: m = L L^T, L lower, row-major.
	std::vector< double > L( static_cast< size_t >( n ) * n, 0.0 );
	for( int i = 0; i < n; ++i )
		for( int j = 0; j <= i; ++j )
		{
			double sum = m[ static_cast< size_t >( i ) * n + j ];
			for( int p = 0; p < j; ++p )
				sum -= L[ static_cast< size_t >( i ) * n + p ] * L[ static_cast< size_t >( j ) * n + p ];
			if( i == j )
			{
				if( !( sum > 0.0 ) )
					return false;
				L[ static_cast< size_t >( i ) * n + i ] = std::sqrt( sum );
			}
			else
				L[ static_cast< size_t >( i ) * n + j ] = sum / L[ static_cast< size_t >( j ) * n + j ];
		}
	//Y = L^-1 K (forward substitution, column by column), then C = Y L^-T.
	std::vector< double > Y( static_cast< size_t >( n ) * n ), C( static_cast< size_t >( n ) * n );
	for( int c = 0; c < n; ++c )
		for( int i = 0; i < n; ++i )
		{
			double sum = k[ static_cast< size_t >( i ) * n + c ];
			for( int p = 0; p < i; ++p )
				sum -= L[ static_cast< size_t >( i ) * n + p ] * Y[ static_cast< size_t >( p ) * n + c ];
			Y[ static_cast< size_t >( i ) * n + c ] = sum / L[ static_cast< size_t >( i ) * n + i ];
		}
	//C = Y L^-T: row r of C solves L c^T = y^T for each row.
	for( int r = 0; r < n; ++r )
		for( int i = 0; i < n; ++i )
		{
			double sum = Y[ static_cast< size_t >( r ) * n + i ];
			for( int p = 0; p < i; ++p )
				sum -= L[ static_cast< size_t >( i ) * n + p ] * C[ static_cast< size_t >( r ) * n + p ];
			C[ static_cast< size_t >( r ) * n + i ] = sum / L[ static_cast< size_t >( i ) * n + i ];
		}
	//Symmetrise against rounding.
	for( int i = 0; i < n; ++i )
		for( int j = 0; j < i; ++j )
		{
			const double s = 0.5 * ( C[ static_cast< size_t >( i ) * n + j ] + C[ static_cast< size_t >( j ) * n + i ] );
			C[ static_cast< size_t >( i ) * n + j ] = C[ static_cast< size_t >( j ) * n + i ] = s;
		}
	std::vector< double > vectors;
	Symmetric( C, n, values, vectors );
	return true;
}

} // namespace eido::eigen
