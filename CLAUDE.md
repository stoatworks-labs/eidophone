# eidophone

A Chladni plate with sand on it, sounded by the music, for Resolume Arena/Avenue, as two
FFGL plugins from one core: a free Kirchhoff plate (square by Rayleigh–Ritz, circle by
Bessel functions) driven at a point, each mode a damped oscillator integrated exactly; sand
thrown only where the plate's acceleration beats g, powder carried to the antinodes. Pitch
and level from earshot (snapshot in `external/earshot`). C++/GLSL, CMake MODULE → universal
`.bundle` (macOS) + Windows `.dll` (built on GitHub's Windows runner). MIT. Public:
`github.com/stoatworks-labs/eidophone`.

| | id | display name | bundle | type |
| --- | --- | --- | --- | --- |
| the plate on black | `ED01` | `SW Eidophone` | `Eidophone.bundle`, `com.stoatworks.ffgl.eidophone` | source |
| the clip printed on the plate | `ED02` | `SW Eido Over` | `Eido Over.bundle`, `com.stoatworks.ffgl.eidophone.over` | effect |

Read `AGENTS.md` before touching the mode solvers (`source/Modes.cpp`), the oscillators
(`source/Plate.cpp`), the audio law (`source/Audio.cpp`), the grain update or the splat
(`source/Shaders.cpp`), or the Over's alpha. The design is
`~/Projects/resolume/specs/SPEC-eidophone.md`; its "As built" section says where the code
departs from it.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64` (iteration builds here are
  `build-arm64`)
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (both bundles into Arena's Extra Effects)
- Render offline: `./build/edtest --out /tmp/eido.png` (the source on its defaults, a sine at
  the default plate's centre mode nearest 400 Hz, 3 s in)
- The Over: `./build/edtest --over --out /tmp/over.png` (the harness's card as the clip),
  or `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/edtest --list` (`--over` for the effect's)
- Set anything by name: `./build/edtest --set "Shape=Circle" --set "Grain=Both"`
  (a standard control takes the host's 0..1; an option takes its name; a bad name exits 2)
- Press something later: `./build/edtest --at 100 "Pour=1"`
- Music: `--wav F` (fed through earshot's `feed()` in step with the frames), `--tone HZ`
  (a sine at −12 dBFS; 0 picks the centre mode nearest 400 Hz), `--bins J` (no feed: the
  host FFT buffer alone, bin J loud, what Resolume's FFT drives)
- A mode by hand: `--pure K G` (mode K of the table at G g peak, no audio)
- Clock: `--fps N` (default 60)
- Film: `./build/edtest --film 450 --size 1280x720 --wav song.wav | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -vf fps=60 -f rawvideo -pix_fmt rgba - | ./build/edtest --over --pipe --size WxH --wav in.wav | ffmpeg …`
  (a partial last frame is dropped; a reader that hangs up ends the run with exit 1)
- Cue sheets: `--script cues`, `frame Name value` per line; an option by its name; standard
  controls ramp between keys, options and events step.

## Verify
- Everything: `OXBOW=… tools/verify.sh [BUILD_DIR]` (reserved-word, far/near/M_PI and WebGL2
  greps, `glslc`, the submodule pin, the earshot snapshot against SNAPSHOT, AGENTS.md and an
  earshot checkout (`EARSHOT`, default `~/Projects/resolume/earshot`), a fresh universal
  build in `build-verify` with no warnings, both bundles through lipo, nm, plist, ad-hoc
  signature, `oxbow probe` and `selftest`; every check below;
  seven on Apple's software renderer; `--offline`; `--pipe` for both; the negative controls;
  26 mutants; the sweep; the bench): about two minutes on this Mac.
- **Set `OXBOW`** to a built `oxbow` binary. verify.sh fails without it, never "skipped".
  Build it out of tree: `cmake -S ~/Projects/resolume/oxbow -B <scratch>/oxbow-build &&
  cmake --build <scratch>/oxbow-build` (never inside ~/Projects).
- **Set `TMPDIR`** to a writable scratch directory in a sandboxed Claude session, or the
  heredocs in `glslc.sh` and `mutate.sh` hang.
- **No step opens an audio device.** Every AudioInput the harness makes lists a scripted
  backend's devices; verify runs `oxbow` with `EIDOPHONE_NO_DEVICES=1`.
- **The plate** (no GL): `--modes` (the square against Iguchi, Leissa's bounds, Ritz 1909,
  the twist, convergence, orthonormality), `--circle` (Bessel roots against a polar Ritz,
  Leissa's Tables 2.5–2.7, Chladni's law), `--plate` (steady state, ring-down, frame rate,
  the centre's symmetry, the rigid shake), `--audio` (pitch, held ω, the seed, the loudness
  and its running peak, the host FFT's bin).
- **The grains** (GL, 1280x720 and 320x180 each): `--threshold` (under g, bit for bit),
  `--hop` (the walk's drift and variance), `--nodal` (settled sand on the zero set),
  `--powder` (the current exactly, heaps at the antinodes), `--freeze` (0.7 g moves nothing;
  silence freezes), `--select` (a sine at a mode's frequency rings that mode), `--level`
  (Gain is the plate's peak, in g), `--splat` (the density against a CPU replica of GL's
  point rule).
- **The rest**: `--device` (the menu by name, opens off the render thread, the rim cues),
  `--alpha`, `--redraw`, `--clock` (a 499,000,000 ms host clock), `--resize`, `--state`,
  `--names` (no GL), `--cues` (no GL).
- `--offline` runs the no-GL checks and their negatives (what CI would run).
- The checks can fail: `./build/edtest --negative` (21 wrong models), `tools/mutate.sh`
  (26 one-character mutants, each built arm64-only and run at 320x180 under a 300 s alarm).
- No dead controls: `python3 tools/sweep.py --build build` (both plugins; `--bare` drops
  the per-control contexts and lists the controls that only act with another one set).
- Render cost: `./build/edtest --bench` (720p, 1080p and 4K at the defaults; 16,384 and
  262,144 grains; Both; the Over).
- `EDTEST_RENDERER=software` runs any of it on Apple's software renderer (slow: every GL
  check passed there on 2026-11-16 at 320x180, about eight minutes in all, so verify runs
  `state threshold hop splat alpha redraw clock`).
- `ED_DEBUG=1` prints the hop's per-step means, the nodal check's worst grain and the
  splat's sums.
- Browser demo (`demo/`, AGENTS.md "Browser demo"): after ANY change under `source/` or
  `external/earshot/`, rerun `demo/tools/build-wasm.sh` (emscripten) and commit its outputs;
  after a shader change, `python3 demo/tools/check_shaders.py --write`. verify.sh fails until
  you do. Against edtest: `python3 demo/tools/crosscheck.py --edtest build/edtest`.

## Notes
- **Units.** Metres, seconds, m/s². Plate coordinates are [−1, 1] over the half-width L
  (the circle's radius); the drive is on +x at Drive At × L. Gain is in g
  (9.80665 m/s², exact).
- **The modes run on the CPU in double** (`source/Modes.cpp`, `source/Eigen.cpp`): the square
  by Rayleigh–Ritz in normalised Legendre polynomials, 36 degrees a side, four parity
  classes, Householder + QL; the circle from eq. 2.14's roots, J_n and I_n by quadrature
  (libc++ has no `std::cyl_bessel_j`). Built once per Shape and ν and shared
  (`TableFor`). 64 elastic modes plus the rigid pair.
- **The oscillators run on the CPU in double** (`source/Plate.cpp`): exact per frame, no
  timestep. The GPU only sums Σ a_k W_k into a 256² RG32F lattice, and |A| bilinear from that
  lattice IS the model's peak acceleration.
- **The audio law** (`source/Audio.cpp`): ω from earshot's voiced pitch, held; loudness the
  band envelopes' RMS; relative to an 8 s released running peak; F₀ = Gain · g · relative /
  `PeakPerNewton(ω)`. AGENTS.md says why it isn't the spec's.
- **Grains** are a 512² RGBA32F ping-pong state (x, y, a/g, kind), updated in 1/480 s
  substeps (at most 32 a frame), PCG-hashed per (grain, substep); splatted as whole-pixel
  point squares with an anti-aliased disc into an RG16F density at the output raster.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element
  index; Pour and Rescan act on the rising edge. Device's elements are filled at run time
  (earshot's device list), valued by `deviceKey(name)`.
- GLSL reserved words (`patch sample input output filter common active half layout flat
  packed`, `noise1`–`noise4`) must not be identifiers, nor `far`, `near` or `M_PI`;
  `verify.sh` greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `eidophone_core` is an OBJECT library: each plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **The earshot snapshot** is earshot (`github.com/stoatworks-labs/earshot`) at `8124191`,
  unmodified (`external/earshot/SNAPSHOT`). Never edit it; change earshot upstream and take a
  new archive. Its own CLAUDE.md and AGENTS.md are earshot's as of that commit, status lines
  and all.
- **Generated, don't edit here:** `source/StoatworksAbout.h` (stoatworks-backend's
  `sync-about.py`, from the website's projects.json: the hook and the User guide button)
  and `ATTRIBUTIONS.md` (`sync-attributions.py`, from the backend's `derived.json`, which
  carries earshot's own credit row; miniaudio is found inside the snapshot). Registered
  2026-11-18; the About block makes it 24 and 25 parameters.
- **CI** (`.github/workflows/ci.yml`): macOS builds arm64 with the tools and runs `--list`,
  `--offline` and `glslc`; Windows builds both DLLs with MSVC and loads each as a host does
  (`tools/wincheck.ps1`, whose `-Params` changes whenever a control or the About block does).
  `release.yml` builds the universal bundles and the DLLs with their zips, `.dmg` and
  installer; dispatched by hand it builds without publishing, and a `v*` tag publishes.
  Actions only because the repo is public: a private copy must never enable them.
- Checkout: `~/Projects/resolume/eidophone`, public at `github.com/stoatworks-labs/eidophone`.

## Not done yet
- Inside Resolume only briefly: in Arena on the Mac (7.29.0) it loaded, drew and answered its
  drawing controls (2026-11-18), and Resolume's FFT threw the sand with `Beat 001.wav` playing
  on the layer below (2026-11-19; the first day's `Beat 001.mov` has no sound track). On Windows it
  has run only in win-lab's Arena (no GPU, no sound device), through the fleet's gate. No
  real capture device has fed it.
- No OpenFX port, no presets. The user guide is `docs/USER-GUIDE.md`; the browser demo is
  `demo/`.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/eidophone/eidophone.YYYY-MM-DD.log
    %LOCALAPPDATA%\eidophone\logs\eidophone.YYYY-MM-DD.log

`EIDOPHONE_LOG_DIR` overrides the folder. Every capture-state change is logged with
earshot's status string.
