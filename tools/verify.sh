#!/usr/bin/env bash
#
# Everything, in the order that fails fastest.
#
# The build is universal on purpose. An arm64-only bundle builds and tests
# perfectly well here and then fails to load in an Intel Resolume, and the
# build log calls it a success either way -- so the architecture is checked
# with lipo, never with the log.
#
# No step opens an audio device: edtest installs a scripted earshot backend
# before anything is constructed, and oxbow runs with EIDOPHONE_NO_DEVICES=1,
# under which the plugin never asks CoreAudio even for its device list.
#
#     OXBOW=/path/to/oxbow tools/verify.sh [BUILD_DIR]
#
# oxbow is required: the one step that loads the bundles the way a host does.
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$REPO/build-verify}"

cd "$REPO"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
fail() { printf '\033[31mFAIL\033[0m %s\n' "$1"; exit 1; }

OXBOW="${OXBOW:-$HOME/Projects/resolume/oxbow/build/oxbow}"
[[ -x "$OXBOW" ]] || fail "no oxbow at $OXBOW: set OXBOW=... (verification needs a host's reading of the bundles; there is no skip)"

#---------------------------------------------------------------------------
# GLSL reserved words, declared as identifiers.
#
# glslc catches these, but glslc is optional -- so the trap most likely to be
# walked into gets its own grep that always runs. The list is GLSL 4.10's
# keywords reserved for future use (3.6) plus the ones already bitten:
# `packed` passed Apple's compiler and glslc and failed only Mesa (atrac);
# noise1..noise4 are built-ins a user function is silently shadowed by (dice).
#---------------------------------------------------------------------------
reserved_words() {
	local words="common partition active asm class union enum typedef template this resource goto inline noinline
	             public static extern external interface long short half fixed unsigned superp input output filter
	             sizeof cast namespace using packed sample patch layout flat smooth noperspective precise
	             noise1 noise2 noise3 noise4"
	local bad=0 word
	for word in $words; do
		if grep -nE "(float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|mat[234])[[:space:]]+$word[[:space:]]*[;=,)(]" \
		            source/Shaders.cpp >/dev/null 2>&1; then
			printf '   "%s" is declared as an identifier and is a GLSL reserved word\n' "$word"
			bad=$(( bad + 1 ))
		fi
	done
	# far and near are reserved in HLSL-flavoured compilers and were macros on
	# one Windows driver; M_PI is not GLSL at all (the house rule: kPi).
	if grep -nE '(^|[^A-Za-z_])(far|near|M_PI)([^A-Za-z_0-9]|$)' source/Shaders.cpp | grep -vE '^[0-9]+:[[:space:]]*//' ; then
		printf '   far, near or M_PI appears in the GLSL\n'
		bad=$(( bad + 1 ))
	fi
	[ "$bad" -eq 0 ] && printf '   none of the reserved words is used as an identifier; no far, near or M_PI\n'
	return "$bad"
}

step "GLSL reserved words"
reserved_words || fail "a GLSL reserved word is used as an identifier"

#---------------------------------------------------------------------------
step "GLSL: nothing WebGL2 lacks"
#---------------------------------------------------------------------------
# The fleet's browser demos run the shaders unedited in WebGL2 (GLSL ES 3.00),
# and the kit's port() translates spellings, never maths. Eidophone's demo
# (demo/, AGENTS.md "Browser demo") runs them so; the rule keeps it possible. These
# built-ins arrived in GLSL 4.00 / ES 3.10 and compile everywhere here but
# there: findMSB was in ferrofluid's panel shader until 2026-10-06.
if grep -nE '(^|[^A-Za-z_])(findMSB|findLSB|bitCount|bitfieldExtract|bitfieldInsert|bitfieldReverse|uaddCarry|usubBorrow|umulExtended|imulExtended|frexp|ldexp|fma|textureGather|textureQueryLevels|packUnorm4x8|unpackUnorm4x8)[[:space:]]*\(' source/Shaders.cpp | grep -vE '^[0-9]+:[[:space:]]*//' ; then
	fail "a built-in GLSL ES 3.00 does not have is in the GLSL"
fi
echo "   none"

#---------------------------------------------------------------------------
step "Shaders"
#---------------------------------------------------------------------------
tools/glslc.sh || fail "a shader does not compile"

#---------------------------------------------------------------------------
step "Browser demo's copies"
#---------------------------------------------------------------------------
# demo/shaders.js must be source/Shaders.cpp's GLSL character for character,
# with the order Assemble(), SplatVertex() and InitGL join it in; every uniform
# must be set by the setter its type takes (WebGL2 drops a wrong one silently);
# the glue's track and --bins buffer must be edtest's; no served file may name a
# device API; and the .wasm's recorded inputs must still hash as they did when
# demo/tools/build-wasm.sh built it (rebuilding needs emscripten).
if [[ -f demo/tools/check_shaders.py ]]; then
	python3 demo/tools/check_shaders.py || fail "the demo's copies have drifted from the plugin's"
else
	echo "   skipped: no demo/"
fi

#---------------------------------------------------------------------------
step "Submodule"
#---------------------------------------------------------------------------
if [[ ! -f external/ffgl/CMakeLists.txt ]]; then
	fail "FFGL SDK missing -- run: git submodule update --init --recursive"
fi
pin="$(git -C external/ffgl rev-parse --short=7 HEAD)"
[[ "$pin" == "b1afaf9" ]] || fail "FFGL SDK at $pin, not the fleet's b1afaf9"
echo "ok   FFGL SDK pinned at $pin"

#---------------------------------------------------------------------------
step "earshot snapshot"
#---------------------------------------------------------------------------
# external/earshot is `git archive` of one commit, unmodified (SNAPSHOT says
# which and whether anything was patched). Where the earshot checkout is here,
# the snapshot is compared with that commit file for file.
snap="$( sed -n 's/^Commit:[[:space:]]*\([0-9a-f]\{7,\}\).*/\1/p' external/earshot/SNAPSHOT )"
[[ -n "$snap" ]] || fail "external/earshot/SNAPSHOT names no commit"
grep -q "$snap" AGENTS.md || fail "AGENTS.md does not record the earshot commit $snap"
EARSHOT="${EARSHOT:-$HOME/Projects/resolume/earshot}"
if git -C "$EARSHOT" cat-file -e "$snap^{commit}" 2>/dev/null; then
	tmp="$( mktemp -d )"
	git -C "$EARSHOT" archive "$snap" | tar -x -C "$tmp"
	diff -r -x SNAPSHOT "$tmp" external/earshot >/dev/null || { diff -rq -x SNAPSHOT "$tmp" external/earshot | head; rm -rf "$tmp"; fail "external/earshot differs from earshot $snap"; }
	rm -rf "$tmp"
	echo "ok   external/earshot is earshot $snap, file for file"
else
	echo "ok   SNAPSHOT and AGENTS.md agree on $snap (no earshot checkout at $EARSHOT to compare the files with)"
fi

#---------------------------------------------------------------------------
step "Build (universal)"
#---------------------------------------------------------------------------
log="$( mktemp )"
cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >"$log" 2>&1 || { cat "$log"; fail "configure"; }
cmake --build "$BUILD" -j8 >"$log" 2>&1 || { tail -40 "$log"; fail "build"; }
# The SDK's own FFGLUtilities.cpp warns about RAND_MAX; anything else is ours
# (earshot's snapshot included: it is built here, so its warnings are ours too).
if grep -E 'warning:' "$log" | grep -v 'external/ffgl' | grep -q .; then
	grep -E 'warning:' "$log" | grep -v 'external/ffgl' | sed 's/^/   /'
	fail "a warning in this repo's code"
fi
rm -f "$log"
echo "ok   built, no warnings outside the SDK"
EDTEST="$BUILD/edtest"

#---------------------------------------------------------------------------
step "Bundles"
#---------------------------------------------------------------------------
declared="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1 )"
grep -q "versionFallback = \"v$declared\"" source/StoatworksAbout.h || fail "StoatworksAbout.h's versionFallback is not v$declared"
check_bundle() {
	local name="$1" id="$2" display="$3" ffid="$4" kind="$5"
	local bundle="$BUILD/$name.bundle" binary="$BUILD/$name.bundle/Contents/MacOS/$name"
	[[ -f "$binary" ]] || fail "no binary at $binary"
	local arches; arches="$(lipo -archs "$binary")"
	[[ "$arches" == *arm64* ]]  || fail "$name: no arm64 slice (got: $arches)"
	[[ "$arches" == *x86_64* ]] || fail "$name: no x86_64 slice (got: $arches)"
	# Captured, then matched from a herestring -- never `nm ... | grep -q`: under
	# pipefail the grep's early exit SIGPIPEs nm and fails the pipeline.
	local symbols; symbols=$( nm -gU "$binary" 2>/dev/null || true )
	grep -q '_plugMain' <<<"$symbols" || fail "$name: plugMain not exported"
	local plist="$bundle/Contents/Info.plist"
	[[ -f "$plist" ]] || fail "$name: no Info.plist"
	read_plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist" 2>/dev/null || true; }
	[[ "$( read_plist CFBundleIdentifier )" == "$id" ]] || fail "$name: bundle id is '$( read_plist CFBundleIdentifier )'"
	[[ "$( read_plist CFBundleExecutable )" == "$name" ]] || fail "$name: CFBundleExecutable is wrong"
	[[ "$( read_plist CFBundlePackageType )" == "BNDL" ]] || fail "$name: CFBundlePackageType is not BNDL"
	[[ "$( read_plist CFBundleVersion )" == "$declared" ]] || fail "$name: plist version != CMakeLists $declared"
	codesign --force --sign - --timestamp=none "$bundle" >/dev/null 2>&1 || fail "$name: could not be ad-hoc signed"
	codesign --verify --deep --strict "$bundle" >/dev/null 2>&1 || fail "$name: the ad-hoc signature does not verify"
	echo "ok   $name: $arches, plugMain exported, $id, BNDL, v$declared, ad-hoc signed and verified"
	# What a host reads: id, name, type. The FFGL name field is char[16] and not
	# null-terminated, so only something reading it back as a host does notices
	# a truncation.
	local probe; probe="$( EIDOPHONE_NO_DEVICES=1 "$OXBOW" probe "$bundle" 2>&1 || true )"
	printf '%s\n' "$probe" | sed -n '1,3p' | sed 's/^/   /'
	grep -q "id:          $ffid" <<<"$probe" || fail "oxbow did not read the id $ffid"
	grep -q "name:        $display\$" <<<"$probe" || fail "oxbow did not read the name '$display'"
	grep -q "type:        $kind" <<<"$probe" || fail "oxbow did not read the type as $kind"
	local selftest; selftest="$( EIDOPHONE_NO_DEVICES=1 "$OXBOW" selftest "$bundle" 2>&1 || true )"
	grep -q "selftest:    PASS" <<<"$selftest" || { printf '%s\n' "$selftest" | tail -5; fail "oxbow selftest failed on $name"; }
	echo "ok   a host reads $display $ffid $kind, and oxbow selftest renders through it"
}
check_bundle "Eidophone" "com.stoatworks.ffgl.eidophone" "SW Eidophone" "ED01" "source"
check_bundle "Eido Over" "com.stoatworks.ffgl.eidophone.over" "SW Eido Over" "ED02" "effect"

#---------------------------------------------------------------------------
step "Checks (this Mac's GPU, 1280x720 and 320x180)"
#---------------------------------------------------------------------------
# Every claim the README makes, in the order the README makes them. Each GL
# check runs at 1280x720 and 320x180 itself, except --level and --clock
# (320x180: the field lattice and the clock do not depend on the raster),
# --resize (640x360 to 320x180 and 320x240) and --state (GL state at
# 320x180). modes, circle, plate, audio, names and cues need no GL context.
CHECKS="modes circle plate audio device select threshold hop nodal powder level freeze redraw alpha splat clock resize state names cues"
for check in $CHECKS; do
	if out=$( "$EDTEST" --$check 2>&1 ); then
		printf '   ok   edtest --%s: %s checks\n' "$check" "$( printf '%s\n' "$out" | grep -c '^  ok' )"
	else
		printf '%s\n' "$out" | grep -E 'FAIL' | sed 's/^/      /'
		fail "edtest --$check"
	fi
done

#---------------------------------------------------------------------------
step "Software renderer (CI's, at 320x180)"
#---------------------------------------------------------------------------
# The cheap checks on Apple's software renderer, which is what a GPU-less CI
# runner falls back to: the GL state, the gate bit for bit, the hop, the
# splat against its CPU replica (sharp here: 10 subpixel bits and float
# blending, where the GPU reports 4 and blends in half), the Over's alpha,
# the redraw and the clock. Every GL check passed there on 2026-11-16 (about
# eight minutes in all, AGENTS.md); the rest take minutes each, and --device's
# 250 ms frame limit has less margin there (90 ms frames).
SOFTWARE="state threshold hop splat alpha redraw clock"
for check in $SOFTWARE; do
	if out=$( EDTEST_RENDERER=software "$EDTEST" --$check --size 320x180 2>&1 ); then
		printf '   ok   edtest --%s (software): %s checks\n' "$check" "$( printf '%s\n' "$out" | grep -c '^  ok' )"
	else
		printf '%s\n' "$out" | grep -E 'FAIL' | sed 's/^/      /'
		fail "edtest --$check on the software renderer -- run: EDTEST_RENDERER=software $EDTEST --$check --size 320x180"
	fi
done

#---------------------------------------------------------------------------
step "Offline (what CI runs)"
#---------------------------------------------------------------------------
"$EDTEST" --offline >/dev/null || fail "edtest --offline"
echo "ok   edtest --offline"

#---------------------------------------------------------------------------
step "Pipe"
#---------------------------------------------------------------------------
# The fleet's --pipe frame format, which the video renders through, for both
# plugins. Two and a half frames in must be exactly two out and a clean exit --
# a partial frame is the end of the stream, never a frame -- --film N must be N
# frames, a cue naming no parameter and a --set that is not a number or an
# option must be refused, and a reader that hangs up must end the run with
# exit 1, not SIGPIPE's silent 141.
frame=$(( 64 * 36 * 4 ))
raw=$( mktemp ); cues=$( mktemp ); out=$( mktemp )
for flavour in "" "--over"; do
	label=${flavour:-source}
	head -c $(( frame * 5 / 2 )) /dev/zero > "$raw"
	status=0
	"$EDTEST" $flavour --pipe --size 64x36 < "$raw" > "$out" 2>/dev/null || status=$?
	got=$( wc -c < "$out" | tr -d ' ' )
	[[ "$status" -eq 0 && "$got" == "$(( frame * 2 ))" ]] \
		|| fail "$label: 2.5 frames in gave $got bytes out (want $(( frame * 2 ))), exit $status"
	status=0
	"$EDTEST" $flavour --film 3 --size 64x36 > "$out" 2>/dev/null || status=$?
	got=$( wc -c < "$out" | tr -d ' ' )
	[[ "$status" -eq 0 && "$got" == "$(( frame * 3 ))" ]] || fail "$label: --film 3 gave $got bytes, exit $status"
	# Read from a file, not a pipe: a writer killed by SIGPIPE would fail the
	# pipeline whatever edtest did, and the refusal would pass for the wrong reason.
	printf '0 No Such Control 0.5\n' > "$cues"
	status=0
	"$EDTEST" $flavour --pipe --size 64x36 --script "$cues" < "$raw" >/dev/null 2>&1 || status=$?
	[[ "$status" -eq 2 ]] || fail "$label: a cue naming no parameter gave exit $status, not 2"
	status=0
	"$EDTEST" $flavour --film 1 --size 64x36 --set "Shape=Triangle" >/dev/null 2>&1 || status=$?
	[[ "$status" -eq 2 ]] || fail "$label: --set 'Shape=Triangle' (no such option) gave exit $status, not 2"
	status=0
	"$EDTEST" $flavour --film 1 --size 64x36 --set "Shape=Circle" >/dev/null 2>&1 || status=$?
	[[ "$status" -eq 0 ]] || fail "$label: --set 'Shape=Circle' (an option by name) gave exit $status"
	# Music through the pipe: a tone fed in step with the frames.
	status=0
	"$EDTEST" $flavour --film 3 --size 64x36 --tone 0 > "$out" 2>/dev/null || status=$?
	got=$( wc -c < "$out" | tr -d ' ' )
	[[ "$status" -eq 0 && "$got" == "$(( frame * 3 ))" ]] || fail "$label: --film 3 --tone 0 gave $got bytes, exit $status"
	head -c $(( frame * 20 )) /dev/zero > "$raw"
	set +e
	"$EDTEST" $flavour --pipe --size 64x36 < "$raw" 2>/dev/null | head -c 1 >/dev/null
	s1=${PIPESTATUS[0]}
	"$EDTEST" $flavour --film 20 --size 64x36 2>/dev/null | head -c 1 >/dev/null
	s2=${PIPESTATUS[0]}
	set -e
	[[ "$s1" -eq 1 && "$s2" -eq 1 ]] || fail "$label: a closed stdout gave exit $s1 (--pipe), $s2 (--film), not 1"
	echo "ok   $label: 2.5 frames in, exactly 2 out; --film 3 is 3, with a tone too; a bad cue and a bad option refused (exit 2); | head -c 1 exits 1, not 141"
done
rm -f "$raw" "$cues" "$out"

#---------------------------------------------------------------------------
step "Negative controls"
#---------------------------------------------------------------------------
# Every check above, against a model that is deliberately wrong, required to
# FAIL. A check that cannot fail is not a check.
if out=$( "$EDTEST" --negative 2>&1 ); then
	printf '%s\n' "$out" | grep -E '^negative controls' | sed 's/^/   /'
else
	printf '%s\n' "$out" | grep -E 'PASSED against' | sed 's/^/      /'
	fail "a negative control went undetected"
fi

#---------------------------------------------------------------------------
step "Mutants"
#---------------------------------------------------------------------------
tools/mutate.sh | grep -E '^== mutant|^   (ok|FAIL)|^mutants' || fail "a mutant survived"

#---------------------------------------------------------------------------
step "Dead controls"
#---------------------------------------------------------------------------
# The only thing that catches a uniform whose name does not match the C++.
python3 tools/sweep.py --build "$BUILD" || fail "a dead control"

#---------------------------------------------------------------------------
step "Cost"
#---------------------------------------------------------------------------
"$EDTEST" --bench

printf '\n\033[32mall green\033[0m\n'
