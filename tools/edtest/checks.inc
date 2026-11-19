//===========================================================================
// The checks. Each sets every control it depends on (honeydew's lesson), and
// every GL check runs at each raster in kRasters.
//===========================================================================

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr )
{
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.SetHooksForTest( perturb.hooks );
	return true;
}

double wallNow()
{
	using namespace std::chrono;
	return duration_cast< duration< double > >( steady_clock::now().time_since_epoch() ).count();
}

const double kG = kGravity;

std::shared_ptr< const modes::Table > aluminiumTable( Shape shape )
{
	return EidophonePlugin::TableFor( shape, MaterialOf( Material::Aluminium ).nu );
}

/// The index of the `skip`-th mode of a class in a table (-1 if none).
int findMode( const modes::Table& table, int cls, int skip = 0 )
{
	for( size_t k = 0; k < table.modes.size(); ++k )
		if( table.modes[ k ].cls == cls && skip-- == 0 )
			return static_cast< int >( k );
	return -1;
}
int findCircle( const modes::Table& table, int n, int s )
{
	for( size_t k = 0; k < table.modes.size(); ++k )
		if( table.modes[ k ].cls == modes::kCircle && table.modes[ k ].n == n && table.modes[ k ].s == s )
			return static_cast< int >( k );
	return -1;
}

/// The modal state set directly: a_k = accel (m/s^2) on one mode, optionally a
/// complex second one, the grains' w_d = omega.
void setPure( Rig& rig, int mode, double accel, double omega, int mode2 = -1, double accel2 = 0.0, double accel2Imag = 0.0 )
{
	TestDrive d;
	d.pure       = true;
	d.mode       = mode;
	d.accel      = accel;
	d.mode2      = mode2;
	d.accel2     = accel2;
	d.accel2Imag = accel2Imag;
	d.omega      = omega;
	rig.plugin.SetTestDrive( d );
}

/// The model's acceleration where a grain is: the field lattice's bilinear
/// interpolant, clamped at its edge, and its derivatives per plate unit -- the
/// shader's fieldAt(), in double, from the lattice as read back.
struct Accel
{
	double A[ 2 ], Ax[ 2 ], Ay[ 2 ];
	double a;       ///< |A|
	double gx, gy;  ///< grad |A|, per plate unit
};
Accel fieldAt( const Floats& field, double ux, double uy )
{
	const int S   = shaders::kFieldSize;
	const double tx = ( ux + 1.0 ) * 0.5 * S - 0.5, ty = ( uy + 1.0 ) * 0.5 * S - 0.5;
	const double ix = std::floor( tx ), iy = std::floor( ty );
	const double fx = tx - ix, fy = ty - iy;
	const int lox = std::clamp( static_cast< int >( ix ), 0, S - 1 ), hix = std::clamp( static_cast< int >( ix ) + 1, 0, S - 1 );
	const int loy = std::clamp( static_cast< int >( iy ), 0, S - 1 ), hiy = std::clamp( static_cast< int >( iy ) + 1, 0, S - 1 );
	auto at = [ & ]( int x, int y, int c ) { return static_cast< double >( field[ ( static_cast< size_t >( y ) * S + x ) * 4 + c ] ); };
	Accel r {};
	for( int c = 0; c < 2; ++c )
	{
		const double a00 = at( lox, loy, c ), a10 = at( hix, loy, c ), a01 = at( lox, hiy, c ), a11 = at( hix, hiy, c );
		const double bottom = a00 + ( a10 - a00 ) * fx, top = a01 + ( a11 - a01 ) * fx;
		r.A[ c ]  = bottom + ( top - bottom ) * fy;
		r.Ax[ c ] = ( ( a10 - a00 ) + ( ( a11 - a01 ) - ( a10 - a00 ) ) * fy ) * 0.5 * S;
		r.Ay[ c ] = ( top - bottom ) * 0.5 * S;
	}
	r.a  = std::hypot( r.A[ 0 ], r.A[ 1 ] );
	r.gx = r.a > 0.0 ? ( r.A[ 0 ] * r.Ax[ 0 ] + r.A[ 1 ] * r.Ax[ 1 ] ) / r.a : 0.0;
	r.gy = r.a > 0.0 ? ( r.A[ 0 ] * r.Ay[ 0 ] + r.A[ 1 ] * r.Ay[ 1 ] ) / r.a : 0.0;
	return r;
}

double latticeMax( const Floats& field )
{
	double m = 0.0;
	for( size_t i = 0; i < field.size(); i += 4 )
		m = std::max( m, std::hypot( static_cast< double >( field[ i ] ), static_cast< double >( field[ i + 1 ] ) ) );
	return m;
}

bool sameTexel( const Floats& a, const Floats& b, size_t i )
{
	return std::memcmp( &a[ 4 * i ], &b[ 4 * i ], 4 * sizeof( float ) ) == 0;
}

/// Grains whose position changed between two readbacks, among the first n.
int movedCount( const Floats& a, const Floats& b, int n )
{
	int moved = 0;
	for( int i = 0; i < n; ++i )
		moved += a[ 4 * static_cast< size_t >( i ) ] != b[ 4 * static_cast< size_t >( i ) ] || a[ 4 * static_cast< size_t >( i ) + 1 ] != b[ 4 * static_cast< size_t >( i ) + 1 ];
	return moved;
}

/// Renders with a few ms of real time between frames until `done` holds or
/// `seconds` of wall time pass: for what earshot's worker does off the
/// render thread (an open, a retry).
bool renderUntil( Rig& rig, const std::function< bool() >& done, double seconds )
{
	const double start = wallNow();
	while( wallNow() - start < seconds )
	{
		if( !rig.Render( 1 ) )
			return false;
		if( done() )
			return true;
		std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
	}
	return done();
}

/// 64 Gauss-Legendre points on ( -1, 1 ), by Newton on the three-term recurrence.
void gaussLegendre64( std::vector< double >& x, std::vector< double >& w )
{
	const int n = 64;
	x.assign( n, 0.0 );
	w.assign( n, 0.0 );
	for( int i = 0; i < n; ++i )
	{
		double z = std::cos( kPi * ( i + 0.75 ) / ( n + 0.5 ) );
		double dp = 0.0;
		for( int it = 0; it < 100; ++it )
		{
			double p0 = 1.0, p1 = z;
			for( int k = 2; k <= n; ++k )
			{
				const double p2 = ( ( 2.0 * k - 1.0 ) * z * p1 - ( k - 1.0 ) * p0 ) / k;
				p0 = p1;
				p1 = p2;
			}
			dp = n * ( z * p1 - p0 ) / ( z * z - 1.0 );
			const double dz = p1 / dp;
			z -= dz;
			if( std::fabs( dz ) < 1e-16 )
				break;
		}
		x[ static_cast< size_t >( i ) ] = z;
		w[ static_cast< size_t >( i ) ] = 2.0 / ( ( 1.0 - z * z ) * dp * dp );
	}
}

//===========================================================================
// --modes (no GL): the free square.
//===========================================================================
int runModes( const Perturb& perturb )
{
	std::printf( "\n=== modes: the free square against Iguchi's eigenvalues, Leissa's bounds and Ritz's (Gander & Wanner); the twist exactly; "
	             "converged; orthonormal\n" );
	const modes::Hooks& hooks = perturb.modeHooks;
	const int N               = 36;
	//The nearest of a list to x: its relative distance, and whether x is above it.
	auto nearest = []( const std::vector< double >& v, double x, bool& above ) {
		double best = 1e300;
		for( double y : v )
			if( std::fabs( y / x - 1.0 ) < best )
			{
				best  = std::fabs( y / x - 1.0 );
				above = x >= y;
			}
		return best;
	};
	{
		//Iguchi (1953) via Leissa, nu = 0.3, lambda = w a^2 sqrt( rho h / D ).
		//Iguchi's SS-A 65.3680 is left out: no mode of the class lies near it
		//(AGENTS.md, "References").
		const auto ss = modes::SquareClass( 0.3, N, modes::kSS, hooks );
		const auto sa = modes::SquareClass( 0.3, N, modes::kSA, hooks );
		const double iguchiSS[] = { 24.2702, 63.6870, 122.4449, 168.4888, 299.9325 };
		const double iguchiSA[] = { 19.5961, 117.1093, 161.5049, 293.7190 };
		double worst = 0.0;
		int above    = 0;
		for( int pass = 0; pass < 2; ++pass )
			for( double v : pass == 0 ? std::vector< double >( std::begin( iguchiSS ), std::end( iguchiSS ) )
			                          : std::vector< double >( std::begin( iguchiSA ), std::end( iguchiSA ) ) )
			{
				bool up = false;
				worst   = std::max( worst, nearest( pass == 0 ? ss : sa, v, up ) );
				above += up;
			}
		Check( worst <= 1e-4, fmt( "Iguchi's nine doubly-symmetric eigenvalues (nu 0.3, SS 24.2702 .. 299.9325, SA 19.5961 .. 293.7190): the "
		                           "farthest %.1e from the nearest here, relative (allowed 1e-4); %d of 9 at or above it",
		                           worst, above ) );
	}
	{
		//Leissa Table 4.67 (Waller's lower bounds; the 50-term Ritz upper bounds of
		//ref. 4.118), the class odd in x and y and symmetric about the diagonal.
		const double lower225[] = { 13.851, 76.245, 151.54, 210.90, 293.27, 421.26, 438.47, 504.41, 654.62, 710.70 };
		const double upper225[] = { 14.118, 77.576, 156.36, 214.67, 302.24, 430.76, 456.37, 519.03, 682.76, 726.97 };
		const double lower30[]  = { 13.201, 75.735, 147.71, 209.46, 288.72, 416.00, 432.13, 498.77, 645.60, 701.20 };
		const double upper30[]  = { 13.473, 77.354, 153.07, 214.62, 299.05, 430.33, 450.71, 516.19, 676.35, 727.08 };
		int outside = 0;
		std::string where;
		for( int pass = 0; pass < 2; ++pass )
		{
			const double nu  = pass == 0 ? 0.225 : 0.3;
			const auto os    = modes::SquareClass( nu, N, modes::kOS, hooks );
			const double* lo = pass == 0 ? lower225 : lower30;
			const double* hi = pass == 0 ? upper225 : upper30;
			for( int k = 0; k < 10 && k < static_cast< int >( os.size() ); ++k )
			{
				const double half = lo[ k ] < 100.0 ? 0.0005 : 0.005;//half a unit in the last printed digit
				if( os[ static_cast< size_t >( k ) ] < lo[ k ] - half || os[ static_cast< size_t >( k ) ] > hi[ k ] + 0.0005 * ( hi[ k ] < 100.0 ? 1.0 : 10.0 ) )
				{
					++outside;
					where += fmt( " nu %.3f #%d %.3f", nu, k + 1, os[ static_cast< size_t >( k ) ] );
				}
			}
		}
		const auto os225 = modes::SquareClass( 0.225, N, modes::kOS, hooks );
		//Table 4.117's bracket on the lowest, and Ritz's own values (Gander & Wanner, table 3).
		const bool bracket   = os225.size() > 0 && os225[ 0 ] > 14.1028 && os225[ 0 ] < 14.1165;
		const double ritz[]  = { 14.135, 77.907, 157.95, 215.40, 308.84, 477.25 };
		int aboveRitz        = 0;
		for( int k = 0; k < 6 && k < static_cast< int >( os225.size() ); ++k )
			aboveRitz += os225[ static_cast< size_t >( k ) ] >= ritz[ k ];
		Check( outside == 0, fmt( "the odd-odd diagonal-symmetric class at nu 0.225 and 0.3: all 20 inside Leissa's Table 4.67 bounds (%d outside:%s)", outside,
		                          where.empty() ? " none" : where.c_str() ) );
		Check( bracket && aboveRitz == 0, fmt( "nu 0.225: the lowest %.5f inside Table 4.117's 14.1028 .. 14.1165; the six below Ritz's 1909 values "
		                                       "(14.135 .. 477.25): %d at or above",
		                                       os225.empty() ? 0.0 : os225[ 0 ], aboveRitz ) );
	}
	{
		const double xy = modes::RayleighOfXY( 0.3, N, hooks );
		Check( std::fabs( xy / ( 18.0 * 0.7 ) - 1.0 ) < 1e-12, fmt( "the twist w = xy: Rayleigh quotient %.15f against 18( 1 - nu ) = 12.6 (off %.1e)", xy,
		                                                            std::fabs( xy / 12.6 - 1.0 ) ) );
	}
	{
		const auto ss = modes::SquareClass( 0.33, N, modes::kSS, hooks );
		const auto oe = modes::SquareClass( 0.33, N, modes::kOE, hooks );
		Check( ss.size() > 1 && oe.size() > 1 && ss[ 0 ] < 1e-4 && oe[ 0 ] < 1e-4 && ss[ 1 ] > 10.0 && oe[ 1 ] > 10.0,
		       fmt( "the rigid modes: the doubly-even class's lowest lambda %.1e (translation), the odd-even's %.1e (tilt); the next %.2f and %.2f",
		            ss.empty() ? -1.0 : ss[ 0 ], oe.empty() ? -1.0 : oe[ 0 ], ss.size() > 1 ? ss[ 1 ] : -1.0, oe.size() > 1 ? oe[ 1 ] : -1.0 ) );
	}
	{
		const modes::Table a = modes::BuildSquare( 0.33, N, 64, hooks );
		const modes::Table b = modes::BuildSquare( 0.33, N + 6, 64, hooks );
		double worst = 0.0;
		for( size_t k = 2; k < a.modes.size() && k < b.modes.size(); ++k )
			worst = std::max( worst, std::fabs( a.modes[ k ].Lambda / b.modes[ k ].Lambda - 1.0 ) );
		Check( a.modes.size() == 66 && worst < 1e-6, fmt( "converged: the 64 elastic modes at %d Legendre degrees against %d differ by at most %.1e (relative); "
		                                                  "the highest %.2f (Leissa's lambda)",
		                                                  N, N + 6, worst, a.modes.empty() ? 0.0 : a.modes.back().paper ) );
		std::vector< double > gx, gw;
		gaussLegendre64( gx, gw );
		const size_t M = a.modes.size();
		std::vector< double > values( M * 64 * 64 );
		for( size_t k = 0; k < M; ++k )
			for( int i = 0; i < 64; ++i )
				for( int j = 0; j < 64; ++j )
					values[ ( k * 64 + static_cast< size_t >( i ) ) * 64 + static_cast< size_t >( j ) ] = a.W( a.modes[ k ], gx[ static_cast< size_t >( i ) ], gx[ static_cast< size_t >( j ) ] );
		double offI = 0.0;
		for( size_t p = 0; p < M; ++p )
			for( size_t q = p; q < M; ++q )
			{
				double sum = 0.0;
				for( int i = 0; i < 64; ++i )
					for( int j = 0; j < 64; ++j )
						sum += gw[ static_cast< size_t >( i ) ] * gw[ static_cast< size_t >( j ) ] * values[ ( p * 64 + static_cast< size_t >( i ) ) * 64 + static_cast< size_t >( j ) ]
						       * values[ ( q * 64 + static_cast< size_t >( i ) ) * 64 + static_cast< size_t >( j ) ];
				offI = std::max( offI, std::fabs( 0.25 * sum - ( p == q ? 1.0 : 0.0 ) ) );
			}
		Check( offI < 1e-9, fmt( "orthonormal: the mean of W_i W_j over the plate by 64 x 64 Gauss-Legendre (exact for these degrees), all %zu x %zu: "
		                         "off the identity by at most %.1e",
		                         M, M, offI ) );
	}
	return Verdict();
}

//===========================================================================
// --circle (no GL): the free disc.
//===========================================================================
int runCircle( const Perturb& perturb )
{
	std::printf( "\n=== circle: the Bessel roots against a polar Rayleigh-Ritz solve, Leissa's Tables 2.5-2.7, and Chladni's law\n" );
	const bool flip = perturb.modeHooks.momentSignFlip;
	{
		double worst = 0.0;
		int compared = 0;
		for( double nu : { 0.25, 0.33 } )
			for( int n = 0; n <= 3; ++n )
			{
				const auto roots = modes::CircleRoots( n, nu, 30.0, flip );
				std::vector< double > ritz;
				for( double v : modes::PolarRitz( n, nu, 24 ) )
					if( v > 1e-3 )
						ritz.push_back( v );
				for( size_t k = 0; k < roots.size() && k < ritz.size() && roots[ k ] * roots[ k ] < 900.0; ++k )
				{
					worst = std::max( worst, std::fabs( roots[ k ] * roots[ k ] / ritz[ k ] - 1.0 ) );
					++compared;
				}
			}
		Check( compared >= 50 && worst < 1e-7, fmt( "%d roots (n 0-3, nu 0.25 and 0.33, lambda^2 < 900): eq. (2.14)'s against polar Ritz (24 terms) within %.1e "
		                                            "(allowed 1e-7)",
		                                            compared, worst ) );
	}
	{
		//Leissa Table 2.6 (nu = 0.25) and 2.5 (nu = 0.33), read from the page
		//images. Two misprints left out: Table 2.6 n=1 s=4 (196.67) and Table 2.5
		//n=1 s=7 (651.8), each out of sequence with its column.
		const std::vector< std::vector< double > > t26 = {
			{ 8.892, 38.34, 87.65, 156.73, 245.52, 354.08, 482.37, 630.41, 798.23 },
			{ 20.41, 59.74, 118.88, -1, 296.46, 414.86, 553.00, 710.92, 888.58 },
			{ 5.513, 35.28, 84.38, 153.29, 241.99, 350.48, 478.73, 626.75, 794.51, 982.01 },
			{ 12.75, 53.16, 112.36, 191.02, 289.51, 408.16, 545.83, 703.63, 881.20, 1078.5 },
		};
		const std::vector< std::vector< double > > t25 = {
			{ 9.084, 38.55, 87.80, 157.0, 245.9, 354.6, 483.1, 631.0, 798.6, 986.0 },
			{ 20.52, 59.86, 119.0, 198.2, 296.9, 415.3, -1, 711.3, 888.6, 1086 },
			{ 5.253, 35.25, 83.9, 154.0, 242.7, 350.8, 479.2, 627.0, 794.7, 981.6, 1188 },
			{ 12.23, 52.91, 111.3, 192.1, 290.7, 408.4, 546.2, 703.3, 880.3, 1076, 1292 },
		};
		for( int pass = 0; pass < 2; ++pass )
		{
			const double nu    = pass == 0 ? 0.25 : 0.33;
			const auto& table  = pass == 0 ? t26 : t25;
			const double limit = pass == 0 ? 0.01 : 0.02;
			double worst = 0.0;
			int count = 0, missing = 0;
			for( int n = 0; n <= 3; ++n )
			{
				const auto roots = modes::CircleRoots( n, nu, 45.0, flip );
				for( size_t k = 0; k < table[ static_cast< size_t >( n ) ].size(); ++k )
				{
					const double want = table[ static_cast< size_t >( n ) ][ k ];
					if( want < 0.0 )
						continue;
					if( k >= roots.size() )
					{
						++missing;
						continue;
					}
					worst = std::max( worst, std::fabs( roots[ k ] * roots[ k ] / want - 1.0 ) );
					++count;
				}
			}
			Check( missing == 0 && worst < limit, fmt( "Leissa Table %s (nu %.2f): %d values for n 0-3, the farthest %.2f%% off (allowed %.0f%%; the table's own "
			                                           "precision varies), %d missing",
			                                           pass == 0 ? "2.6" : "2.5", nu, count, 100.0 * worst, 100.0 * limit, missing ) );
		}
	}
	{
		//Table 2.7 (nu = 0.33), n = 0: the radii of the nodal circles.
		const modes::Table table = modes::BuildCircle( 0.33, 64, perturb.modeHooks );
		const std::vector< std::vector< double > > want = { { 0.680 }, { 0.841, 0.391 }, { 0.893, 0.591, 0.257 } };
		double worst = 0.0;
		int wrongCount = 0;
		for( int s = 1; s <= 3; ++s )
		{
			const int k = findCircle( table, 0, s );
			std::vector< double > zeros;
			if( k >= 0 )
			{
				const modes::Mode& mode = table.modes[ static_cast< size_t >( k ) ];
				double r0 = 1e-6, f0 = table.Radial( mode, r0 );
				for( int i = 1; i <= 4000; ++i )
				{
					const double r1 = i / 4000.0, f1 = table.Radial( mode, r1 );
					if( f0 * f1 < 0.0 )
					{
						double a = r0, b = r1, fa = f0;
						for( int it = 0; it < 60; ++it )
						{
							const double m = 0.5 * ( a + b ), fm = table.Radial( mode, m );
							if( fa * fm <= 0.0 )
								b = m;
							else
							{
								a  = m;
								fa = fm;
							}
						}
						zeros.push_back( 0.5 * ( a + b ) );
					}
					r0 = r1;
					f0 = f1;
				}
			}
			std::sort( zeros.rbegin(), zeros.rend() );
			const auto& w = want[ static_cast< size_t >( s - 1 ) ];
			if( zeros.size() != w.size() )
			{
				++wrongCount;
				continue;
			}
			for( size_t i = 0; i < w.size(); ++i )
				worst = std::max( worst, std::fabs( zeros[ i ] - w[ i ] ) );
		}
		Check( wrongCount == 0 && worst <= 0.001, fmt( "Table 2.7 (nu 0.33): the six nodal-circle radii of the axisymmetric modes s = 1-3, the farthest %.4f off "
		                                               "(allowed 0.001); %d modes with the wrong count of circles",
		                                               worst, wrongCount ) );
	}
	{
		//Chladni: f ~ ( m + 2n )^2 for m diameters and n circles. Here lambda / ( m + 2n )
		//must settle monotonically on pi/2 (Rayleigh's asymptote).
		int notMonotone = 0;
		double worst    = 0.0;
		for( int n = 0; n <= 3; ++n )
		{
			const auto roots = modes::CircleRoots( n, 0.33, 45.0, flip );
			const int first  = n < 2 ? 1 : 0;//the circle count of roots[ 0 ]
			std::vector< double > ratio;
			for( int s = 2; s <= 10; ++s )
			{
				const size_t k = static_cast< size_t >( s - first );
				if( k < roots.size() )
					ratio.push_back( roots[ k ] / ( n + 2.0 * s ) );
			}
			if( ratio.size() != 9 )
			{
				++notMonotone;
				continue;
			}
			const double sign = ratio[ 1 ] - ratio[ 0 ];
			for( size_t i = 1; i < ratio.size(); ++i )
				notMonotone += ( ratio[ i ] - ratio[ i - 1 ] ) * sign <= 0.0 || std::fabs( ratio[ i ] - 0.5 * kPi ) > std::fabs( ratio[ i - 1 ] - 0.5 * kPi );
			worst = std::max( worst, std::fabs( ratio.back() / ( 0.5 * kPi ) - 1.0 ) );
		}
		Check( notMonotone == 0 && worst < 0.01, fmt( "Chladni's law: lambda / ( m + 2n ) closes on pi/2 monotonically from n = 2 circles to 10 for 0-3 "
		                                              "diameters (%d steps not closer); at 10 circles within %.2f%% (allowed 1%%)",
		                                              notMonotone, 100.0 * worst ) );
	}
	return Verdict();
}

//===========================================================================
// --plate (no GL): the modal integrator against the damped oscillator.
//===========================================================================
struct PlateRig
{
	std::shared_ptr< const modes::Table > table;
	PlateConfig config;
	Plate plate;
};

PlateRig makePlate( const Perturb& perturb, double driveAt )
{
	PlateRig r;
	const MaterialData& m = MaterialOf( Material::Aluminium );
	r.table               = aluminiumTable( Shape::Square );
	r.config.halfSize     = 0.15;
	r.config.speed        = PlateSpeed( m, 1.0e-3 );
	r.config.mass         = m.rho * 1.0e-3 * 0.30 * 0.30;
	r.config.q            = 40.0;
	r.config.driveAt      = driveAt;
	r.plate.hooks         = perturb.hooks & 7;
	r.plate.SetTable( r.table );
	r.plate.Configure( r.config );
	return r;
}

int runPlate( const Perturb& perturb )
{
	std::printf( "\n=== plate: each mode a damped oscillator, integrated exactly: the steady state, the ring-down, any frame rate, the centre's "
	             "symmetry, the rigid shake\n" );
	const double F0 = 1.0, wd = 2.0 * kPi * 300.0, xd = 0.37;
	{
		PlateRig r = makePlate( perturb, xd );
		for( int f = 0; f < 600; ++f )
			r.plate.Advance( 1.0 / 60.0, F0, wd );
		//Independently: G_k = ( F0 W_k( x_d ) / M ) / ( w_k^2 - w_d^2 + 2 i zeta w_k w_d ).
		const double zeta  = 0.5 / r.config.q;
		const double scale = r.config.speed / ( r.config.halfSize * r.config.halfSize );
		const std::complex< double > drive = std::polar( 1.0, r.plate.Phase() );
		double worst = 0.0, biggest = 0.0;
		for( int k = 2; k < r.plate.Count(); ++k )
		{
			const modes::Mode& mode = r.table->modes[ static_cast< size_t >( k ) ];
			const double w          = mode.Lambda * scale;
			const double f          = F0 * r.table->W( mode, xd, 0.0 ) / r.config.mass;
			const std::complex< double > G = f / std::complex< double >( w * w - wd * wd, 2.0 * zeta * w * wd );
			biggest = std::max( biggest, std::abs( G ) );
			worst   = std::max( worst, std::fabs( r.plate.Displacement( k ) - ( G * drive ).real() ) );
			worst   = std::max( worst, std::fabs( r.plate.Velocity( k ) - ( std::complex< double >( 0.0, wd ) * G * drive ).real() ) / wd );
		}
		Check( worst / biggest < 1e-9, fmt( "driven 10 s at 300 Hz from x_d = %.2f (60 fps): every elastic mode's q and q'/w_d on Re[ G e^{i phi} ], G from "
		                                    "the oscillator's formula, within %.1e of the largest |G|",
		                                    xd, worst / biggest ) );
		const double translation = std::abs( r.plate.Acceleration( 0 ) );
		const double want        = F0 / r.config.mass;
		Check( std::fabs( translation / want - 1.0 ) < 1e-12, fmt( "the rigid translation's acceleration %.6f m/s^2 against F0/M = %.6f (off %.1e)", translation,
		                                                           want, std::fabs( translation / want - 1.0 ) ) );

		//Switch the drive off: the free decay in closed form from where it was.
		std::vector< double > q0, v0;
		for( int k = 0; k < r.plate.Count(); ++k )
		{
			q0.push_back( r.plate.Displacement( k ) );
			v0.push_back( r.plate.Velocity( k ) );
		}
		for( int f = 0; f < 120; ++f )
			r.plate.Advance( 1.0 / 60.0, 0.0, wd );
		double worstFree = 0.0, q0max = 0.0;
		for( int k = 2; k < r.plate.Count(); ++k )
		{
			const double w = r.plate.Omega( k ), zw = zeta * w, wdk = w * std::sqrt( 1.0 - zeta * zeta ), t = 2.0;
			const double closed = std::exp( -zw * t ) * ( q0[ static_cast< size_t >( k ) ] * std::cos( wdk * t )
			                                              + ( v0[ static_cast< size_t >( k ) ] + zw * q0[ static_cast< size_t >( k ) ] ) / wdk * std::sin( wdk * t ) );
			worstFree = std::max( worstFree, std::fabs( r.plate.Displacement( k ) - closed ) );
			q0max     = std::max( q0max, std::fabs( q0[ static_cast< size_t >( k ) ] ) );
		}
		Check( worstFree / q0max < 1e-12, fmt( "then 2 s undriven in 120 frames: every mode on e^{-zeta w t}( q0 cos w_d t + ( v0 + zeta w q0 ) / w_d sin w_d t ) "
		                                       "within %.1e of the largest q0",
		                                       worstFree / q0max ) );
	}
	{
		//A drive that changes every 1/6 s, run at four rates.
		auto schedule = []( int segment, double& F, double& w ) {
			F = 0.5 + 0.4 * std::sin( 1.7 * segment );
			w = 2.0 * kPi * ( 150.0 + 90.0 * ( ( segment * 7 ) % 11 ) );
		};
		std::vector< std::vector< double > > results;
		const int rates[] = { 240, 24, 30, 60 };
		for( int fps : rates )
		{
			PlateRig r = makePlate( perturb, xd );
			const int per = fps / 6;
			for( int f = 0; f < fps * 7 / 2; ++f )
			{
				double F = 0.0, w = 0.0;
				schedule( f / per, F, w );
				r.plate.Advance( 1.0 / fps, F, w );
			}
			std::vector< double > state;
			for( int k = 0; k < r.plate.Count(); ++k )
			{
				state.push_back( r.plate.Displacement( k ) );
				state.push_back( r.plate.Velocity( k ) );
			}
			results.push_back( state );
		}
		double worst = 0.0, biggest = 0.0;
		for( size_t i = 0; i < results[ 0 ].size(); ++i )
			biggest = std::max( biggest, std::fabs( results[ 0 ][ i ] ) );
		for( size_t run = 1; run < results.size(); ++run )
			for( size_t i = 0; i < results[ 0 ].size(); ++i )
				worst = std::max( worst, std::fabs( results[ run ][ i ] - results[ 0 ][ i ] ) );
		Check( worst / biggest < 1e-9, fmt( "a drive changing every 1/6 s for 3.5 s at 24, 30 and 60 fps against 240: every q and q' within %.1e of the largest",
		                                    worst / biggest ) );
	}
	{
		PlateRig r = makePlate( perturb, 0.0 );
		for( int f = 0; f < 60; ++f )
			r.plate.Advance( 1.0 / 60.0, F0, wd );
		double symmetric = 0.0, other = 0.0;
		int ss = 0;
		for( int k = 2; k < r.plate.Count(); ++k )
		{
			const int cls = r.table->modes[ static_cast< size_t >( k ) ].cls;
			const double a = std::abs( r.plate.AnalyticDisplacement( k ) );
			if( cls == modes::kSS )
			{
				symmetric = std::max( symmetric, a );
				ss += a > 0.0;
			}
			else
				other = std::max( other, a );
		}
		const double tilt = std::abs( r.plate.Acceleration( 1 ) ) / std::abs( r.plate.Acceleration( 0 ) );
		Check( other <= 1e-12 * symmetric && tilt <= 1e-12 && ss > 5, fmt( "driven at the centre: %d doubly-symmetric modes respond; every other mode at most "
		                                                                     "%.1e of the largest, the tilt %.1e of the translation",
		                                                                     ss, symmetric > 0.0 ? other / symmetric : 1.0, tilt ) );
	}
	return Verdict();
}

//===========================================================================
// --audio (no GL): what drives the plate, from earshot.
//===========================================================================
int runAudio( const Perturb& perturb )
{
	std::printf( "\n=== audio: the device path's frequency is 2 pi x the voiced pitch, held, seeded by the strongest partial; its loudness the band "
	             "envelopes, over a released running peak; the host's FFT guesses from its strongest bin (never bin 0); the menu\n" );
	{
		AudioInput audio;
		audio.hooks = perturb.hooks;
		const auto& slots = audio.Slots();
		bool layout = slots.size() == static_cast< size_t >( kDeviceElements ) && slots[ 0 ].label == AudioInput::HostLabel() && slots[ 0 ].value == 0.0f
		              && slots[ 1 ].name == earshot::kDefaultDevice && slots[ 1 ].value == earshot::deviceKey( earshot::kDefaultDevice );
		std::map< float, int > values;
		int devices = 0, spares = 0;
		for( size_t i = 0; i < slots.size(); ++i )
		{
			++values[ slots[ i ].value ];
			if( i >= 2 && !slots[ i ].name.empty() )
			{
				++devices;
				layout = layout && slots[ i ].value == earshot::deviceKey( slots[ i ].name.c_str() ) && slots[ i ].value > 0.0f;
			}
			else if( i >= 2 )
			{
				++spares;
				layout = layout && slots[ i ].value < 0.0f;
			}
		}
		Check( layout && values.size() == slots.size() && devices == static_cast< int >( g_script.present.size() ),
		       fmt( "the menu: '%s' = 0, 'System default', %d devices each valued by deviceKey( name ), %d spares below 0, %zu distinct values in %zu",
		            AudioInput::HostLabel(), devices, spares, values.size(), slots.size() ) );
		audio.Select( slots[ 2 ].value );
		const bool byName = audio.Requested() == slots[ 2 ].name;
		audio.Select( slots.back().value );
		const bool spare = audio.Requested().empty();
		audio.Select( earshot::deviceKey( "Not Plugged In" ) );
		const bool missing = audio.Requested().rfind( "(missing device", 0 ) == 0;
		audio.Select( slots[ 1 ].value );
		const bool fallback = audio.Requested() == earshot::kDefaultDevice;
		audio.Select( 0.0f );
		Check( byName && spare && missing && fallback && audio.Requested().empty(),
		       fmt( "Select: a device's value opens it by name (%s), a spare and 0 the host FFT (%s), an unknown key a name nothing matches (%s), "
		            "'System default' the default (%s)",
		            byName ? "yes" : "NO", spare ? "yes" : "NO", missing ? "yes" : "NO", fallback ? "yes" : "NO" ) );
	}
	{
		//The host's axis: earshot's 48 kHz until the host says otherwise (a
		//rate no engine runs at is ignored), then the host's, 64 bins to its
		//Nyquist (earshot's AGENTS.md, "Resolume's 64 bins, measured"). One
		//instance never told; one told 44.1 kHz before its first frame (when
		//Clarity is applied too, which must not undo it), then 7 Hz, 48 and
		//96 kHz.
		int wrong = 0;
		std::string what;
		double t = 1.0;
		auto peaks = [ & ]( AudioInput& audio, unsigned int set, double rate ) {
			for( BinLaw law : { BinLaw::Linear, BinLaw::Log } )
				for( int peak : { 1, 9, 40 } )
				{
					float bins[ 64 ];
					for( int j = 0; j < 64; ++j )
						bins[ j ] = 0.02f;
					bins[ 0 ]    = 0.9f;//DC and the lowest bass: loud, and never the note
					bins[ peak ] = 0.5f;
					t += 1.0;
					const Drive d = audio.Update( t, bins, 64, 0.8, law );
					//The spec's centres, written out here rather than called: 64 bins to the axis's Nyquist.
					const double nyquist = 0.5 * rate;
					const double u       = ( peak + 0.5 ) / 64.0;
					const double centre  = law == BinLaw::Log ? 20.0 * std::pow( nyquist / 20.0, u ) : u * nyquist;
					const bool ok = d.note && !d.fromDevice && d.bin == peak && std::fabs( d.hz / centre - 1.0 ) < 1e-12 && d.omega == 2.0 * kPi * d.hz
					                && d.loudness == 0.5 && d.relative == 1.0;
					wrong += !ok;
					if( !ok )
						what += fmt( " %.0f Hz %s bin %d -> bin %d %.1f Hz (want %.1f), loudness %.3f, relative %.3f;", rate,
						             law == BinLaw::Log ? "Log" : "Linear", peak, d.bin, d.hz, centre, d.loudness, d.relative );
				}
			const bool rateOk = audio.HostRate() == rate;
			wrong += !rateOk;
			if( !rateOk )
				what += fmt( " SetHostRate( %u ) read the bins at %.0f Hz, not %.0f;", set, audio.HostRate(), rate );
		};
		{
			AudioInput audio;
			audio.hooks = perturb.hooks;
			peaks( audio, 0u, 48000.0 );
		}
		{
			AudioInput audio;
			audio.hooks = perturb.hooks;
			struct Axis
			{
				unsigned int set;
				double rate;
			};
			for( const Axis& axis : { Axis { 44100u, 44100.0 }, Axis { 7u, 44100.0 }, Axis { 48000u, 48000.0 }, Axis { 96000u, 96000.0 } } )
			{
				audio.SetHostRate( axis.set );
				peaks( audio, axis.set, axis.rate );
			}
		}
		Check( wrong == 0, fmt( "the host FFT, bin 0 loudest: the note is the strongest other bin, at its centre under each Bin Law on the host's axis "
		                        "(48 kHz before any SetHostRate, then 44.1, 48 and 96 kHz as set; a rate of 7 Hz ignored), its value the loudness "
		                        "(%d wrong:%s)",
		                        wrong, what.empty() ? " none" : what.c_str() ) );
	}
	{
		//A sine, a step down 20 dB, silence: the frequency from the pitch, the
		//loudness from the band envelopes, the running peak's release.
		AudioInput audio;
		audio.hooks = perturb.hooks;
		const int rate = 48000, hop = 800;//60 updates a second
		const std::vector< float > tone = sine( 440.0, -20.0, rate, 2.0 ), quiet = sine( 440.0, -40.0, rate, 2.0 );
		const std::vector< float > hush( static_cast< size_t >( rate ) * 2, 0.0f );
		double t = 0.0, noteOmega = 0.0;
		Drive d;
		auto run = [ & ]( const std::vector< float >& clip, double seconds ) {
			for( int i = 0; i < static_cast< int >( seconds * rate ) / hop; ++i )
			{
				audio.Feed( &clip[ static_cast< size_t >( i % ( clip.size() / 2 / hop ) ) * hop * 2 ], hop, rate, 2 );
				t += static_cast< double >( hop ) / rate;
				d = audio.Update( t, nullptr, 0, 0.8, BinLaw::Linear );
				if( d.note )
					noteOmega = d.omega;
			}
		};
		run( tone, 1.0 );
		const double loudDb = 20.0 * std::log10( d.loudness ), toneLoud = d.loudness, stepAt = t;
		Check( d.note && d.fromDevice && std::fabs( d.hz / 440.0 - 1.0 ) < 0.002 && std::fabs( d.omega / ( 2.0 * kPi * d.hz ) - 1.0 ) < 1e-12
		           && std::fabs( loudDb + 20.0 ) < 0.5 && d.relative > 0.99 && d.relative <= 1.0,
		       fmt( "a 440 Hz sine at -20 dBFS through feed(): pitch %.2f Hz, w = 2 pi x %.4f, loudness %.2f dBFS (the band envelopes' RMS), relative %.4f",
		            d.hz, d.omega / ( 2.0 * kPi ), loudDb, d.relative ) );
		run( quiet, 2.0 );
		//8 s, -40 dBFS: the spec's constants, written out (not Audio.h's).
		const double expected = d.loudness / std::max( { toneLoud * std::exp( -( t - stepAt ) / 8.0 ), d.loudness, 0.01 } );
		Check( std::fabs( d.relative / expected - 1.0 ) < 0.02 && std::fabs( d.omega / ( 2.0 * kPi * 440.0 ) - 1.0 ) < 0.002,
		       fmt( "20 dB down for 2 s: relative %.4f against loudness / ( peak e^( -2 s / 8 s ) ) = %.4f (a held peak: %.4f); w still 2 pi x %.1f",
		            d.relative, expected, d.loudness / toneLoud, d.omega / ( 2.0 * kPi ) ) );
		run( hush, 1.0 );
		Check( !d.note && d.relative == 0.0 && d.omega == noteOmega,
		       fmt( "then 1 s of silence: no note, loudness %.2g (under the -60 dBFS gate), relative %.3g, w held at the last voiced pitch's, "
		            "2 pi x %.2f Hz (the tone's tail in the 75 ms window still voices a frame or two)",
		            d.loudness, d.relative, d.omega / ( 2.0 * kPi ) ) );
	}
	{
		//No pitch yet: the strongest partial stands in, once; a pitch replaces
		//it; a later unvoiced stretch holds the pitch.
		AudioInput audio;
		audio.hooks = perturb.hooks;
		const int rate = 48000, hop = 800;
		std::vector< float > chord = sine( 440.0, -26.0, rate, 1.0 );
		const std::vector< float > upper = sine( 623.0, -20.0, rate, 1.0 ), held = sine( 330.0, -20.0, rate, 1.0 );
		for( size_t i = 0; i < chord.size(); ++i )
			chord[ i ] += upper[ i ];
		double t = 0.0;
		Drive d;
		bool everNote = false;
		auto run = [ & ]( const std::vector< float >& clip, double clarity ) {
			everNote = false;
			for( int i = 0; i < rate / hop; ++i )
			{
				audio.Feed( &clip[ static_cast< size_t >( i ) * hop * 2 ], hop, rate, 2 );
				t += static_cast< double >( hop ) / rate;
				d = audio.Update( t, nullptr, 0, clarity, BinLaw::Linear );
				everNote = everNote || d.note;
			}
		};
		run( chord, 0.99 );
		const bool seeded = !everNote && std::fabs( d.omega / ( 2.0 * kPi * 623.0 ) - 1.0 ) < 0.005;
		const double seedHz = d.omega / ( 2.0 * kPi );
		run( held, 0.8 );
		const bool pitched = d.note && std::fabs( d.omega / ( 2.0 * kPi * 330.0 ) - 1.0 ) < 0.002;
		const double pitchHz = d.omega / ( 2.0 * kPi );
		run( chord, 0.99 );
		const bool kept = !everNote && std::fabs( d.omega / ( 2.0 * kPi * 330.0 ) - 1.0 ) < 0.002;
		Check( seeded && pitched && kept, fmt( "440 + 623 Hz at clarity 0.99 (unvoiced), nothing before: the louder partial, %.2f Hz; a 330 Hz sine then sets "
		                                       "%.2f Hz; the unvoiced pair again keeps it: %.2f Hz",
		                                       seedHz, pitchHz, d.omega / ( 2.0 * kPi ) ) );
	}
	return Verdict();
}

//===========================================================================
// --hostfft (GL): the plate's drive on Resolume's own FFT, through the
// plugin. A sine becomes the 64 bins Arena 7.29.0 was measured to send
// (earshot's AGENTS.md, "Resolume's 64 bins, measured") and is handed to the
// plugin as its FF_USAGE_FFT buffer, element by element, as a host does, with
// the host's rate by SetSampleRate; nothing goes through feed(). v0.1.0 read
// the bins at 44.1 kHz whatever the host ran at: against Arena's 48 kHz
// engine every note 1.47 semitones flat (x 0.919).
//===========================================================================

/// Arena's FFT buffer as measured (snowglobe's sgtest --hostfft, the same
/// model): every 2048 samples of the engine's rate, a 2048-point Hann-windowed
/// DFT of the newest 2048 samples of the mono sum, and bin k the sum of
/// |X_i| / 2N over the 16 fine bins 16k .. 16k + 15. So linear, fs / 128 a
/// bin (375 Hz at 48 kHz) to fs / 2, a magnitude, a sine of amplitude A
/// reading about A / 4 in its bin. Held between updates, as Arena holds it.
struct HostSpectrum
{
	static constexpr int kN = 2048;
	int rate;
	std::function< double( double ) > signal;
	std::vector< float > bins = std::vector< float >( 64, 0.0f );
	int64_t updated           = -1;

	HostSpectrum( int r, std::function< double( double ) > s ) : rate( r ), signal( std::move( s ) ) {}

	const std::vector< float >& at( double t )
	{
		const int64_t u = static_cast< int64_t >( std::floor( t * rate / kN + 1e-9 ) );
		if( u == updated )
			return bins;
		updated = u;
		std::vector< std::complex< double > > x( kN );
		for( int n = 0; n < kN; ++n )
		{
			const int64_t sample = u * kN - kN + n;
			const double hann    = 0.5 - 0.5 * std::cos( 2.0 * kPi * n / kN );
			x[ static_cast< size_t >( n ) ] = sample < 0 ? 0.0 : hann * signal( static_cast< double >( sample ) / rate );
		}
		//Radix-2, in place.
		for( int i = 1, j = 0; i < kN; ++i )
		{
			int bit = kN >> 1;
			for( ; j & bit; bit >>= 1 )
				j ^= bit;
			j ^= bit;
			if( i < j )
				std::swap( x[ static_cast< size_t >( i ) ], x[ static_cast< size_t >( j ) ] );
		}
		for( int len = 2; len <= kN; len <<= 1 )
		{
			const std::complex< double > w = std::polar( 1.0, -2.0 * kPi / len );
			for( int i = 0; i < kN; i += len )
			{
				std::complex< double > wk( 1.0, 0.0 );
				for( int k = 0; k < len / 2; ++k )
				{
					const auto a = x[ static_cast< size_t >( i + k ) ], b = x[ static_cast< size_t >( i + k + len / 2 ) ] * wk;
					x[ static_cast< size_t >( i + k ) ]           = a + b;
					x[ static_cast< size_t >( i + k + len / 2 ) ] = a - b;
					wk *= w;
				}
			}
		}
		for( int k = 0; k < 64; ++k )
		{
			double sum = 0.0;
			for( int i = 16 * k; i < 16 * k + 16; ++i )
				sum += std::abs( x[ static_cast< size_t >( i ) ] );
			bins[ static_cast< size_t >( k ) ] = static_cast< float >( sum / ( 2.0 * kN ) );
		}
		return bins;
	}
};

int runHostFft( const Perturb& perturb )
{
	std::printf( "\n=== hostfft: the drive on Resolume's FFT alone (the bins as Arena 7.29.0 sends them, nothing fed, the host's rate by SetSampleRate): "
	             "a sine at a bin's centre drives the plate at its own pitch, at 48 and at 44.1 kHz; any other sine at the centre of the bin "
	             "holding it, never more than half a bin off; over a louder bass in bin 0; with no rate sent, 48 kHz; Bin Law's default is the "
	             "measured Linear\n" );
	const Raster raster = kRasters.back();
	//A -12 dBFS sine (amplitude 0.25, about 0.06 in its bin) over a 49 Hz bass
	//at -6 dBFS in bin 0, twice as loud there: music's bin 0 is its loudest.
	auto toneOverBass = []( double hz ) {
		return [ hz ]( double t ) { return 0.25 * std::sin( 2.0 * kPi * hz * t ) + 0.5 * std::sin( 2.0 * kPi * 49.0 * t ); };
	};
	struct Tone
	{
		double hz;
		bool centred;///< at a bin's centre: the drive must be the tone's pitch
	};
	//The bin centres ( j + 0.5 ) fs / 128 for j = 1, 3, 8, 20, 45, and A4, A5, A6
	//and C8, which sit inside bins (none within two fine bins of an edge at
	//either rate).
	auto tonesAt = []( int rate ) {
		std::vector< Tone > tones;
		for( int j : { 1, 3, 8, 20, 45 } )
			tones.push_back( { ( j + 0.5 ) * rate / 128.0, true } );
		for( double hz : { 440.0, 880.0, 1760.0, 4186.01 } )
			tones.push_back( { hz, false } );
		return tones;
	};
	//30 frames a tone at 60 fps: the spectrum is held 43 ms, so a new tone is
	//whole in the buffer after two updates (85 ms) and the last frame reads it.
	auto drive = [ & ]( Rig& rig, int rate, const Tone& tone ) {
		HostSpectrum host( rate, toneOverBass( tone.hz ) );
		const double start = rig.Seconds( rig.frame );
		rig.beforeFrame    = [ &rig, &host, start ]( int f ) {
			const std::vector< float >& b = host.at( rig.Seconds( f ) - start );
			const unsigned int index      = static_cast< unsigned int >( rig.plugin.HostIndexOf( PT_AUDIO ) );
			for( unsigned int j = 0; j < 64; ++j )
				rig.plugin.SetParamElementValue( index, j, b[ j ] );
		};
		const bool ok   = rig.Render( 30 );
		rig.beforeFrame = nullptr;
		return ok;
	};
	struct Pass
	{
		int rate;      ///< the engine rate the bins are made at
		bool sendRate; ///< SetSampleRate( rate ) first (false: the host never says)
		bool over;
	};
	//One instance per plugin: the source at 48 kHz then 44.1 kHz, the Over at
	//44.1 kHz from its first frame then 48 kHz (the rate taken before the
	//first frame, and followed when it changes), and an instance the host
	//never tells.
	const Pass passes[] = { { 48000, true, false }, { 44100, true, false }, { 44100, true, true }, { 48000, true, true }, { 48000, false, false } };
	std::unique_ptr< Rig > rig;
	bool lastOver = false;
	for( const Pass& pass : passes )
	{
		if( !rig || pass.over != lastOver || !pass.sendRate )
		{
			rig      = std::make_unique< Rig >( pass.over );
			lastOver = pass.over;
			if( !startRig( *rig, raster.w, raster.h, perturb ) )
				return 1;
			rig->Set( PT_DEVICE, 0.0f );//"Resolume FFT (no pitch)", the default
			rig->Set( PT_BIN_LAW, static_cast< float >( BinLaw::Linear ) );
		}
		if( pass.sendRate )
			rig->plugin.SetSampleRate( static_cast< unsigned int >( pass.rate ) );
		const double binHz = pass.rate / 128.0;
		int wrong = 0, worstOffCentre = 0;
		double worstSemis = 0.0, worstOff = 0.0;
		std::string what;
		for( const Tone& tone : tonesAt( pass.rate ) )
		{
			if( !drive( *rig, pass.rate, tone ) )
				return 1;
			const FrameData& fd = rig->plugin.LastFrame();
			const int holds     = static_cast< int >( std::floor( tone.hz / binHz ) );
			const double centre = ( holds + 0.5 ) * binHz;//the Linear centre, written out
			const double semis  = 12.0 * std::log2( fd.drive.hz / tone.hz );
			const bool ok       = fd.drive.note && !fd.drive.fromDevice && fd.drive.bin == holds && std::fabs( fd.drive.hz / centre - 1.0 ) < 1e-9
			                && std::fabs( fd.drive.omega / ( 2.0 * kPi * fd.drive.hz ) - 1.0 ) < 1e-12 && fd.omega == fd.drive.omega && fd.drive.F0 > 0.0
			                && ( !tone.centred || std::fabs( fd.drive.hz / tone.hz - 1.0 ) < 1e-9 ) && std::fabs( fd.drive.hz - tone.hz ) <= 0.5 * binHz + 1e-9;
			wrong += !ok;
			if( tone.centred )
				worstSemis = std::max( worstSemis, std::fabs( semis ) );
			else if( std::fabs( fd.drive.hz - tone.hz ) > worstOff )
			{
				worstOff       = std::fabs( fd.drive.hz - tone.hz );
				worstOffCentre = holds;
			}
			if( !ok )
				what += fmt( " %.2f Hz -> bin %d at %.2f Hz (%+.2f semitones; want bin %d at %.2f), F0 %.3g N;", tone.hz, fd.drive.bin, fd.drive.hz, semis, holds,
				             centre, fd.drive.F0 );
		}
		const double read = rig->plugin.Audio().HostRate();
		const std::string who = fmt( "%s, %s %d Hz", pass.over ? "SW Eido Over" : "SW Eidophone", pass.sendRate ? "the host says" : "the host never says; the bins at", pass.rate );
		Check( wrong == 0 && read == pass.rate,
		       fmt( "%s: the bins read at %.0f Hz; 5 sines at bin centres drive the plate at their own pitch (worst %.2g semitones), 4 others at "
		            "the centre of the bin holding them (worst %.1f Hz off, bin %d, half a bin is %.1f), w = 2 pi f, the grains' w the drive's, "
		            "over a louder 49 Hz bass in bin 0 (%d wrong:%s)",
		            who.c_str(), read, worstSemis, worstOff, worstOffCentre, 0.5 * binHz, wrong, what.empty() ? " none" : what.c_str() ) );
	}
	//A rate no engine runs at is not one; and Bin Law's default is Linear, the
	//law Arena was measured to use (Log puts the same tone elsewhere).
	{
		Rig fresh( false );
		if( !startRig( fresh, raster.w, raster.h, perturb ) )
			return 1;
		fresh.plugin.SetSampleRate( 0u );
		const float lawDefault = fresh.plugin.GetById( PT_BIN_LAW );
		fresh.Set( PT_DEVICE, 0.0f );
		const Tone tone = { 3.5 * 48000.0 / 128.0, true };
		if( !drive( fresh, 48000, tone ) )
			return 1;
		const double linearHz = fresh.plugin.LastFrame().drive.hz, read = fresh.plugin.Audio().HostRate();
		fresh.Set( PT_BIN_LAW, static_cast< float >( BinLaw::Log ) );
		if( !drive( fresh, 48000, tone ) )
			return 1;
		const double logHz = fresh.plugin.LastFrame().drive.hz;
		Check( read == 48000.0 && lawDefault == static_cast< float >( BinLaw::Linear ) && std::fabs( linearHz / tone.hz - 1.0 ) < 1e-9
		           && std::fabs( logHz / ( 20.0 * std::pow( 24000.0 / 20.0, 3.5 / 64.0 ) ) - 1.0 ) < 1e-9,
		       fmt( "a host rate of 0: the bins stay at %.0f Hz (want 48000); Bin Law's default is %s, the measured law: a %.1f Hz sine drives at "
		            "%.1f Hz under it, at %.1f Hz under Log (%+.1f semitones; Log is kept for unmeasured hosts)",
		            read, lawDefault == static_cast< float >( BinLaw::Linear ) ? "Linear" : "NOT Linear", tone.hz, linearHz, logHz,
		            12.0 * std::log2( logHz / tone.hz ) ) );
	}
	return Verdict();
}

//===========================================================================
// --device (GL): the menu, opening off the render thread, the capture-state
// cue, a device remembered by name.
//===========================================================================

/// The share of the cue band at a pixel: the final pass's formula, in double.
double cueBeta( int px, int py, int W, int H, double plateScale, Shape shape, double cueWidth )
{
	const double qx = px + 0.5 - 0.5 * W, qy = py + 0.5 - 0.5 * H;
	const double ux = qx / plateScale, uy = qy / plateScale;
	const double edge    = shape == Shape::Circle ? std::hypot( ux, uy ) : std::max( std::fabs( ux ), std::fabs( uy ) );
	const double inside  = ( 1.0 - edge ) * plateScale;
	const double mask    = std::clamp( inside + 0.5, 0.0, 1.0 );
	const double closest = std::min( inside, std::min( 0.5 * W - std::fabs( qx ), 0.5 * H - std::fabs( qy ) ) );
	return std::clamp( cueWidth - closest, 0.0, 1.0 ) * mask;
}

/// The worst difference between `cued` and `plain` blended toward `rgb` by
/// strength x beta( pixel ): what the cue must be, at every pixel.
double cueError( const Floats& cued, const Floats& plain, int W, int H, double plateScale, Shape shape, const double rgb[ 3 ], double strength, int* banded = nullptr )
{
	const double cueWidth = std::max( 2.0, 0.012 * H );
	double worst          = 0.0;
	int count             = 0;
	for( int y = 0; y < H; ++y )
		for( int x = 0; x < W; ++x )
		{
			const double b = strength * cueBeta( x, y, W, H, plateScale, shape, cueWidth );
			count += b > 0.5 * strength;
			const size_t i = ( static_cast< size_t >( y ) * W + x ) * 4;
			for( int c = 0; c < 3; ++c )
				worst = std::max( worst, std::fabs( cued[ i + c ] - ( plain[ i + c ] * ( 1.0 - b ) + rgb[ c ] * b ) ) );
		}
	if( banded )
		*banded = count;
	return worst;
}

int runDevice( const Perturb& perturb )
{
	std::printf( "\n=== device: the menu by name, every open off the render thread, Missing amber and Silent red on the rim, a device found again by "
	             "name on Rescan\n" );
	for( const Raster& raster : kRasters )
	{
		{
			std::lock_guard< std::mutex > guard( g_script.lock );
			g_script.present = { "Bench Mic", "Dante 1-2", g_script.silentName };
		}
		//A is the instance under test; B never leaves the host FFT, so it is A's
		//picture without a cue (both undriven: Gain at its floor, nothing hops).
		Rig a, b;
		if( !startRig( a, raster.w, raster.h, perturb ) || !startRig( b, raster.w, raster.h, Perturb {} ) )
			return 1;
		for( Rig* r : { &a, &b } )
		{
			r->Set( PT_GAIN, 0.0f );
			r->Set( PT_VIEW, static_cast< float >( View::Fit ) );
		}
		if( !a.Render( 2 ) || !b.Render( 2 ) )
			return 1;
		const Floats plain    = b.Output();
		const unsigned host   = static_cast< unsigned >( a.plugin.HostIndexOf( PT_DEVICE ) );
		const unsigned rescan = PT_RESCAN;
		auto valueOf = [ & ]( const char* label ) -> float {
			for( unsigned e = 0; e < a.plugin.GetNumParamElements( host ); ++e )
				if( std::string( a.plugin.GetParamElementName( host, e ) ) == label )
					return elementValue( a.plugin, host, e );
			return -1000.0f;
		};
		auto state = [ & ]() { return a.plugin.Audio().State(); };
		const double scale = a.plugin.LastFrame().plateScale;
		double worstFrame  = 0.0;
		auto timed         = [ & ]( int n ) {
            for( int i = 0; i < n; ++i )
            {
                const double t0 = wallNow();
                if( !a.Render( 1 ) )
                    return false;
                worstFrame = std::max( worstFrame, wallNow() - t0 );
            }
            return true;
		};

		//The menu as a host lists it.
		const bool menu = a.plugin.GetNumParamElements( host ) == static_cast< unsigned >( kDeviceElements ) && valueOf( AudioInput::HostLabel() ) == 0.0f
		                  && valueOf( "Bench Mic" ) == earshot::deviceKey( "Bench Mic" ) && valueOf( "Dante 1-2" ) == earshot::deviceKey( "Dante 1-2" )
		                  && valueOf( "System default" ) == earshot::deviceKey( earshot::kDefaultDevice );

		//Open a present device: Running, no cue.
		a.plugin.SetFloatParameter( host, valueOf( "Dante 1-2" ) );
		const bool running = renderUntil( a, [ & ] { return state() == earshot::CaptureState::Running; }, 3.0 );
		const bool clean   = a.Output() == plain;

		//A slow device: the frame after choosing it returns while it is still opening.
		{
			std::lock_guard< std::mutex > guard( g_script.lock );
			g_script.present.push_back( g_script.slowName );
		}
		if( !a.Press( rescan ) )
			return 1;
		const bool listed = valueOf( g_script.slowName.c_str() ) == earshot::deviceKey( g_script.slowName.c_str() ) && a.plugin.Audio().Requested() == "Dante 1-2";
		a.plugin.SetFloatParameter( host, valueOf( g_script.slowName.c_str() ) );
		const double t0 = wallNow();
		if( !timed( 1 ) )
			return 1;
		const double firstFrame   = wallNow() - t0;
		const bool stillOpening   = state() == earshot::CaptureState::Opening;
		const bool openedLater    = renderUntil( a, [ & ] { return state() == earshot::CaptureState::Running; }, 3.0 );
		Check( menu && running && clean && listed, fmt( "%dx%d: %d elements, the host FFT 0, each device deviceKey( name ); Dante 1-2 Running (%s) with no cue (%s); "
		                                               "Rescan lists the new device and keeps the open one (%s)",
		                                               raster.w, raster.h, a.plugin.GetNumParamElements( host ), running ? "yes" : "NO",
		                                               clean ? "yes" : "NO", listed ? "yes" : "NO" ) );
		Check( stillOpening && openedLater, fmt( "%dx%d: a device that takes %.1f s to open: the frame after choosing it took %.1f ms and found it still Opening "
		                                         "(%s); Running later (%s)",
		                                         raster.w, raster.h, g_script.openDelay, 1000.0 * firstFrame, stillOpening ? "yes" : "NO", openedLater ? "yes" : "NO" ) );

		//A composition's device that is not plugged in: Missing, amber on the rim.
		const char* away = "Studio Interface";
		a.plugin.SetFloatParameter( host, earshot::deviceKey( away ) );
		const bool missing = renderUntil( a, [ & ] { return state() == earshot::CaptureState::Missing; }, 3.0 );
		if( !timed( 1 ) )
			return 1;
		const double amber[ 3 ] = { 1.0, 0.62, 0.10 };
		int band                = 0;
		const double amberError = cueError( a.Output(), plain, raster.w, raster.h, scale, Shape::Square, amber, 0.85, &band );
		Check( missing && amberError < 1e-4 && band > 0, fmt( "%dx%d: '%s' saved but absent: Missing (%s), the frame is the plain one blended to amber by 0.85 "
		                                                    "x the band (%d px) within %.1e",
		                                                    raster.w, raster.h, away, missing ? "yes" : "NO", band, amberError ) );

		//It is plugged in, and Rescan finds it by name.
		{
			std::lock_guard< std::mutex > guard( g_script.lock );
			g_script.present.push_back( away );
		}
		if( !a.Press( rescan ) )
			return 1;
		const bool found = renderUntil( a, [ & ] { return state() == earshot::CaptureState::Running; }, 3.0 );
		const bool named = a.plugin.Audio().Requested() == away;
		if( !timed( 1 ) )
			return 1;
		const bool gone = a.Output() == plain;
		Check( found && named && gone, fmt( "%dx%d: plugged in, Rescan opens it by name (%s, requested '%s') and the cue goes (%s)", raster.w, raster.h,
		                                    found ? "Running" : "NOT running", a.plugin.Audio().Requested().c_str(), gone ? "yes" : "NO" ) );

		//A device that opens and delivers only zeros: Silent, pulsing red.
		a.plugin.SetFloatParameter( host, valueOf( g_script.silentName.c_str() ) );
		const bool silent = renderUntil( a, [ & ] { return state() == earshot::CaptureState::Silent; }, 3.0 );
		double redError   = 1.0;
		double pulse      = 0.0;
		int redBand       = 0;
		for( int i = 0; i < 3 && silent; ++i )
		{
			if( !timed( 7 ) )
				return 1;
			const double now = a.plugin.HostNow();
			pulse            = 0.35 + 0.65 * ( 0.5 + 0.5 * std::cos( kPi * now ) );
			const double red[ 3 ] = { 1.0, 0.12, 0.08 };
			redError         = std::max( i == 0 ? 0.0 : redError, cueError( a.Output(), plain, raster.w, raster.h, scale, Shape::Square, red, pulse, &redBand ) );
		}
		Check( silent && redError < 1e-4 && redBand > 0, fmt( "%dx%d: '%s' open and dead: Silent (%s), red at 0.35 + 0.65 x the 2 s pulse (last %.2f) on the band "
		                                                    "within %.1e",
		                                                    raster.w, raster.h, g_script.silentName.c_str(), silent ? "yes" : "NO", pulse, redError ) );
		a.plugin.SetFloatParameter( host, 0.0f );
		const bool closed = renderUntil( a, [ & ] { return state() == earshot::CaptureState::Closed; }, 3.0 );
		Check( closed && a.Output() == plain && worstFrame < 0.25, fmt( "%dx%d: back to the host FFT: Closed, no cue; the slowest timed frame %.1f ms (an open "
		                                                                "takes %.0f ms)",
		                                                                raster.w, raster.h, 1000.0 * worstFrame, 1000.0 * g_script.openDelay ) );
	}
	{
		std::lock_guard< std::mutex > guard( g_script.lock );
		g_script.present = { "Bench Mic", "Dante 1-2" };
	}
	return Verdict();
}

//===========================================================================
// --select (GL): a sine at a mode's frequency, through earshot, selects it.
//===========================================================================
int runSelect( const Perturb& perturb )
{
	std::printf( "\n=== select: a sine at a mode's frequency, fed through earshot's pitch, puts most of the plate's energy in that mode\n" );
	const double kQ = 20.0, kDriveAt = 0.5;
	for( const Raster& raster : kRasters )
	{
		//The candidates: well coupled at x_d, 80-1500 Hz, no other coupled mode
		//within 10 % (the half-power band at Q 20 is 5 %).
		std::vector< int > picks;
		std::vector< double > omegas;
		{
			Rig probe;
			if( !startRig( probe, raster.w, raster.h, Perturb {} ) )
				return 1;
			probe.Set( PT_DAMPING, ParamFromDamping( kQ ) );
			probe.Set( PT_DRIVE_AT, ParamFromDriveAt( kDriveAt ) );
			if( !probe.Render( 1 ) )
				return 1;
			const Plate& p = probe.plugin.PlateState();
			std::vector< int > ok;
			for( int k = 2; k < p.Count(); ++k )
			{
				const double hz = p.Omega( k ) / ( 2.0 * kPi );
				if( std::fabs( p.Coupling( k ) ) < 0.3 || hz < 80.0 || hz > 1500.0 )
					continue;
				bool alone = true;
				for( int j = 2; j < p.Count(); ++j )
					if( j != k && std::fabs( p.Coupling( j ) ) > 0.1 && std::fabs( std::log( p.Omega( j ) / p.Omega( k ) ) ) < std::log( 1.10 ) )
						alone = false;
				if( alone )
					ok.push_back( k );
			}
			if( !ok.empty() )
			{
				picks.push_back( ok.front() );
				if( ok.size() > 2 )
					picks.push_back( ok[ ok.size() / 2 ] );
				if( ok.size() > 1 )
					picks.push_back( ok.back() );
			}
			for( int k : picks )
				omegas.push_back( p.Omega( k ) );
		}
		Check( picks.size() >= 3, fmt( "%dx%d: %zu well separated, well coupled modes to try", raster.w, raster.h, picks.size() ) );
		for( size_t i = 0; i < picks.size(); ++i )
		{
			const int k     = picks[ i ];
			const double hz = omegas[ i ] / ( 2.0 * kPi );
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.Set( PT_DAMPING, ParamFromDamping( kQ ) );
			rig.Set( PT_DRIVE_AT, ParamFromDriveAt( kDriveAt ) );
			Wav tone;
			tone.rate     = 48000;
			tone.channels = 2;
			tone.samples  = sine( hz, -12.0, tone.rate, 3.0 );
			Feeder feeder;
			feeder.wav      = &tone;
			rig.beforeFrame = [ & ]( int f ) { feeder.Before( rig.plugin, f, rig.fps ); };
			if( !rig.Render( 150 ) )
				return 1;
			const Plate& p     = rig.plugin.PlateState();
			const double drive = rig.plugin.LastFrame().drive.omega;
			double total = 0.0, best = 0.0;
			int argmax = -1;
			for( int j = 2; j < p.Count(); ++j )
			{
				const double e = std::norm( p.AnalyticDisplacement( j ) ) * 0.5 * ( p.Omega( j ) * p.Omega( j ) + drive * drive );
				total += e;
				if( e > best )
				{
					best   = e;
					argmax = j;
				}
			}
			const double share = total > 0.0 ? std::norm( p.AnalyticDisplacement( k ) ) * 0.5 * ( p.Omega( k ) * p.Omega( k ) + drive * drive ) / total : 0.0;
			const double pitch = rig.plugin.LastFrame().drive.hz;
			Check( argmax == k && share > 0.5 && rig.plugin.LastFrame().drive.fromDevice,
			       fmt( "%dx%d: mode %d (%s, %.2f Hz): a sine there, pitched %.2f Hz (%+.2f%%), gives it %.1f%% of the elastic energy (largest: mode %d)",
			            raster.w, raster.h, k, rig.plugin.ModeTable()->modes[ static_cast< size_t >( k ) ].cls == modes::kSS ? "SS"
			                                   : rig.plugin.ModeTable()->modes[ static_cast< size_t >( k ) ].cls == modes::kSA ? "SA" : "OE",
			            hz, pitch, 100.0 * ( pitch / hz - 1.0 ), 100.0 * share, argmax ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --threshold (GL): no grain of sand moves unless the plate beats g.
//===========================================================================
int runThreshold( const Perturb& perturb )
{
	std::printf( "\n=== threshold: over 20 substeps of a complex field, every grain where |A| < g is bit for bit what it was; every grain where |A| > g "
	             "moved\n" );
	const auto table = aluminiumTable( Shape::Square );
	const int ss = findMode( *table, modes::kSS ), oe = findMode( *table, modes::kOE );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_GRAIN, static_cast< float >( Grain::Sand ) );
		rig.Set( PT_AMOUNT, 1.0f );
		//Re A on a symmetric mode, Im A on an odd one: |A| is not |Re A|, and
		//the gate must be on |A|.
		setPure( rig, ss, 2.5 * kG, 2.0 * kPi * 200.0, oe, 0.0, 1.5 * kG );
		rig.plugin.SetSubstepsForTest( 0 );
		if( !rig.Render( 1 ) )
			return 1;
		const Floats field = rig.Field();
		const int count    = rig.plugin.LastFrame().grains;
		rig.plugin.SetSubstepsForTest( 1 );
		long held = 0, thrown = 0, band = 0, heldMoved = 0, thrownStill = 0;
		Floats before = rig.Grains();
		for( int step = 0; step < 20; ++step )
		{
			if( !rig.Render( 1 ) )
				return 1;
			const Floats after = rig.Grains();
			for( int i = 0; i < count; ++i )
			{
				const size_t t = static_cast< size_t >( i );
				const double b = fieldAt( field, before[ 4 * t ], before[ 4 * t + 1 ] ).a / kG;
				if( b < 1.0 - 1e-5 )
				{
					++held;
					heldMoved += !sameTexel( before, after, t );
				}
				else if( b > 1.0 + 1e-5 )
				{
					++thrown;
					thrownStill += before[ 4 * t ] == after[ 4 * t ] && before[ 4 * t + 1 ] == after[ 4 * t + 1 ];
				}
				else
					++band;
			}
			before = after;
		}
		Check( heldMoved == 0 && held > 100000, fmt( "%dx%d: %ld grain-substeps under g (|A| from the lattice in double, 1e-5 margin): %ld changed in any bit", raster.w,
		                                             raster.h, held, heldMoved ) );
		Check( thrownStill == 0 && thrown > 100000, fmt( "%dx%d: %ld grain-substeps over g: %ld stayed put; %ld inside the margin, not judged", raster.w, raster.h,
		                                                 thrown, thrownStill, band ) );
	}
	return Verdict();
}

//===========================================================================
// --hop (GL): one substep's displacements against the hop's closed forms.
//===========================================================================
int runHop( const Perturb& perturb )
{
	std::printf( "\n=== hop: a thrown grain's step is -( H v / 2 ) dt down grad a plus a Gaussian of variance 2 D dt a side, v = sqrt( a^2 - g^2 ) / w, "
	             "D = H^2 v^3 / 8g\n" );
	const auto table = aluminiumTable( Shape::Square );
	const int ss     = findMode( *table, modes::kSS );
	const double w   = 2.0 * kPi * 200.0;
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_GRAIN, static_cast< float >( Grain::Sand ) );
		rig.Set( PT_AMOUNT, 1.0f );
		rig.Set( PT_HOP, ParamFromHop( 1.0 ) );
		setPure( rig, ss, 2.0 * kG, w );
		rig.plugin.SetSubstepsForTest( 0 );
		if( !rig.Render( 1 ) )
			return 1;
		const Floats field = rig.Field();
		const double H     = HopFromParam( rig.plugin.GetById( PT_HOP ) );
		const double L     = rig.plugin.LastFrame().halfSize;
		const double dt    = static_cast< float >( shaders::kSubstep );
		double sumN = 0.0, sumT = 0.0, sumSq = 0.0, biggest = 0.0;
		long n = 0;
		rig.plugin.SetSubstepsForTest( 1 );
		//Four substeps (four seeds), each judged from where it began.
		for( int step = 0; step < 4; ++step )
		{
			double dbgN = 0.0, dbgT = 0.0;
			long dbgC   = 0;
			const Floats before = rig.Grains();
			if( !rig.Render( 1 ) )
				return 1;
			const Floats after = rig.Grains();
			for( int i = 0; i < rig.plugin.LastFrame().grains; ++i )
			{
				const size_t t = static_cast< size_t >( i );
				const double x = before[ 4 * t ], y = before[ 4 * t + 1 ];
				if( std::fabs( x ) > 0.99 || std::fabs( y ) > 0.99 )
					continue;//a reflection is not a hop
				const Accel A   = fieldAt( field, x, y );
				const double b  = A.a / kG;
				const double gn = std::hypot( A.gx, A.gy );
				if( b < 1.1 || gn == 0.0 )
					continue;
				const double v     = std::sqrt( A.a * A.a - kG * kG ) / w;
				const double D     = H * H * v * v * v / ( 8.0 * kG );
				const double nx = A.gx / gn, ny = A.gy / gn;
				const double drift = -0.5 * H * v * dt / L;//plate units, along n
				const double sigma = std::sqrt( 2.0 * D * dt ) / L;
				const double dx = after[ 4 * t ] - x, dy = after[ 4 * t + 1 ] - y;
				biggest = std::max( biggest, std::hypot( dx, dy ) );
				const double rx = dx - drift * nx, ry = dy - drift * ny;
				const double zn = ( rx * nx + ry * ny ) / sigma, zt = ( -rx * ny + ry * nx ) / sigma;
				sumN += zn;
				sumT += zt;
				sumSq += zn * zn + zt * zt;
				++n;
				dbgN += zn;
				dbgT += zt;
				++dbgC;
			}
			if( std::getenv( "ED_DEBUG" ) )
				std::printf( "    step %d (Step %u): %ld grains, mean along %+.4f, across %+.4f\n", step, rig.plugin.LastFrame().firstStep, dbgC, dbgN / dbgC, dbgT / dbgC );
		}
		const double meanN = sumN / n, meanT = sumT / n, var = sumSq / ( 2.0 * n );
		const double zMeanN = meanN * std::sqrt( static_cast< double >( n ) ), zMeanT = meanT * std::sqrt( static_cast< double >( n ) );
		const double zVar = ( var - 1.0 ) * std::sqrt( static_cast< double >( n ) );//var of a sample variance of 2n is 1/n
		Check( n > 200000 && std::fabs( zMeanN ) < 4.0 && std::fabs( zMeanT ) < 4.0,
		       fmt( "%dx%d: %ld thrown grain-substeps (a/g > 1.1, off the rim; 4 substeps): the residual after the drift has mean %+.4f sigma down the gradient (z %+.1f) and "
		            "%+.4f across (z %+.1f)",
		            raster.w, raster.h, n, meanN, zMeanN, meanT, zMeanT ) );
		Check( std::fabs( zVar ) < 4.0 && biggest < 1.0 / shaders::kFieldSize, fmt( "%dx%d: its variance %.4f sigma^2 (z %+.1f); the largest step %.2e, under "
		                                                                           "the half-texel clamp %.2e (none clamped)",
		                                                                           raster.w, raster.h, var, zVar, biggest, 1.0 / shaders::kFieldSize ) );
	}
	return Verdict();
}

//===========================================================================
// --nodal (GL): settled sand lies on the analytic nodal lines.
//===========================================================================
/// The zero set of a mode: where W changes sign along the edges of a 1024 x
/// 1024 grid, with |grad W| there, from the exact W in double.
struct ZeroSet
{
	std::vector< double > x, y, grad;
	static constexpr int B = 64;
	std::vector< std::vector< int > > bucket = std::vector< std::vector< int > >( B * B );
	int count() const
	{
		return static_cast< int >( x.size() );
	}
};

const ZeroSet& zeroSetOf( const modes::Table& table, int k )
{
	static std::map< std::pair< const modes::Table*, int >, ZeroSet > cache;
	const auto key = std::make_pair( &table, k );
	auto found     = cache.find( key );
	if( found != cache.end() )
		return found->second;
	const modes::Mode& mode = table.modes[ static_cast< size_t >( k ) ];
	const int G = 1024, P = G + 1;
	const double h = 2.0 / G;
	std::vector< double > w( static_cast< size_t >( P ) * P );
	const bool disc = table.shape == Shape::Circle;
	std::vector< std::thread > workers;
	for( int part = 0; part < 4; ++part )
		workers.emplace_back( [ &, part ] {
			for( int j = part; j < P; j += 4 )
				for( int i = 0; i < P; ++i )
				{
					const double x = -1.0 + i * h, y = -1.0 + j * h;
					w[ static_cast< size_t >( j ) * P + i ] = disc && x * x + y * y > 1.0 ? std::nan( "" ) : table.W( mode, x, y );
				}
		} );
	for( auto& t : workers )
		t.join();
	ZeroSet z;
	auto add = [ & ]( double x, double y ) {
		double gx = 0.0, gy = 0.0;
		table.Gradient( mode, x, y, gx, gy );
		z.x.push_back( x );
		z.y.push_back( y );
		z.grad.push_back( std::hypot( gx, gy ) );
		const int bx = std::clamp( static_cast< int >( ( x + 1.0 ) * 0.5 * ZeroSet::B ), 0, ZeroSet::B - 1 );
		const int by = std::clamp( static_cast< int >( ( y + 1.0 ) * 0.5 * ZeroSet::B ), 0, ZeroSet::B - 1 );
		z.bucket[ static_cast< size_t >( by * ZeroSet::B + bx ) ].push_back( z.count() - 1 );
	};
	for( int j = 0; j < P; ++j )
		for( int i = 0; i < P; ++i )
		{
			const double w0 = w[ static_cast< size_t >( j ) * P + i ];
			if( std::isnan( w0 ) )
				continue;
			if( w0 == 0.0 )
				add( -1.0 + i * h, -1.0 + j * h );
			if( i + 1 < P )
			{
				const double w1 = w[ static_cast< size_t >( j ) * P + i + 1 ];
				if( !std::isnan( w1 ) && w0 * w1 < 0.0 )
					add( -1.0 + ( i + w0 / ( w0 - w1 ) ) * h, -1.0 + j * h );
			}
			if( j + 1 < P )
			{
				const double w1 = w[ static_cast< size_t >( j + 1 ) * P + i ];
				if( !std::isnan( w1 ) && w0 * w1 < 0.0 )
					add( -1.0 + i * h, -1.0 + ( j + w0 / ( w0 - w1 ) ) * h );
			}
		}
	return cache.emplace( key, std::move( z ) ).first->second;
}

/// Whether a grain at ( gx, gy ) is on the zero set: within the dead band
/// g / ( a0 |grad W| ) of some point of it, plus `extra`, capped at `cap`.
/// `nearest` gets the distance to the nearest point (up to the search radius).
bool onZeroSet( const ZeroSet& z, double gx, double gy, double bandScale, double extra, double cap, double* nearest )
{
	const double reach = cap + extra;
	const int r        = static_cast< int >( std::ceil( reach / ( 2.0 / ZeroSet::B ) ) ) + 1;
	const int bx = std::clamp( static_cast< int >( ( gx + 1.0 ) * 0.5 * ZeroSet::B ), 0, ZeroSet::B - 1 );
	const int by = std::clamp( static_cast< int >( ( gy + 1.0 ) * 0.5 * ZeroSet::B ), 0, ZeroSet::B - 1 );
	bool ok      = false;
	double best  = 1e300;
	for( int j = std::max( 0, by - r ); j <= std::min( ZeroSet::B - 1, by + r ); ++j )
		for( int i = std::max( 0, bx - r ); i <= std::min( ZeroSet::B - 1, bx + r ); ++i )
			for( int p : z.bucket[ static_cast< size_t >( j * ZeroSet::B + i ) ] )
			{
				const double d = std::hypot( gx - z.x[ static_cast< size_t >( p ) ], gy - z.y[ static_cast< size_t >( p ) ] );
				best           = std::min( best, d );
				const double g = z.grad[ static_cast< size_t >( p ) ];
				const double tol = std::min( cap, ( g > 0.0 ? bandScale / g : cap ) + extra );
				if( d <= tol )
					ok = true;
			}
	if( nearest )
		*nearest = best;
	return ok;
}

/// A grain on the nodal lines: inside the analytic dead band, a0 |W( x )| <= g
/// (5 % slack for the lattice: its bilinear error here is under 1 %), and
/// within 0.1 plate units (13 lattice texels) of the zero set, so a low
/// patch far from any line does not count. Near a line's straight run that is
/// a distance of g / ( a0 |grad W| ); where lines nearly cross, the band is a
/// patch (the square's avoided crossings), and sand fills it, as on a plate.
/// `linear` says whether the grain is also inside the straight-run distance.
bool onNodal( const modes::Table& table, int k, const ZeroSet& z, double x, double y, double a0, double* nearest, bool* linear, double cap = 0.1 )
{
	double d       = 0.0;
	const bool lin = onZeroSet( z, x, y, kG / a0, 2.0 / shaders::kFieldSize + 2.0 / 1024.0, 0.1, &d );
	if( nearest )
		*nearest = d;
	if( linear )
		*linear = lin;
	return a0 * std::fabs( table.W( table.modes[ static_cast< size_t >( k ) ], x, y ) ) <= 1.05 * kG && d <= cap;
}

struct Settled
{
	Floats state;
	int count     = 0;
	double seconds = 0.0;
	int moving    = 0;
	int offPlate  = 0;///< the most grains outside the plate at any readback (the rim reflects: must be 0)
};

/// Grains outside the plate: the square's [ -1, 1 ]^2, or the unit disc.
int offPlateCount( const Floats& state, int count, Shape shape )
{
	int off = 0;
	for( int i = 0; i < count; ++i )
	{
		const double x = state[ 4 * static_cast< size_t >( i ) ], y = state[ 4 * static_cast< size_t >( i ) + 1 ];
		off += shape == Shape::Circle ? x * x + y * y > 1.0 + 1e-6 : std::fabs( x ) > 1.0 || std::fabs( y ) > 1.0;
	}
	return off;
}

/// Pure drive on one mode, 32 substeps a frame, until fewer than 0.5 % of the
/// grains moved over the last 480 substeps (1 s of plate time) or `limit` s.
bool settle( Rig& rig, double limit, Settled& out )
{
	rig.plugin.SetSubstepsForTest( shaders::kMaxSubsteps );
	if( !rig.Render( 1 ) )
		return false;
	out.count   = rig.plugin.LastFrame().grains;
	Floats last = rig.Grains();
	const int block = 480 / shaders::kMaxSubsteps;
	for( int f = 1; f * shaders::kMaxSubsteps * shaders::kSubstep < limit; f += block )
	{
		if( !rig.Render( block ) )
			return false;
		Floats now   = rig.Grains();
		out.moving   = movedCount( last, now, out.count );
		out.offPlate = std::max( out.offPlate, offPlateCount( now, out.count, rig.plugin.ModeTable()->shape ) );
		out.seconds = ( f + block ) * shaders::kMaxSubsteps * shaders::kSubstep;
		last        = std::move( now );
		if( out.moving <= out.count / 200 )
			break;
	}
	out.state = std::move( last );
	return true;
}

int runNodal( const Perturb& perturb )
{
	std::printf( "\n=== nodal: driven in one mode at a0 = 16 g, the settled sand lies in the analytic dead band a0 |W| <= g and within 0.1 of the "
	             "mode's zero set, both from the exact W; the distances in lattice texels\n" );
	struct Case
	{
		Shape shape;
		const char* name;
		int k;
	};
	const auto square = aluminiumTable( Shape::Square ), disc = aluminiumTable( Shape::Circle );
	const Case cases[] = {
		{ Shape::Square, "square, doubly symmetric #2", findMode( *square, modes::kSS, 1 ) },
		{ Shape::Square, "square, odd-even #2", findMode( *square, modes::kOE, 1 ) },
		{ Shape::Circle, "disc, n 0 s 2", findCircle( *disc, 0, 2 ) },
		{ Shape::Circle, "disc, n 2 s 1", findCircle( *disc, 2, 1 ) },
	};
	const double a0 = 16.0 * kG;
	for( const Raster& raster : kRasters )
		for( const Case& c : cases )
		{
			const modes::Table& table = c.shape == Shape::Circle ? *disc : *square;
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.Set( PT_SHAPE, static_cast< float >( c.shape ) );
			rig.Set( PT_GRAIN, static_cast< float >( Grain::Sand ) );
			rig.Set( PT_AMOUNT, ParamFromAmount( 16384 ) );
			setPure( rig, c.k, a0, 2.0 * kPi * 200.0 );
			Settled s;
			if( !settle( rig, 30.0, s ) )
				return 1;
			const ZeroSet& z = zeroSetOf( table, c.k );
			const double texel = 2.0 / shaders::kFieldSize;
			int on = 0, linear = 0;
			int hist[ 4 ] = {};
			for( int i = 0; i < s.count; ++i )
			{
				double d = 0.0;
				bool lin = false;
				on += onNodal( table, c.k, z, s.state[ 4 * static_cast< size_t >( i ) ], s.state[ 4 * static_cast< size_t >( i ) + 1 ], a0, &d, &lin );
				linear += lin;
				hist[ d < texel ? 0 : d < 2 * texel ? 1 : d < 4 * texel ? 2 : 3 ]++;
			}
			const double share = static_cast< double >( on ) / s.count;
			Check( share >= 0.99 && z.count() > 100 && s.offPlate == 0,
			       fmt( "%dx%d: %s (lambda %.2f): %.2f%% of %d grains on the nodal lines after %.1f s (%d still moving, %d ever off the plate), %.1f%% "
			            "inside the straight-run distance; <1 texel %d, 1-2 %d, 2-4 %d, more %d",
			            raster.w, raster.h, c.name, table.modes[ static_cast< size_t >( c.k ) ].paper, 100.0 * share, s.count, s.seconds, s.moving, s.offPlate,
			            100.0 * linear / s.count, hist[ 0 ], hist[ 1 ], hist[ 2 ], hist[ 3 ] ) );
		}
	return Verdict();
}

//===========================================================================
// --powder (GL): Faraday's currents, exactly; powder heaps on the antinodes.
//===========================================================================
int runPowder( const Perturb& perturb )
{
	std::printf( "\n=== powder: under g, a grain of powder moves by P a grad( a ) dt / ( L^2 w^3 ) a substep, exactly; with sand (Both), the sand goes "
	             "to the nodal lines and the powder to the antinodes\n" );
	const auto table = aluminiumTable( Shape::Square );
	const int ss     = findMode( *table, modes::kSS, 1 );
	for( const Raster& raster : kRasters )
	{
		{
			const double w = 2.0 * kPi * 20.0;
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.Set( PT_GRAIN, static_cast< float >( Grain::Powder ) );
			rig.Set( PT_AMOUNT, 1.0f );
			rig.Set( PT_AIR, 1.0f );
			setPure( rig, ss, 1.0, w );
			rig.plugin.SetSubstepsForTest( 0 );
			if( !rig.Render( 1 ) )
				return 1;
			const double peak = 0.8 * kG / latticeMax( rig.Field() );
			setPure( rig, ss, peak, w );
			if( !rig.Render( 1 ) )
				return 1;
			const Floats field  = rig.Field();
			const Floats before = rig.Grains();
			rig.plugin.SetSubstepsForTest( 1 );
			if( !rig.Render( 1 ) )
				return 1;
			const Floats after = rig.Grains();
			const double P  = AirFromParam( rig.plugin.GetById( PT_AIR ) );
			const double L  = rig.plugin.LastFrame().halfSize;
			const double dt = static_cast< float >( shaders::kSubstep );
			double worst = 0.0, typical = 0.0;
			long n = 0, big = 0;
			for( int i = 0; i < rig.plugin.LastFrame().grains; ++i )
			{
				const size_t t = static_cast< size_t >( i );
				const double x = before[ 4 * t ], y = before[ 4 * t + 1 ];
				if( std::fabs( x ) > 0.999 || std::fabs( y ) > 0.999 )
					continue;
				const Accel A = fieldAt( field, x, y );
				const double k  = P * A.a * dt / ( L * L * w * w * w );
				const double ex = k * A.gx, ey = k * A.gy;
				//Allowed: the rounding of x + d (two ulps of x) and 1e-4 of |d| (the
				//float chain; a small component carries the error of the whole step).
				const double dn = std::hypot( ex, ey );
				const double tx = 2.0 * std::ldexp( 1.0, std::ilogb( std::max( std::fabs( x ), 1e-3 ) ) - 23 ) + 1e-4 * dn;
				const double ty = 2.0 * std::ldexp( 1.0, std::ilogb( std::max( std::fabs( y ), 1e-3 ) ) - 23 ) + 1e-4 * dn;
				const double e = std::max( std::fabs( after[ 4 * t ] - x - ex ) / tx, std::fabs( after[ 4 * t + 1 ] - y - ey ) / ty );
				if( e > worst && std::getenv( "ED_DEBUG" ) )
				{
					const double cx = ( x + 1.0 ) * 0.5 * shaders::kFieldSize - 0.5, cy = ( y + 1.0 ) * 0.5 * shaders::kFieldSize - 0.5;
					std::printf( "    worse: grain %d at ( %.7f, %.7f ) cell frac ( %.6f, %.6f ): d ( %.4e, %.4e ) want ( %.4e, %.4e ), a/g %.3f\n", i, x, y,
					             cx - std::floor( cx ), cy - std::floor( cy ), after[ 4 * t ] - x, after[ 4 * t + 1 ] - y, ex, ey, A.a / kG );
				}
				worst = std::max( worst, e );
				typical += std::hypot( ex, ey );
				big += std::hypot( ex, ey ) > 10.0 * std::max( tx, ty );
				++n;
			}
			Check( worst <= 1.0 && big > n / 2, fmt( "%dx%d: %ld grains of powder under g (peak %.2f g), one substep: the worst off P a grad( a ) dt / ( L^2 w^3 ) by "
			                                         "%.2f of its allowance (2 ulp + 1e-4); mean step %.2e plate units",
			                                         raster.w, raster.h, n, 0.8, worst, typical / n ) );
		}
		{
			const double a0 = 4.0 * kG, w = 2.0 * kPi * 100.0;
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.Set( PT_GRAIN, static_cast< float >( Grain::Both ) );
			rig.Set( PT_AMOUNT, ParamFromAmount( 16384 ) );
			rig.Set( PT_AIR, 1.0f );
			setPure( rig, ss, a0, w );
			rig.plugin.SetSubstepsForTest( shaders::kMaxSubsteps );
			if( !rig.Render( 20 * 480 / shaders::kMaxSubsteps ) )
				return 1;
			const Floats state = rig.Grains();
			const int count    = rig.plugin.LastFrame().grains;
			//The antinodes: local maxima of |W| on a 257^2 grid, at least half the largest.
			const modes::Mode& mode = table->modes[ static_cast< size_t >( ss ) ];
			const int G = 256;
			std::vector< double > grid( static_cast< size_t >( G + 1 ) * ( G + 1 ) );
			double top = 0.0;
			for( int j = 0; j <= G; ++j )
				for( int i = 0; i <= G; ++i )
				{
					grid[ static_cast< size_t >( j ) * ( G + 1 ) + i ] = std::fabs( table->W( mode, -1.0 + 2.0 * i / G, -1.0 + 2.0 * j / G ) );
					top = std::max( top, grid[ static_cast< size_t >( j ) * ( G + 1 ) + i ] );
				}
			std::vector< std::pair< double, double > > antinodes;
			for( int j = 0; j <= G; ++j )
				for( int i = 0; i <= G; ++i )
				{
					const double v = grid[ static_cast< size_t >( j ) * ( G + 1 ) + i ];
					bool peak      = v >= 0.5 * top;
					for( int dj = -1; dj <= 1 && peak; ++dj )
						for( int di = -1; di <= 1 && peak; ++di )
						{
							const int ii = i + di, jj = j + dj;
							if( ( di || dj ) && ii >= 0 && jj >= 0 && ii <= G && jj <= G && grid[ static_cast< size_t >( jj ) * ( G + 1 ) + ii ] > v )
								peak = false;
						}
					if( peak )
						antinodes.emplace_back( -1.0 + 2.0 * i / G, -1.0 + 2.0 * j / G );
				}
			const double reach = 0.2;
			auto nearAntinode  = [ & ]( double x, double y ) {
                for( const auto& p : antinodes )
                    if( std::hypot( x - p.first, y - p.second ) <= reach )
                        return true;
                return false;
			};
			//What a uniform scatter would give: the area within reach.
			int areaIn = 0;
			for( int j = 0; j < 200; ++j )
				for( int i = 0; i < 200; ++i )
					areaIn += nearAntinode( -1.0 + ( i + 0.5 ) / 100.0, -1.0 + ( j + 0.5 ) / 100.0 );
			const ZeroSet& z = zeroSetOf( *table, ss );
			int powder = 0, powderNear = 0, sand = 0, sandOn = 0;
			for( int i = 0; i < count; ++i )
			{
				const size_t t = static_cast< size_t >( i );
				const double x = state[ 4 * t ], y = state[ 4 * t + 1 ];
				if( state[ 4 * t + 3 ] > 0.5 )
				{
					++powder;
					powderNear += nearAntinode( x, y );
				}
				else
				{
					++sand;
					//At 4 g the dead band is broad: the patch where the lines nearly
					//cross is a quarter of a plate unit across, and sand fills it.
					sandOn += onNodal( *table, ss, z, x, y, a0, nullptr, nullptr, 0.5 );
				}
			}
			const double near = static_cast< double >( powderNear ) / std::max( powder, 1 ), on = static_cast< double >( sandOn ) / std::max( sand, 1 );
			Check( near >= 0.9 && on >= 0.95 && powder > count / 3 && sand > count / 3,
			       fmt( "%dx%d: Both, mode %d at %.0f g, 20 s: %.1f%% of %d powder grains within %.1f of the %zu antinodes (a uniform scatter: %.1f%%); "
			            "%.1f%% of %d sand grains in the dead band, within 0.5 of a nodal line",
			            raster.w, raster.h, ss, a0 / kG, 100.0 * near, powder, reach, antinodes.size(), 100.0 * areaIn / 40000.0, 100.0 * on, sand ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --freeze (GL): music under the threshold moves nothing, for 10 s; silence
// after loud music freezes the figure.
//===========================================================================
struct ToneSource
{
	double hz = 400.0, db = -12.0;
	bool on   = true;
	size_t at = 0;
	void Before( EidophonePlugin& plugin, int frame, double fps )
	{
		const size_t target = static_cast< size_t >( std::llround( ( frame + 1 ) / fps * 48000.0 ) );
		if( target <= at )
			return;
		const size_t n = target - at;
		std::vector< float > block( 2 * n, 0.0f );
		const double amp = on ? std::pow( 10.0, db / 20.0 ) : 0.0;
		for( size_t i = 0; i < n; ++i )
			block[ 2 * i ] = block[ 2 * i + 1 ] = static_cast< float >( amp * std::sin( 2.0 * kPi * hz * static_cast< double >( at + i ) / 48000.0 ) );
		plugin.Audio().Feed( block.data(), static_cast< int >( n ), 48000, 2 );
		at = target;
	}
};

//===========================================================================
// --level (GL): Gain is the plate's steady peak acceleration, on a mode or
// between two, so a wrong note shows as plainly as a right one.
//===========================================================================
/// max |sum_k a_k W_k| per newton over the field lattice's texel centres, in
/// double from the plate's own steady state (what the GPU's lattice holds).
double latticePeakPerNewton( const Plate& plate, double omegaD )
{
	const modes::Table& table = *plate.TablePtr();
	const int S = shaders::kFieldSize;
	std::vector< std::complex< double > > a( static_cast< size_t >( plate.Count() ) );
	for( int k = 0; k < plate.Count(); ++k )
		a[ static_cast< size_t >( k ) ] = -omegaD * omegaD * plate.SteadyAmplitude( k, 1.0, omegaD );
	std::vector< float > lattice( static_cast< size_t >( S ) * S );
	std::vector< std::complex< double > > sum( static_cast< size_t >( S ) * S );
	for( int k = 0; k < plate.Count(); ++k )
	{
		table.Sample( table.modes[ static_cast< size_t >( k ) ], S, lattice.data() );
		for( size_t p = 0; p < sum.size(); ++p )
			sum[ p ] += a[ static_cast< size_t >( k ) ] * static_cast< double >( lattice[ p ] );
	}
	double best = 0.0;
	for( const auto& v : sum )
		best = std::max( best, std::abs( v ) );
	return best;
}

int runLevel( const Perturb& perturb )
{
	std::printf( "\n=== level: a steady sine at any pitch settles with the plate's peak acceleration at Gain (in g): on a mode, between two, low, "
	             "high\n" );
	const auto table = aluminiumTable( Shape::Square );
	//The default plate's SS frequencies (the centre drive excites only these).
	std::vector< double > ss;
	{
		const MaterialData& m = MaterialOf( Material::Aluminium );
		const double L = 0.5 * SizeFromParam( InfoOf( PT_SIZE ).defaultValue );
		const double c = PlateSpeed( m, ThicknessFromParam( InfoOf( PT_THICKNESS ).defaultValue ) );
		for( const modes::Mode& mode : table->modes )
			if( mode.cls == modes::kSS )
				ss.push_back( mode.Lambda * c / ( L * L ) / ( 2.0 * kPi ) );
	}
	const double onMode = defaultToneHz();
	size_t at = 0;
	while( at + 1 < ss.size() && ss[ at ] < onMode - 1e-9 )
		++at;
	const double between = std::sqrt( ss[ at ] * ss[ at + 1 ] );
	struct Case
	{
		const char* what;
		double hz;
	};
	const Case cases[] = { { "on an SS mode", onMode }, { "midway (geometric) to the next", between }, { "low, near earshot's 40 Hz floor", 45.0 },
		                   { "high", 1700.0 } };
	const Raster raster = kRasters.back();//the field lattice is the same at every raster
	const double gain   = 5.0;
	for( const Case& c : cases )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		ToneSource tone;
		tone.hz         = c.hz;
		rig.beforeFrame = [ & ]( int f ) { tone.Before( rig.plugin, f, rig.fps ); };
		rig.Set( PT_GAIN, ParamFromGain( gain ) );
		rig.Set( PT_AMOUNT, ParamFromAmount( 1024 ) );
		if( !rig.Render( 240 ) )//4 s: the slowest free part (the lowest SS mode, Q 40) is down by e^-14
			return 1;
		const FrameData& f   = rig.plugin.LastFrame();
		const double peak    = latticeMax( rig.Field() );
		const Plate& plate   = rig.plugin.PlateState();
		const double grid    = plate.PeakPerNewton( f.drive.omega );
		const double fine    = latticePeakPerNewton( plate, f.drive.omega );
		const double expect  = gain * kG * f.drive.relative * fine / grid;
		Check( std::fabs( peak / expect - 1.0 ) < 2e-3 && peak / ( gain * kG ) > 0.9 && peak / ( gain * kG ) < 1.15,
		       fmt( "%-31s %7.2f Hz: peak %.3f g = %.3f x Gain (%.0f g); the plugin's 25^2 grid against the lattice's 256^2 predicts %.4f x, the "
		            "lattice holds %.4f of that",
		            c.what, f.drive.omega / ( 2.0 * kPi ), peak / kG, peak / ( gain * kG ), gain, fine / grid, peak / expect ) );
	}
	return Verdict();
}

int runFreeze( const Perturb& perturb )
{
	std::printf( "\n=== freeze: a tone that drives the plate to 0.7 g moves no grain of sand in 10 s; after loud music, silence freezes the figure\n" );
	const double hz = defaultToneHz();
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		ToneSource tone;
		tone.hz         = hz;
		rig.beforeFrame = [ & ]( int f ) { tone.Before( rig.plugin, f, rig.fps ); };
		rig.Set( PT_GRAIN, static_cast< float >( Grain::Sand ) );
		const double probeGain = 1.0;
		rig.Set( PT_GAIN, ParamFromGain( probeGain ) );
		if( !rig.Render( 60 ) )
			return 1;
		//The response is linear in Gain: scale it to put the lattice's peak at 0.7 g.
		const double gain = probeGain * 0.7 * kG / latticeMax( rig.Field() );
		rig.Set( PT_GAIN, ParamFromGain( gain ) );
		if( !rig.Render( 60 ) )
			return 1;
		rig.plugin.PourForTest();
		if( !rig.Render( 1 ) )
			return 1;
		const Floats poured = rig.Grains();
		const int count     = rig.plugin.LastFrame().grains;
		double peak = 0.0;
		int changed = 0, unvoiced = 0;
		for( int f = 0; f < 600; ++f )
		{
			if( !rig.Render( 1 ) )
				return 1;
			peak = std::max( peak, latticeMax( rig.Field() ) );
			unvoiced += !rig.plugin.LastFrame().drive.note;
			if( f % 100 == 99 )
			{
				const Floats now = rig.Grains();
				for( int i = 0; i < count; ++i )
					changed += !sameTexel( poured, now, static_cast< size_t >( i ) );
			}
		}
		Check( changed == 0 && peak < kG && peak > 0.5 * kG && unvoiced == 0,
		       fmt( "%dx%d: a %.1f Hz tone for 10 s, the plate's peak %.3f g (Gain %.3f g): %d grain-readings of %d changed in any bit; %d frames without a "
		            "note",
		            raster.w, raster.h, hz, peak / kG, gain, changed, 6 * count, unvoiced ) );

		rig.Set( PT_GAIN, ParamFromGain( 4.0 * gain ) );
		if( !rig.Render( 120 ) )
			return 1;
		const Floats loud = rig.Grains();
		int thrown = 0;
		for( int i = 0; i < count; ++i )
			thrown += !sameTexel( poured, loud, static_cast< size_t >( i ) );
		tone.on = false;
		if( !rig.Render( 120 ) )
			return 1;
		const Floats early = rig.Grains();
		if( !rig.Render( 480 ) )
			return 1;
		const Floats late = rig.Grains();
		int moved = 0;
		for( int i = 0; i < count; ++i )
			moved += !sameTexel( early, late, static_cast< size_t >( i ) );
		Check( thrown > 1000 && moved == 0 && !rig.plugin.LastFrame().drive.note,
		       fmt( "%dx%d: 2 s at 2.8 g moved %d of %d grains; then silence: from 2 s to 10 s after it, %d moved in any bit", raster.w, raster.h, thrown, count,
		            moved ) );
	}
	return Verdict();
}

//===========================================================================
// --redraw (GL): a host that draws the same frame twice moves nothing.
//===========================================================================
int runRedraw( const Perturb& perturb )
{
	std::printf( "\n=== redraw: frames at an unchanged host time run no substep, change no grain and draw the same picture\n" );
	const auto table = aluminiumTable( Shape::Square );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		setPure( rig, findMode( *table, modes::kSS ), 6.0 * kG, 2.0 * kPi * 200.0 );
		if( !rig.Render( 30 ) )
			return 1;
		const Floats grains = rig.Grains(), picture = rig.Output();
		const uint32_t steps = rig.plugin.StepCounter();
		for( int i = 0; i < 5; ++i )
		{
			rig.plugin.SetTime( rig.Seconds( rig.frame - 1 ) );
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 0, 0, raster.w, raster.h );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
		}
		Check( rig.plugin.StepCounter() == steps && rig.Grains() == grains && rig.Output() == picture && steps > 0,
		       fmt( "%dx%d: five more frames at the last frame's time: %u substeps run (%u before), grains and picture %s", raster.w, raster.h,
		            rig.plugin.StepCounter() - steps, steps, rig.Grains() == grains && rig.Output() == picture ? "identical" : "CHANGED" ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha (GL): the Over's alpha.
//===========================================================================
int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: the Over at Mix 0 is the clip; its alpha is the grains laid OVER the clip's, mixed by Mix; the clip shows through "
	             "untouched where no grain or shadow lies; the source is opaque\n" );
	for( const Raster& raster : kRasters )
	{
		const Floats noise = noiseCard( raster.w, raster.h );
		{
			Rig rig( true );
			if( !startRig( rig, raster.w, raster.h, perturb, &noise ) )
				return 1;
			rig.Set( PT_MIX, 0.0f );
			if( !rig.Render( 2 ) )
				return 1;
			const Floats out = rig.Output();
			int differ       = 0;
			for( size_t i = 0; i < out.size(); ++i )
				differ += out[ i ] != noise[ i ];
			Check( differ == 0, fmt( "%dx%d: Mix 0 against a per-pixel noise card: %d of %zu floats differ", raster.w, raster.h, differ, out.size() ) );
		}
		{
			const float clipAlpha = 0.3f;
			const Floats see      = flatCard( raster.w, raster.h, 0.8f, 0.4f, 0.2f, clipAlpha );
			Rig rig( true );
			if( !startRig( rig, raster.w, raster.h, perturb, &see ) )
				return 1;
			rig.Set( PT_GRAIN, static_cast< float >( Grain::Both ) );
			rig.Set( PT_GRAIN_SIZE, ParamFromGrainSize( 2.5 ) );
			double worstAlpha = 0.0, worstClear = 0.0;
			int clear = 0, covered = 0;
			for( float mix : { 0.4f, 1.0f } )
			{
				rig.Set( PT_MIX, mix );
				if( !rig.Render( 2 ) )
					return 1;
				const Floats out = rig.Output(), dens = rig.Density();
				for( int y = 0; y < raster.h; ++y )
					for( int x = 0; x < raster.w; ++x )
					{
						const size_t i = ( static_cast< size_t >( y ) * raster.w + x ) * 4;
						const double cs = 1.0 - std::exp( -static_cast< double >( dens[ i ] ) ), cp = 1.0 - std::exp( -static_cast< double >( dens[ i + 1 ] ) );
						const double A  = ( clipAlpha * ( 1.0 - cs ) + cs ) * ( 1.0 - cp ) + cp;
						worstAlpha      = std::max( worstAlpha, std::fabs( out[ i + 3 ] - ( clipAlpha + ( A - clipAlpha ) * mix ) ) );
						covered += cs + cp > 0.5;
						const int bx = std::clamp( x - 1, 0, raster.w - 1 ), by = std::clamp( y + 1, 0, raster.h - 1 );
						const size_t behind = ( static_cast< size_t >( by ) * raster.w + bx ) * 4;
						if( mix == 1.0f && dens[ i ] == 0.0f && dens[ i + 1 ] == 0.0f && dens[ behind ] == 0.0f && dens[ behind + 1 ] == 0.0f )
						{
							++clear;
							for( int c = 0; c < 3; ++c )
								worstClear = std::max( worstClear, static_cast< double >( std::fabs( out[ i + c ] - see[ i + c ] ) ) );
						}
					}
			}
			Check( worstAlpha < 1e-5 && covered > 0, fmt( "%dx%d: a clip at alpha 0.3, Both, Mix 0.4 and 1: alpha against mix( 0.3, over( sand, powder, 0.3 ), "
			                                             "Mix ) from the density read back, within %.1e (%d covered pixels)",
			                                             raster.w, raster.h, worstAlpha, covered ) );
			Check( worstClear < 1e-5 && clear > 0, fmt( "%dx%d: %d pixels with no grain and no shadow: the clip's colour within %.1e", raster.w, raster.h, clear,
			                                           worstClear ) );
		}
		{
			Rig rig( false );
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			if( !rig.Render( 3 ) )
				return 1;
			const Floats out = rig.Output();
			int notOpaque    = 0;
			for( size_t i = 3; i < out.size(); i += 4 )
				notOpaque += out[ i ] != 1.0f;
			Check( notOpaque == 0, fmt( "%dx%d: the source's alpha is 1 at %zu of %zu pixels", raster.w, raster.h, out.size() / 4 - notOpaque, out.size() / 4 ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --splat (GL): the picture is the state. Every grain drawn once, where the
// state says, sand in r and powder in g, against a CPU rasteriser.
//===========================================================================
struct SplatReplica
{
	std::vector< double > r, g;
	std::vector< int > blendsR, blendsG;///< fragments with coverage blended into each pixel
	double fringe = 0.0;///< weight x pixels in a disc's edge, summed: where a snapped centre can move the coverage
	int sand = 0, powder = 0;
};

/// GL's point rule and the splat fragment, in double: a point of Raster px
/// (whole) covers the pixels whose centres lie inside its square, each by the
/// disc coverage clamp( PS/2 - r + 1/2 ). Window px = x * PlateScale + W/2.
SplatReplica splatReplica( const Floats& grains, int count, int W, int H, double plateScale, double pointSize )
{
	SplatReplica out;
	out.r.assign( static_cast< size_t >( W ) * H, 0.0 );
	out.g.assign( static_cast< size_t >( W ) * H, 0.0 );
	out.blendsR.assign( static_cast< size_t >( W ) * H, 0 );
	out.blendsG.assign( static_cast< size_t >( W ) * H, 0 );
	const double disc   = std::max( pointSize, 1.0 );
	const double weight = ( pointSize / disc ) * ( pointSize / disc );
	const double half   = 0.5 * ( std::ceil( disc ) + 2.0 );
	for( int i = 0; i < count; ++i )
	{
		const double px = grains[ static_cast< size_t >( i ) * 4 ] * plateScale + 0.5 * W;
		const double py = grains[ static_cast< size_t >( i ) * 4 + 1 ] * plateScale + 0.5 * H;
		const bool isPowder = grains[ static_cast< size_t >( i ) * 4 + 3 ] > 0.5f;
		( isPowder ? out.powder : out.sand )++;
		std::vector< double >& target = isPowder ? out.g : out.r;
		for( int y = static_cast< int >( std::floor( py - half ) ); y <= static_cast< int >( std::ceil( py + half ) ); ++y )
			for( int x = static_cast< int >( std::floor( px - half ) ); x <= static_cast< int >( std::ceil( px + half ) ); ++x )
			{
				const double dx = x + 0.5 - px, dy = y + 0.5 - py;
				if( std::fabs( dx ) >= half || std::fabs( dy ) >= half || x < 0 || y < 0 || x >= W || y >= H )
					continue;
				const double edge = 0.5 * disc - std::hypot( dx, dy ) + 0.5;
				target[ static_cast< size_t >( y ) * W + x ] += weight * std::clamp( edge, 0.0, 1.0 );
				if( edge > 0.0 )
					( isPowder ? out.blendsG : out.blendsR )[ static_cast< size_t >( y ) * W + x ]++;
				if( edge > -0.05 && edge < 1.05 )
					out.fringe += weight;
			}
	}
	return out;
}

int runSplat( const Perturb& perturb )
{
	std::printf( "\n=== splat: the density the final pass reads is the grain state rasterised -- every grain once, at its place, sand in r and powder "
	             "in g -- against a CPU replica of GL's point rule\n" );
	const auto table = aluminiumTable( Shape::Square );
	struct Case
	{
		int w, h;
		double pointSize;
	};
	std::vector< Case > cases;
	for( const Raster& raster : kRasters )
		cases.push_back( { raster.w, raster.h, 0.6 } );//under a pixel: a one-pixel disc at weight PS^2
	//Grain Size reaches these only at 720 lines and up (0.5 to 4 px at 1080).
	for( double ps : { 1.5, 2.6 } )
		if( ps * 1080.0 / kRasters.front().h <= 4.0 )//Grain Size's top, 4 px at 1080
			cases.push_back( { kRasters.front().w, kRasters.front().h, ps } );
	GLint subpixelBits = 0;
	glGetIntegerv( GL_SUBPIXEL_BITS, &subpixelBits );
	const double snap = std::ldexp( 1.0, -std::max( subpixelBits, 4 ) );//GL's floor is 4 bits
	for( const Case& c : cases )
	{
		Rig rig;
		if( !startRig( rig, c.w, c.h, perturb ) )
			return 1;
		rig.Set( PT_GRAIN, static_cast< float >( Grain::Both ) );
		rig.Set( PT_VIEW, 0.0f );//Fit: the whole plate on the raster
		rig.Set( PT_GRAIN_SIZE, ParamFromGrainSize( c.pointSize * 1080.0 / c.h ) );
		setPure( rig, findMode( *table, modes::kSS, 1 ), 3.0 * kG, 2.0 * kPi * 200.0 );
		if( !rig.Render( 20 ) )//the grains moved off the poured lattice of random numbers, so the test is not of the pour alone
			return 1;
		const FrameData& f  = rig.plugin.LastFrame();
		const Floats grains = rig.Grains(), dens = rig.Density();
		const SplatReplica cpu = splatReplica( grains, f.grains, c.w, c.h, f.plateScale, f.pointSize );
		//RG16F blends round each sum to 10 mantissa bits: a pixel blended n
		//times ends within n 2^-10 of its value (2^-10, not 2^-11, in case the
		//blender truncates). The software renderer keeps more and lands inside.
		double gpuR = 0.0, gpuG = 0.0, cpuR = 0.0, cpuG = 0.0, l1 = 0.0, halfR = 0.0, halfG = 0.0;
		for( size_t p = 0; p < cpu.r.size(); ++p )
		{
			gpuR += dens[ p * 4 ];
			gpuG += dens[ p * 4 + 1 ];
			cpuR += cpu.r[ p ];
			cpuG += cpu.g[ p ];
			halfR += cpu.blendsR[ p ] * cpu.r[ p ] * std::ldexp( 1.0, -10 );
			halfG += cpu.blendsG[ p ] * cpu.g[ p ] * std::ldexp( 1.0, -10 );
			l1 += std::fabs( dens[ p * 4 ] - cpu.r[ p ] ) + std::fabs( dens[ p * 4 + 1 ] - cpu.g[ p ] );
		}
		const double total = cpuR + cpuG;
		if( std::getenv( "ED_DEBUG" ) )
			std::printf( "    gpu r %.3f g %.3f, cpu r %.3f g %.3f (half-float bound %.3f, %.3f); GL_SUBPIXEL_BITS %d\n", gpuR, gpuG, cpuR, cpuG, halfR, halfG,
			             subpixelBits );
		const bool massOk = std::fabs( gpuR - cpuR ) <= halfR + 1e-6 * cpuR && std::fabs( gpuG - cpuG ) <= halfG + 1e-6 * cpuG;
		const double massErr = std::max( std::fabs( gpuR - cpuR ) / halfR, std::fabs( gpuG - cpuG ) / halfG );
		//A centre snapped to the rasteriser's subpixel grid moves each edge
		//pixel's coverage by at most the snap.
		const double allowed = snap * cpu.fringe + halfR + halfG;
		Check( massOk && l1 <= allowed && cpu.sand > 0 && cpu.powder > 0,
		       fmt( "%dx%d, %.2f px points: %d sand and %d powder grains; mass in r and g off the replica's by %.2f of the half-float bound; per-pixel |GPU - CPU| summed %.2f "
		            "(%.3f%% of the mass; allowed %.2f: %d subpixel bits on the disc edges, RG16F's rounding)",
		            c.w, c.h, f.pointSize, cpu.sand, cpu.powder, massErr, l1, 100.0 * l1 / total, allowed, subpixelBits ) );
	}
	return Verdict();
}

//===========================================================================
// --clock, --resize and --state.
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: at 499,000,000 ms the frames are still 1/60 s apart, and each runs exactly 8 substeps of 1/480 s\n" );
	const Raster raster = kRasters.back();
	Rig rig;
	if( !startRig( rig, raster.w, raster.h, perturb ) )
		return 1;
	rig.plugin.SetClockScaleForTest( 0.001 );
	double worstDt = 0.0, lastNow = 0.0;
	int wrongSteps = 0;
	for( int f = 0; f < 60; ++f )
	{
		rig.plugin.SetTime( 499000000.0 + f * 1000.0 / 60.0 );
		glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
		glViewport( 0, 0, raster.w, raster.h );
		if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
			return 1;
		const double now = rig.plugin.HostNow();
		if( f > 0 )
		{
			worstDt = std::max( worstDt, std::fabs( ( now - lastNow ) - 1.0 / 60.0 ) );
			wrongSteps += rig.plugin.LastFrame().substeps != 8;
		}
		lastNow = now;
	}
	Check( worstDt < 1e-9 && wrongSteps == 0, fmt( "60 frames: dt off 1/60 by at most %.1e s; %d frames without exactly 8 substeps", worstDt, wrongSteps ) );
	return Verdict();
}

int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: after the host's raster changes (and its shape), the picture is exactly a fresh instance's that saw the same frames\n" );
	const Raster small = kRasters.back(), big = { 2 * small.w, 2 * small.h }, tall = { small.w, small.w * 3 / 4 };
	const auto table = aluminiumTable( Shape::Square );
	const int ss     = findMode( *table, modes::kSS, 1 );
	for( bool over : { false, true } )
		for( const Raster& to : { small, tall } )
		{
			Rig resized( over );
			if( !startRig( resized, big.w, big.h, perturb ) )
				return 1;
			setPure( resized, ss, 8.0 * kG, 2.0 * kPi * 200.0 );
			if( !resized.Render( 70 ) )
				return 1;
			const Floats card = buildCard( to.w, to.h );
			if( !resized.Resize( to.w, to.h, &card ) || !resized.Render( 3 ) )
				return 1;
			Rig fresh( over );
			if( !startRig( fresh, to.w, to.h, Perturb {}, &card ) )
				return 1;
			setPure( fresh, ss, 8.0 * kG, 2.0 * kPi * 200.0 );
			if( !fresh.Render( 73 ) )
				return 1;
			const Floats x = resized.Output(), y = fresh.Output();
			int differ = 0;
			for( size_t i = 0; i < x.size() && i < y.size(); ++i )
				differ += byteOf( x[ i ] ) != byteOf( y[ i ] );
			Floats poured;
			{
				Rig still( over );
				if( !startRig( still, to.w, to.h, Perturb {}, &card ) || !still.Render( 1 ) )
					return 1;
				poured = still.Grains();
			}
			const int moved = movedCount( poured, fresh.Grains(), fresh.plugin.LastFrame().grains );
			Check( differ == 0 && x.size() == y.size() && moved > 1000,
			       fmt( "%s: %dx%d for 70 frames, then %dx%d for 3, against %dx%d for all 73: %d bytes differ (%d grains had moved)", over ? "Over" : "source", big.w,
			            big.h, to.w, to.h, to.w, to.h, differ, moved ) );
		}
	return Verdict();
}

int runState( const Perturb& )
{
	std::printf( "\n=== state: the GL state the host hands over is the state it gets back, both plugins\n" );
	for( bool over : { false, true } )
		for( bool pointSize : { false, true } )
		{
			Rig rig( over );
			if( !rig.Init( 320, 180 ) )
				return 1;
			GLuint hostArray = 0, hostBuffer = 0;
			glGenVertexArrays( 1, &hostArray );
			glGenBuffers( 1, &hostBuffer );
			int problems = 0;
			std::string what;
			for( int frame = 0; frame < 6; ++frame )
			{
				rig.Set( PT_POUR, frame == 1 ? 1.0f : 0.0f );
				rig.Set( PT_RESCAN, frame == 2 ? 1.0f : 0.0f );
				rig.Set( PT_SHAPE, static_cast< float >( frame >= 3 ? Shape::Circle : Shape::Square ) );
				rig.Set( PT_GRAIN, static_cast< float >( frame % 3 ) );
				glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
				glViewport( 0, 0, 320, 180 );
				glBindVertexArray( hostArray );
				glBindBuffer( GL_ARRAY_BUFFER, hostBuffer );
				glEnable( GL_BLEND );
				glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
				glClearColor( 0.2f, 0.3f, 0.4f, 0.5f );
				glEnable( GL_SCISSOR_TEST );
				glScissor( 0, 0, 320, 180 );
				if( pointSize )
					glEnable( GL_PROGRAM_POINT_SIZE );
				else
					glDisable( GL_PROGRAM_POINT_SIZE );
				glActiveTexture( GL_TEXTURE0 );
				glUseProgram( 0 );
				rig.plugin.SetTime( frame / 60.0 );
				if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
					return 1;
				GLint viewport[ 4 ] = {}, array = 0, buffer = 0, program = 0, unit = 0, fbo = 0, src = 0, dst = 0;
				GLfloat clear[ 4 ]  = {};
				GLboolean mask[ 4 ] = {};
				glGetIntegerv( GL_VIEWPORT, viewport );
				glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &array );
				glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &buffer );
				glGetIntegerv( GL_CURRENT_PROGRAM, &program );
				glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
				glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
				glGetIntegerv( GL_BLEND_SRC_RGB, &src );
				glGetIntegerv( GL_BLEND_DST_RGB, &dst );
				glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
				glGetBooleanv( GL_COLOR_WRITEMASK, mask );
				auto expect = [ & ]( bool ok, const char* name ) {
					if( !ok )
					{
						++problems;
						what += std::string( " " ) + name;
					}
				};
				expect( viewport[ 0 ] == 0 && viewport[ 1 ] == 0 && viewport[ 2 ] == 320 && viewport[ 3 ] == 180, "viewport" );
				expect( array == static_cast< GLint >( hostArray ), "vertex-array" );
				expect( buffer == static_cast< GLint >( hostBuffer ), "array-buffer" );
				expect( program == 0, "program" );
				expect( unit == GL_TEXTURE0, "active-unit" );
				expect( fbo == static_cast< GLint >( rig.outputFBO ), "framebuffer" );
				expect( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blend" );
				expect( glIsEnabled( GL_SCISSOR_TEST ), "scissor" );
				expect( static_cast< bool >( glIsEnabled( GL_PROGRAM_POINT_SIZE ) ) == pointSize, "program-point-size" );
				expect( clear[ 0 ] == 0.2f && clear[ 1 ] == 0.3f && clear[ 2 ] == 0.4f && clear[ 3 ] == 0.5f, "clear-colour" );
				expect( mask[ 0 ] && mask[ 1 ] && mask[ 2 ] && mask[ 3 ], "colour-mask" );
				for( int u = 0; u < 8; ++u )
				{
					GLint bound = 0, boundArray = 0;
					glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
					glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
					glGetIntegerv( GL_TEXTURE_BINDING_2D_ARRAY, &boundArray );
					expect( bound == 0, "texture-unit" );
					expect( boundArray == 0, "texture-array-unit" );
				}
				glActiveTexture( GL_TEXTURE0 );
			}
			glDisable( GL_SCISSOR_TEST );
			glDisable( GL_BLEND );
			glDisable( GL_PROGRAM_POINT_SIZE );
			glBindVertexArray( 0 );
			glBindBuffer( GL_ARRAY_BUFFER, 0 );
			glDeleteVertexArrays( 1, &hostArray );
			glDeleteBuffers( 1, &hostBuffer );
			Check( problems == 0, fmt( "%s, host point size %s, six frames with a Pour, a Rescan, both shapes and every grain: viewport, vertex array, array buffer, "
			                           "program, active unit, framebuffer, blend, scissor, point size, clear colour, colour mask, 8 units of 2D and 2D-array "
			                           "textures (%d wrong:%s)",
			                           over ? "SW Eido Over" : "SW Eidophone", pointSize ? "on" : "off", problems, what.empty() ? " none" : what.c_str() ) );
		}
	return Verdict();
}

//===========================================================================
// --names and --cues (no GL).
//===========================================================================
int runNames( const Perturb& )
{
	std::printf( "\n=== names: every parameter unique (as Arena addresses them, too), within FFGL's 16 characters, the About block last\n" );
	for( bool over : { false, true } )
	{
		EidophonePlugin plugin( over );
		std::map< std::string, int > seen, address;
		int longNames = 0, dupes = 0, clashes = 0, slashes = 0;
		for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
		{
			const std::string name = plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "";
			if( name.size() > 16 )
			{
				std::printf( "    too long: %s\n", name.c_str() );
				++longNames;
			}
			if( seen[ name ]++ > 0 )
				++dupes;
			slashes += name.find( '/' ) != std::string::npos;
			std::string key;
			for( char c : name )
				if( c != ' ' )
					key += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
			if( address[ key ]++ > 0 )
				++clashes;
		}
		const unsigned int aboutFirst = plugin.GetNumParams() - stoatworks::about::kParamCount;
		const bool aboutLast          = std::string( plugin.GetParamName( aboutFirst ) ) == "About";
		const bool mix                = plugin.HostIndexOf( PT_MIX ) >= 0;
		Check( longNames == 0 && dupes == 0 && clashes == 0 && slashes == 0 && aboutLast && mix == over,
		       fmt( "%s: %u parameters, %d too long, %d duplicated, %d clashing addresses, %d with a '/'; the About block last (%s); Mix %s", over ? "SW Eido Over" : "SW Eidophone",
		            plugin.GetNumParams(), longNames, dupes, clashes, slashes, aboutLast ? "yes" : "NO", mix ? "present" : "absent" ) );
	}
	return Verdict();
}

int runCues( const Perturb& perturb )
{
	std::printf( "\n=== cues: a cue sheet steps options and events, ramps standard controls, and names an option by its name\n" );
	EidophonePlugin plugin( false );
	std::istringstream sheet( "0 Shape Square\n60 Shape Circle\n0 Grain Sand\n60 Grain Both\n0 Pour 0\n60 Pour 1\n0 Drive At 0\n60 Drive At 1\n" );
	std::map< unsigned int, Cue > cues;
	std::string error;
	bindScript( plugin, sheet, "cues", cues, error );
	int bad = 0;
	std::string what;
	for( const auto& entry : cues )
	{
		const unsigned int type = plugin.GetParamType( entry.first );
		const bool ramp         = perturb.cuesRamp ? true : entry.second.ramp;
		const float mid         = valueAt( entry.second.track, 29, ramp );
		const float a = entry.second.track.front().second, b = entry.second.track.back().second;
		const bool expectRamp = type == FF_TYPE_STANDARD;
		const bool ok         = expectRamp ? ( mid > a && mid < b ) : ( mid == a && valueAt( entry.second.track, 60, ramp ) == b );
		bad += !ok;
		what += fmt( " %s %s(%g at frame 29)", plugin.GetParamName( entry.first ), expectRamp ? "ramps " : "steps ", mid );
	}
	const auto shape = cues.find( static_cast< unsigned int >( plugin.HostIndexOf( PT_SHAPE ) ) );
	const auto grain = cues.find( static_cast< unsigned int >( plugin.HostIndexOf( PT_GRAIN ) ) );
	const bool named = shape != cues.end() && shape->second.track.back().second == static_cast< float >( Shape::Circle ) && grain != cues.end()
	                   && grain->second.track.back().second == static_cast< float >( Grain::Both );
	Check( error.empty() && bad == 0 && cues.size() == 4 && named,
	       fmt( "%d wrong:%s; 'Circle' and 'Both' read as their options: %s", bad, what.c_str(), named ? "yes" : "NO" ) );
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "modes", runModes, true },         { "circle", runCircle, true },   { "plate", runPlate, true },   { "audio", runAudio, true },
		{ "device", runDevice, false },      { "select", runSelect, false },  { "threshold", runThreshold, false }, { "hop", runHop, false },
		{ "nodal", runNodal, false },        { "powder", runPowder, false },  { "level", runLevel, false },    { "freeze", runFreeze, false }, { "redraw", runRedraw, false },
		{ "alpha", runAlpha, false },        { "splat", runSplat, false },    { "clock", runClock, false },    { "resize", runResize, false }, { "state", runState, false },
		{ "names", runNames, true },         { "cues", runCues, true },       { "hostfft", runHostFft, false },
	};
	return list;
}

bool isOffline( const char* name )
{
	for( const CheckEntry& c : checks() )
		if( std::strcmp( c.flag, name ) == 0 )
			return c.offline;
	return false;
}

int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	using namespace shaders;
	add( "modes", runModes, "the nu w_xx w_yy term dropped from the stiffness", []( Perturb& p ) { p.modeHooks.dropNu = true; } );
	add( "modes", runModes, "the twist term 2( 1 - nu ) written ( 1 - nu )", []( Perturb& p ) { p.modeHooks.twistHalf = true; } );
	add( "circle", runCircle, "the moment condition's ( 1 - nu ) negated", []( Perturb& p ) { p.modeHooks.momentSignFlip = true; } );
	add( "plate", runPlate, "damping zeta w_d where zeta w_k belongs", []( Perturb& p ) { p.hooks = kHookDampingAtDrive; } );
	add( "plate", runPlate, "every mode coupled to the driver by 1", []( Perturb& p ) { p.hooks = kHookCouplingOne; } );
	add( "plate", runPlate, "no rigid-body response", []( Perturb& p ) { p.hooks = kHookDropRigid; } );
	add( "audio", runAudio, "the pitch in Hz taken as rad/s", []( Perturb& p ) { p.hooks = kHookHzAsOmega; } );
	add( "audio", runAudio, "the fallback lets bin 0 win", []( Perturb& p ) { p.hooks = kHookBinZero; } );
	add( "audio", runAudio, "the running peak never released", []( Perturb& p ) { p.hooks = kHookPeakHeld; } );
	add( "audio", runAudio, "the host's bins read at 44.1 kHz whatever the host says (v0.1.0's)", []( Perturb& p ) { p.hooks = kHookAssumed441; } );
	add( "hostfft", runHostFft, "the host's bins read at 44.1 kHz whatever the host says (v0.1.0's)", []( Perturb& p ) { p.hooks = kHookAssumed441; } );
	add( "hostfft", runHostFft, "the host's sample rate never reaches the bins' axis", []( Perturb& p ) { p.hooks = kHookIgnoreHostRate; } );
	add( "hostfft", runHostFft, "the fallback lets bin 0 win", []( Perturb& p ) { p.hooks = kHookBinZero; } );
	add( "device", runDevice, "the menu values devices by position, not by name", []( Perturb& p ) { p.hooks = kHookMenuByIndex; } );
	add( "select", runSelect, "the pitch in Hz taken as rad/s", []( Perturb& p ) { p.hooks = kHookHzAsOmega; } );
	add( "threshold", runThreshold, "grains hop at a > g/2", []( Perturb& p ) { p.hooks = kHookHalfGate; } );
	add( "hop", runHop, "sand drifts up the gradient", []( Perturb& p ) { p.hooks = kHookDriftUp; } );
	add( "hop", runHop, "launch speed a/w, not sqrt( a^2 - g^2 )/w", []( Perturb& p ) { p.hooks = kHookLaunchA; } );
	add( "nodal", runNodal, "sand drifts up the gradient", []( Perturb& p ) { p.hooks = kHookDriftUp; } );
	add( "powder", runPowder, "powder carried down the gradient", []( Perturb& p ) { p.hooks = kHookPowderDown; } );
	add( "level", runLevel, "the force sized by the rigid response alone", []( Perturb& p ) { p.hooks = kHookPeakRigidOnly; } );
	add( "freeze", runFreeze, "grains hop at a > g/2", []( Perturb& p ) { p.hooks = kHookHalfGate; } );
	add( "alpha", runAlpha, "the clip's alpha passed through", []( Perturb& p ) { p.hooks = kHookAlphaThrough; } );
	add( "splat", runSplat, "sand splatted as powder, powder as sand", []( Perturb& p ) { p.hooks = kHookSplatSwap; } );
	add( "cues", runCues, "ramp every control between keys", []( Perturb& p ) { p.cuesRamp = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture and grain checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 320x180 and above, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	const auto table   = aluminiumTable( Shape::Square );
	const int ss       = findMode( *table, modes::kSS, 1 );
	struct Case
	{
		const char* what;
		bool over;
		std::function< void( Rig& ) > set;
		bool at4K;///< 4K once, short (a shared machine: no 4K loops)
	};
	//Every grain thrown: the update's worst case (a pure mode at 40 g).
	auto loud = [ ss ]( Rig& r ) { setPure( r, ss, 40.0 * kG, 2.0 * kPi * 200.0 ); };
	const Case cases[] = {
		{ "source, defaults (65,536 grains)", false, [ & ]( Rig& r ) { loud( r ); }, true },
		{ "source, 262,144 grains", false,
		  [ & ]( Rig& r ) {
			  loud( r );
			  r.Set( PT_AMOUNT, 1.0f );
		  },
		  false },
		{ "source, 16,384 grains", false,
		  [ & ]( Rig& r ) {
			  loud( r );
			  r.Set( PT_AMOUNT, ParamFromAmount( 16384 ) );
		  },
		  false },
		{ "source, defaults, Both", false,
		  [ & ]( Rig& r ) {
			  loud( r );
			  r.Set( PT_GRAIN, static_cast< float >( Grain::Both ) );
		  },
		  false },
		{ "Over, defaults", true, [ & ]( Rig& r ) { loud( r ); }, true },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED) at 60 fps (8 substeps), every grain thrown, after 120 frames of warm-up, median and "
	             "worst of 120 (4K: 30 and 30, defaults only)\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			const bool fourK = size.w == 3840;
			if( fourK && !c.at4K )
				continue;
			Rig rig( c.over );
			if( !rig.Init( size.w, size.h ) )
				return 1;
			c.set( rig );
			if( !rig.Render( fourK ? 30 : 120 ) )
				return 1;
			const int kTimed = fourK ? 30 : 120;
			std::vector< double > times, cpu;
			for( int f = 0; f < kTimed; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				glBeginQuery( GL_TIME_ELAPSED, query );
				if( !rig.Render( 1 ) )
					return 1;
				glEndQuery( GL_TIME_ELAPSED );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				times.push_back( static_cast< double >( ns ) * 1e-6 );
				cpu.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
			}
			std::sort( times.begin(), times.end() );
			std::sort( cpu.begin(), cpu.end() );
			std::printf( "  %-34s %-6s GPU median %6.2f ms, worst %6.2f  (%5.1f%% of a 60 fps frame); wall %5.2f ms\n", c.what, size.name, times[ kTimed / 2 ],
			             times.back(), 100.0 * times[ kTimed / 2 ] / ( 1000.0 / 60.0 ), cpu[ kTimed / 2 ] );
		}
	glDeleteQueries( 1, &query );
	return 0;
}
