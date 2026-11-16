#pragma once

#include "Controls.h"

#include <vector>

/**
    The plate's modes: shapes and frequencies, in double, on the CPU, once per
    change of Shape or Material (they depend on nu only; Size and Thickness
    only rescale omega).

    Coordinates are the plate's own: x, y in [ -1, 1 ] across the square, or
    r <= 1 on the disc, so a point in metres is L times it, L the half-side or
    the radius. Every mode is normalised so its mean square over the plate is
    1, which makes every modal mass the plate's mass.

    omega_k = Lambda_k c / L^2 with c = sqrt( D / rho h ):
    - square, Rayleigh-Ritz in normalised Legendre polynomials (Lambda = sqrt R,
      R an eigenvalue of K on ( -1, 1 )^2; Leissa's lambda = omega a^2 sqrt( rho h / D )
      with a the side is 4 Lambda);
    - circle, J_n and I_n, lambda a root of Leissa's eq. (2.14), Lambda = lambda^2.

    SPEC-eidophone.md, "The plate", has the derivation and what was checked.
*/
namespace eido::modes
{

enum ModeClass : int
{
	kRigid = 0,///< omega 0: translation, or the tilt about y
	kSS,       ///< square: even in x and y, symmetric about the diagonal (centre-driven)
	kSA,       ///< square: even in x and y, antisymmetric about the diagonal
	kOE,       ///< square: odd in x, even in y
	kOS,       ///< square, harness only: odd in x and y, symmetric about the diagonal (Leissa's AS-S)
	kOA,       ///< square, harness only: odd-odd, antisymmetric about the diagonal
	kCircle    ///< J_n + C I_n, cos n theta
};

/// Negative controls: each breaks the model the way a known slip would.
struct Hooks
{
	bool dropNu         = false;///< the nu ( w_xx w_yy ) term left out of K
	bool twistHalf      = false;///< the twist term 2( 1 - nu ) written ( 1 - nu )
	bool momentSignFlip = false;///< the circle's moment condition with ( 1 - nu ) negated
};

struct Mode
{
	double Lambda = 0.0;///< omega = Lambda c / L^2
	double paper  = 0.0;///< the published parameter: square 4 Lambda, circle lambda^2
	int cls       = kRigid;
	int n = 0, s = 0;   ///< circle: nodal diameters and circles
	double root = 0.0;  ///< circle: lambda
	double C    = 0.0;  ///< circle: the I_n coefficient
	double norm = 1.0;  ///< circle: the radial function's RMS factor
	/// Square: N x N, coef[ i * N + j ] multiplies P~_i( x ) P~_j( y ). Circle:
	/// the radial table, ( R, dR/dr ) pairs at evenly spaced r to just past the
	/// rim, normalised, for Sample's Hermite interpolation.
	std::vector< double > coef;
};

class Table
{
public:
	Shape shape = Shape::Square;
	double nu   = 0.3;
	int N       = 0;///< square: Legendre degrees 0..N-1 in each direction
	std::vector< Mode > modes;///< the rigid modes first, then the elastic ones ascending

	/// The mode at a point, exact (from the coefficients or the Bessel form).
	double W( const Mode& mode, double x, double y ) const;
	void Gradient( const Mode& mode, double x, double y, double& wx, double& wy ) const;
	/// The S x S lattice of texel centres over [ -1, 1 ]^2, row 0 at y = -1.
	void Sample( const Mode& mode, int S, float* out ) const;
	/// The radial function of a circle mode, exact.
	double Radial( const Mode& mode, double r, double* derivative = nullptr ) const;
};

/// The lowest `count` elastic modes the +x axis can excite (SS, SA, OE),
/// plus the rigid translation and tilt.
Table BuildSquare( double nu, int N, int count, const Hooks& hooks = Hooks() );
Table BuildCircle( double nu, int count, const Hooks& hooks = Hooks() );

/// One symmetry class of the square, every eigenvalue (as Leissa's lambda,
/// 4 sqrt R, ascending); with `modes`, the RMS-normalised coefficients too.
std::vector< double > SquareClass( double nu, int N, int cls, const Hooks& hooks, std::vector< std::vector< double > >* modes = nullptr );
/// x^T K x / x^T x for w = x y (the twist alone): 18( 1 - nu ) exactly.
double RayleighOfXY( double nu, int N, const Hooks& hooks );

/// Leissa eq. (2.14), the free circular plate's frequency equation, divided by I_n( lambda ) lambda^3.
double FrequencyEquation( int n, double lambda, double nu, bool flip = false );
/// Its roots lambda, ascending, below lambdaMax.
std::vector< double > CircleRoots( int n, double nu, double lambdaMax, bool flip = false );
/// lambda^2 by Rayleigh-Ritz in polar coordinates (radial basis r^n P_k( 2r^2 - 1 ),
/// weak form): an independent check on the Bessel solution, for the harness.
std::vector< double > PolarRitz( int n, double nu, int K );

double BesselJ( int n, double x );
double BesselI( int n, double x );

/// Normalised Legendre polynomials P~_k = sqrt( ( 2k + 1 ) / 2 ) P_k and their
/// first and second derivatives at x, k = 0..N-1.
void Legendre( int N, double x, double* p, double* dp = nullptr, double* d2p = nullptr );

} // namespace eido::modes
