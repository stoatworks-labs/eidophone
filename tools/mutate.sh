#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# One scratch tree (the FFGL SDK and the earshot snapshot symlinked, not
# copied), built arm64-only ONCE; each mutant starts from a copy of that build
# with its timestamps kept, the original file put back with its own (older)
# mtime and the mutated one written after a second has passed, so make's
# one-second mtimes can only rebuild the mutated file (the fleet's trap: never
# mutate a build another step shares). Only the named check runs, at CI's
# raster, under a time limit (a mutant can hang: honeydew's lesson; a timeout
# counts as caught).
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Under TMPDIR when it is set (a heredoc into python hung once with the
# system's /var/folders TMPDIR: the fleet's trap).
WORK="$(mktemp -d "${TMPDIR:-/tmp}/edmutate.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap).
MUTANTS=(
	"source/Shaders.cpp|bool thrown  = a2 > gate * gate;|bool thrown  = a2 < gate * gate;|--threshold|GLSL: the gate inverted, the plate's quiet parts thrown, > -> <"
	"source/Shaders.cpp|sqrt( max( a2 - gate * gate, 0.0 ) ) / w;|sqrt( max( a2 + gate * gate, 0.0 ) ) / w;|--hop|GLSL: the launch speed sqrt( a^2 + g^2 ), - -> +"
	"source/Shaders.cpp|float D = Hop * Hop * v * v * v / ( 8.0 * Gravity );|float D = Hop * Hop * v * v * v / ( 9.0 * Gravity );|--hop|GLSL: the hop's diffusion H^2 v^3 / 9g, 8 -> 9"
	"source/Shaders.cpp|* ( 0.5 * Hop * v * Dt ) * n;|* ( 0.6 * Hop * v * Dt ) * n;|--hop|GLSL: the drift 20% fast, 5 -> 6"
	"source/Shaders.cpp|float r  = sqrt( -2.0 * log( unit01( h1 ) ) );|float r  = sqrt( -3.0 * log( unit01( h1 ) ) );|--hop|GLSL: Box-Muller's radius from -3 ln u, 2 -> 3"
	"source/Shaders.cpp|vec2 u = Air * a * ( grad / HalfSize ) / ( w * w * w );|vec2 u = Air * a * ( grad / HalfSize ) / ( w * w + w );|--powder|GLSL: the powder current over w^2 + w, * -> +"
	"source/Shaders.cpp|Ay = ( top - bottom ) * perUnit;|Ay = ( top + bottom ) * perUnit;|--powder|GLSL: the field's y derivative from the sum of its rows, - -> +"
	"source/Shaders.cpp|vec2 t  = ( u + 1.0 ) * ( 0.5 * float( kFieldSize ) ) - 0.5;|vec2 t  = ( u + 1.0 ) * ( 0.5 * float( kFieldSize ) ) - 0.6;|--threshold|GLSL: the lattice read a tenth of a texel off, 5 -> 6"
	"source/Shaders.cpp|sum += Coef[ k ] * texelFetch( Modes, ivec3( p, k ), 0 ).r;|sum += Coef[ k ] * texelFetch( Modes, ivec3( p, 0 ), 0 ).r;|--level|GLSL: every mode's coefficient on the first mode's shape, k -> 0"
	"source/Shaders.cpp|x *= ( 2.0 - r ) / r;|x *= ( 3.0 - r ) / r;|--nodal|GLSL: the disc's rim throwing grains off the plate, 2 -> 3"
	"source/Shaders.cpp|float cover = clamp( 0.5 * PointSize - r + 0.5, 0.0, 1.0 );|float cover = clamp( 0.6 * PointSize - r + 0.5, 0.0, 1.0 );|--splat|GLSL: the grain's disc 20% wide, 5 -> 6"
	"source/Eidophone.cpp|const float sx = static_cast< float >( 2.0 * frame.plateScale / width );|const float sx = static_cast< float >( 3.0 * frame.plateScale / width );|--splat|C++: the grains drawn 1.5 x wide of where they are, 2 -> 3"
	"source/Shaders.cpp|float covS   = ( 1.0 - exp( -dens.x ) ) * mask;|float covS   = ( 1.0 - exp( -dens.y ) ) * mask;|--alpha|GLSL: the sand's cover read from the powder's channel, x -> y"
	"source/Shaders.cpp|A = A * ( 1.0 - covS ) + covS;|A = A * ( 1.0 + covS ) + covS;|--alpha|GLSL: the sand laid over the clip's alpha with + covS, - -> +"
	"source/Shaders.cpp|if( IsOver == 1 && MixAmount <= 0.0 )|if( IsOver == 1 && MixAmount >= 0.0 )|--alpha|GLSL: the clip passed through at every Mix, < -> >"
	"source/Shaders.cpp|float band   = Cue.a * clamp( CueWidth - closest, 0.0, 1.0 ) * mask;|float band   = Cue.a * clamp( CueWidth + closest, 0.0, 1.0 ) * mask;|--device|GLSL: the cue over the whole plate, not its rim, - -> +"
	"source/Plate.cpp|const Complex G    = f / Complex( w * w - omegaD * omegaD, 2.0 * zw * omegaD );|const Complex G    = f / Complex( w * w + omegaD * omegaD, 2.0 * zw * omegaD );|--plate|C++: the resonator's denominator w^2 + w_d^2, - -> +"
	"source/Plate.cpp|const double h  = decay * ( h0 * c + ( hd0 + zw * h0 ) / wd * s );|const double h  = decay * ( h0 * c + ( hd0 - zw * h0 ) / wd * s );|--plate|C++: the free part's sine term with - zeta w h0, + -> -"
	"source/Plate.cpp|coupling[ static_cast< size_t >( k ) ] = table->W( mode, config.driveAt, 0.0 );|coupling[ static_cast< size_t >( k ) ] = table->W( mode, config.driveAt, 1.0 );|--plate|C++: the driver at the edge, not on the axis, 0 -> 1"
	"source/Plate.cpp|const double x = -1.0 + 2.0 * i / ( kPeakGrid - 1 ), y|const double x = -1.0 + 3.0 * i / ( kPeakGrid - 1 ), y|--level|C++: Gain's peak looked for half off the plate, 2 -> 3"
	"source/Modes.cpp|at( m.C, l, j ) + at( m.C, k, i ) * at( m.C, j, l ) );|at( m.C, l, j ) - at( m.C, k, i ) * at( m.C, j, l ) );|--modes|C++: the stiffness's nu term with its transpose subtracted, + -> -"
	"source/Modes.cpp|b.a2 = l * l * l * Jp + g * n2 * ( l * Jp - J );|b.a2 = l * l * l * Jp - g * n2 * ( l * Jp - J );|--circle|C++: the circle's shear condition with - ( 1 - nu ) n^2, + -> -"
	"source/Audio.h|constexpr double kPeakRelease = 8.0;|constexpr double kPeakRelease = 9.0;|--audio|C++: the running peak released at 9 s, 8 -> 9"
	"source/Audio.cpp|const double u       = ( j + 0.5 ) / std::max( 1, bins );|const double u       = ( j + 0.6 ) / std::max( 1, bins );|--audio|C++: a bin's centre a tenth of a bin high, 5 -> 6"
	"source/Audio.cpp|else if( value == earshot::deviceKey( earshot::kDefaultDevice ) )|else if( value != earshot::deviceKey( earshot::kDefaultDevice ) )|--device|C++: every device opened as the system default, = -> !"
	"source/Eidophone.cpp|substepClock       = std::max( 0.0, total - substeps * shaders::kSubstep );|substepClock       = std::max( 0.0, total + substeps * shaders::kSubstep );|--clock|C++: the substep clock keeping what it ran, - -> +"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -Rp "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
ln -s "$REPO/external/earshot" "$tree/external/earshot"
printf 'building the scratch tree once (arm64)...\n'
cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
cmake --build "$tree/build" --target edtest -j8 >/dev/null 2>&1
cp -Rp "$tree/build" "$WORK/pristine"
sleep 1.1

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	rm -rf "$tree/build"
	cp -Rp "$WORK/pristine" "$tree/build"
	for f in source/*; do cp -p "$REPO/$f" "$tree/$f"; done

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	if ! cmake --build "$tree/build" --target edtest -j8 >"$WORK/build.log" 2>&1; then
		printf '   ok    the mutant does not compile (counted as caught)\n'
		caught=$(( caught + 1 ))
		continue
	fi
	status=0
	limit 300 "$tree/build/edtest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
