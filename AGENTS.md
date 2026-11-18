# AGENTS.md: Eidophone

Why the code is the way it is. `CLAUDE.md` is the command reference; the design is
`~/Projects/resolume/specs/SPEC-eidophone.md`, whose "As built" section lists where this repo
departs from it. Built 2026-11-16 from the spec, in one session; public at
`github.com/stoatworks-labs/eidophone` from 2026-11-18.

## The idea

**A plate rings in the shapes it can ring in; sand sits still only where the plate is still
enough to hold it.**

A free Kirchhoff plate (square or circle, aluminium, brass or steel) is pushed at one point by
a force that follows the music. Its modes are computed once per Shape and Poisson's ratio: the
square by Rayleigh–Ritz in a tensor basis of normalised Legendre polynomials (36 degrees a
side, four parity classes, Householder and QL in double), the circle exactly, from the roots
of Leissa's eq. 2.14 with J_n and I_n by quadrature. Each mode is a damped oscillator,
integrated exactly frame to frame (steady part plus free part, matched at the boundary: no
timestep). The GPU sums the modes' accelerations into a complex 256² lattice; the model's peak
acceleration at a point is |A| interpolated bilinearly from it, defined that way and nowhere
else.

A grain of sand where |A| ≤ g is copied bit for bit: Newton says it never leaves the plate,
so it does not move at all. Where |A| > g it is thrown, at v = √(a² − g²)/ω, and random-walks
with D = H²v³/8g and a drift of −(Hv/2) down the gradient of a, toward the nodal lines. Powder
rides Faraday's air currents up the gradient instead, u = P a∇a/(L ω³), and gathers at the
antinodes. The grains are points in a 512² RGBA32F state texture, splatted into an RG16F
density at the output raster; nothing about the sand depends on the raster.

What falls out (the README lists it): the figure changes with the note, louder draws thinner
lines, quiet freezes the figure exactly, a new note half-redraws the old one, powder draws the
negative, a loud note far from every mode smears it all, and high notes draw slowly.

## The earshot snapshot

`external/earshot/` is earshot (`github.com/stoatworks-labs/earshot`) at commit **`8124191`**
("estest: a negative control for every check, enforced"), taken 2026-11-16 with `git archive
8124191`, **unmodified, no patches**. `external/earshot/SNAPSHOT` records the same. verify.sh
checks that SNAPSHOT and this file agree on the commit, and, when an earshot checkout is
present (`EARSHOT`, default `~/Projects/resolume/earshot`), that the snapshot's files are byte-for-byte that commit's. To update: take a new archive,
change both records, rerun verify. Never edit files under `external/earshot/`; change earshot
upstream.

What eidophone uses from it: the Engine (pitch with NSDF clarity, band envelopes, partials,
the host-FFT fallback), the device list and `deviceKey`, `feedBegin`/`feed` for WAVs and test
tones, and `earshot::testing::CaptureBackend` so no step of the harness opens a device.

## Decisions taken without asking (the brief says decide and write it down)

- **The drive follows the music's level over its own recent peak, and Gain is in g.** The
  spec's law (F₀ = Gain newtons × 10^(levelDb/20), voiced frames only, F₀ = 0 otherwise)
  left the plate still through most of Resolume's demo music. earshot reads those WAVs as
  quiet (mean level 0.009 for Synth 001, 0.101 for Beat 001) and rarely voiced at the spec's
  Clarity 0.80 (Beat 001: 13 frames of 446; Synth 001: 87). As built:
  - **ω** is 2π × earshot's pitch on a voiced frame and is **held** between notes. Before the
    first voiced frame, the strongest partial in 40–2000 Hz seeds it. A new earshot stream
    (a new device, a new feed) forgets it.
  - **Loudness** is the RMS of earshot's band envelopes (its 5 ms attack, 150 ms release),
    whether or not the frame is voiced, gated to 0 under 0.001.
  - **Relative** is loudness over a running peak that releases with an 8 s time constant
    (floor 0.01): the music's dynamics against its own recent loudest.
  - **F₀** = Gain · g · relative / `PeakPerNewton(ω)`, the steady-state peak acceleration per
    newton at ω over a 25² grid of the plate (the disc: the points inside it and 100 on the
    rim). So **Gain is the plate's peak acceleration, in g, at the loudest recent level**,
    at any pitch: `--level` measures 0.943–0.990 of it.
  - A passage more than 20·log₁₀(Gain) dB under the recent peak (21.6 dB at the default
    12 g) leaves the plate under g and the figure frozen; silence under earshot's −60 dBFS
    gate does too.
- **Gain: 0.5–40 g, default 12 g**, geometric. The spec's 0.01–30 N meant nothing without
  knowing the plate; a level in g is what decides whether sand moves.
- **Size: 40 cm, not the spec's 30.** The demo music's notes, by earshot, sit at 65–123 Hz
  (Bass, Beat) and 262 Hz (Synth 003). At 30 cm the lowest centre-driven mode is near 67 Hz
  and the bass sits under or among the first one or two; at 40 cm (lowest near 37.5 Hz) it
  lands on the second to fourth and the sheets showed figures, the star for Beat and the
  corner arcs for Bass. Judged by eye on contact sheets (`d0`–`d6`, then the full pass below).
- **The Over's alpha is the grains laid over the clip's alpha** (Porter–Duff over,
  premultiplied), mixed by Mix: sand over the plate's surface (mask · a_clip), powder over
  that, so A = c_p + (1 − c_p)(c_s + (1 − c_s)·mask·a_clip), and out = mix(clip, (C/A, A),
  Mix). The spec's max(plateMask · a_clip, coverage) is not how two layers stack: a grain of
  coverage 0.5 on a clip at alpha 0.3 is 0.65 opaque, not 0.5. So sand on a transparent part
  of a DXV clip stays visible, the rest of the clip keeps its own alpha, outside the plate is
  transparent, the shadow darkens the surface without adding alpha, and Mix 0 is the clip
  bit for bit. Checked by `--alpha` on a flat 0.3 alpha (to 2.3e-6) and by eye on
  Resolume's `Metalive 01` (DXV with alpha): the alpha still is the clip's shape plus the
  sand.
- **View: Fit for the source, Fill for the Over** (the spec's). In Fill the square plate is
  cropped top and bottom to the 16:9 frame, so the clip fills the frame, and the same Amount
  is spread over about 3.5 times the screen area: the Over's sand reads sparser.
- **The splat draws whole-pixel point squares with an anti-aliased disc inside**, not
  fractional point sizes. Apple's GL rounds `gl_PointSize` to a whole pixel: a 2.6 px grain
  was drawn as a 3 px square and carried 33% too much mass. The point is now
  ceil(disc) + 2 px, the disc's edge a 1 px ramp, its weight (size/disc)² so a grain under
  1 px keeps its mass. `--splat` holds it against a CPU replica of GL's point rule.
- **Damping's Q 40 is an assumption** (a sand-loaded plate on a driver). Nothing published
  was found for it; the range is 5–500.
- **Hop H = 1, the drift fraction β = 1 and Air's P = 20 are chosen**, as the spec says,
  not derived.
- **The plate's lowest mode sits under earshot's floor.** earshot's pitch range starts at
  40 Hz (its window is three periods of the lowest pitch, 75 ms; below 40 it clamps), so the
  default plate's lowest centre-driven mode, near 37.5 Hz, can only be rung through the host
  FFT or by a note's partials. Left as is; a smaller plate moves every mode up.
- **The host FFT has no pitch**, and the menu says so: element 0 is
  "Resolume FFT (no pitch)". The plugin takes the strongest of the 64 bins (never bin 0) at
  its centre under Bin Law (44.1 kHz assumed) and its value as the loudness. Under Linear
  the bins are 345 Hz wide.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (adapted from haze's, `guide`
  empty, no user guide exists) and `ATTRIBUTIONS.md`.
- **Workflows only once public.** `ci.yml` and `release.yml` were sed-adapted from haze's
  first versions and had never run; before the first push they were brought up to
  mirrorball's (haze's current shape: macOS 15, vcpkg applocal off, both DLLs loaded as a
  host loads them by `tools/wincheck.ps1`, ATTRIBUTIONS.md in each zip, the installer by
  release-lib's `--ffgl`). The template had no `vcpkg.json`, so the Windows configure
  could never have found GLEW: it is mirrorball's now. Actions only because the repo is
  public.

## The traps actually hit

- **The spec's audio law drove nothing on real music** (the first decision above). Every
  synthetic check passed on it: a −12 dBFS sine is voiced and loud. Only the demo-clip pass
  showed a still plate. A check on synthetic audio alone would have shipped it.
- **Apple's GL rounds fractional point sizes.** The splat's mass came out 33% high at 2.6 px
  and exactly right at 3. Found by `--splat`'s mass check, fixed by drawing whole-pixel
  squares (a decision above).
- **The GPU's splat is coarse where the software renderer's is sharp.** This Mac's GPU
  reports `GL_SUBPIXEL_BITS` 4 and blends RG16F in half precision, losing up to n·2⁻¹⁰ for n
  points in a pixel; Apple's software renderer has 10 subpixel bits and blends in float. A
  1e-3 mass tolerance passed at 720p and failed at 320×180 on the GPU. The bound is now the
  half-float one, Σ n_p·v_p·2⁻¹⁰, and the per-pixel L1 is allowed four subpixel bits on the
  disc edges. On the software renderer the replica matches to 0.0005% of the mass.
- **The pour's noise was the first hop's noise.** With the pour seeded by Step, the first
  substep's Gaussian came from the same PCG stream as the poured positions; `--hop` caught a
  −0.15 σ pull down the gradient at Step 1. The pour is now salted apart
  (`pcg(index ^ pcg(Seed ^ 0xa511e9b3))`).
- **A level case fell under earshot's 40 Hz floor.** `--level`'s "below the lowest mode"
  case was 0.6 × the lowest mode, 28 Hz on the 40 cm plate: earshot read no pitch at all.
  It's now 45 Hz, just above the floor.
- **A tone's tail still voices.** After a tone stops, earshot's 75 ms pitch window still
  holds it for a frame or two, at a slightly different pitch (440.63 Hz after 440). `--audio`
  compares the held ω with the last voiced frame's, not with the tone's.
- **selectDevice("") ends a feed()** (woofer's trap, earshot's API): the plugin never calls
  it while feeding.
- **The feed overrides the device.** In the sweep, Device at both ends was identical because
  the harness's tone feed is what earshot analyses. Device and Bin Law are swept on the host
  FFT, `--bins 3`, with no feed.
- **Three mutants survived the first run.** The disc's rim throwing grains off the plate
  passed `--nodal` (a thrown grain off the plate was never on a nodal line, but never counted
  either): `--nodal` now requires 0 grains ever off the plate. The running peak's release
  (8 → 9 s) and a bin's centre (j + ½ → j + 0.6) passed `--audio`, which computed its
  expectations from the shipped constants: both are now literals (the 8 s, the 0.01 floor)
  and an independent bin-centre formula. Circular checks again (haze's lesson, relearned).
- **Freeze's probe fell outside the new Gain range** (0.02, from the newtons era): it's now
  Gain 1.0 g, and the check's label is in g.
- **Short demo clips end early.** Five of Shop74's clips are under 7.5 s; `ffmpeg -t 7.5`
  gave fewer than 225 frames and no last still. The demo pass loops them
  (`-stream_loop -1`).
- **`TMPDIR`**: in a sandboxed session a heredoc into python hangs under the system's
  `/var/folders` TMPDIR (the fleet's trap); glslc.sh and mutate.sh need it set.
- **macOS has no `timeout(1)`**: mutants run under `perl -e 'alarm N; exec @ARGV'`.
- **zsh has no `PIPESTATUS`**: the `| head -c 1` check lives in verify.sh, which runs under
  bash.
- **`make` has 1 s mtimes**: mutate.sh copies a pristine build with `cp -Rp`, restores with
  `cp -p` and sleeps 1.1 s so a mutated file is always newer than its object.
- **`oxbow` isn't built at the path verify.sh used to default to.** verify.sh now requires
  `OXBOW` and fails without it; it never prints "skipped".

## Verified, and assumed

Verified on this Mac (Apple M4 Max, macOS 26.4.1), 2026-11-16, by `tools/verify.sh` (all
green, 2 min 2 s) and the checks' own output. The grain checks give the same numbers at
1280×720 and 320×180 because the grains live in plate coordinates.

- the free square's modes: Iguchi's nine doubly-symmetric eigenvalues (ν 0.3) to 3.4e-5
  relative (allowed 1e-4); all 20 odd–odd diagonal-symmetric values at ν 0.225 and 0.3 inside
  Leissa's Table 4.67 bounds; the lowest at ν 0.225, 14.11608, inside 14.1028–14.1165; all six
  below Ritz's 1909 values; the twist w = xy to 1.6e-15; 36 against 42 degrees to 2.8e-9;
  orthonormal to 5.8e-15;
- the circle's: eq. 2.14's 72 roots against an independent polar Ritz solve to 6.6e-10;
  Table 2.6 to 0.31%, Table 2.5 to 0.74%, Table 2.7's radii to 0.0005; Chladni's asymptote
  monotone and within 0.37% at 10 circles;
- the plate: the steady state on G to 9.5e-16; the rigid translation F₀/M to 2.2e-16; the
  ring-down to 2.2e-19; a drive changing every 1/6 s the same at 24, 30, 60 and 240 fps to
  2.3e-12; driven at the centre, only the 19 doubly-symmetric modes respond (others 8.9e-17);
- audio: a 440 Hz sine at −20 dBFS gives 440.00 Hz and −20.00 dBFS; 20 dB down for 2 s,
  relative 0.1284 against the 8 s release's 0.1284; silence holds ω; a two-partial chord
  seeds 623.09 Hz, a 330 Hz sine then 330.00 Hz, held; the host FFT's strongest bin under
  each Bin Law, never bin 0;
- the menu: 17 elements, devices valued by `deviceKey(name)`, Missing amber (7.8e-6) and
  Silent red (4.1e-6) on the rim, a 0.3 s open never stalls a frame (the next frame 0.5 ms),
  a device found again by name on Rescan;
- select: a sine at a mode's frequency, through earshot, gives it 91.3%, 89.8% and 65.3% of
  the elastic energy (modes 7, 10, 15 at 160.30, 201.00, 371.35 Hz; each the largest);
- threshold: 757,980 grain-substeps under g, 0 changed in any bit; 4,484,878 over g, 0 stayed
  put (22 within the 1e-5 margin, not judged);
- hop: 614,228 thrown grain-substeps (pooled over 4 substeps), the residual's mean +0.0001 σ
  along the gradient and −0.0009 σ across, its variance 1.0001 σ²;
- nodal: square (two modes) and disc (two modes), 16,384 grains each, 100.00% settled on the
  nodal lines, 0 ever off the plate;
- powder: one substep under g, the worst grain 0.59 of its allowance off the law; with Both,
  92.9% of the powder within 0.2 of the antinodes (a uniform scatter: 12.6%) and 100% of the
  sand in the dead band;
- level: a steady sine at Gain 5 g settles at 0.943–0.990 × Gain at four pitches, and the
  lattice holds 1.0000 of the 25² grid's prediction;
- freeze: 10 s at a peak 0.700 g, 0 of 393,216 grain-readings changed; after 2 s at 2.8 g,
  8 s of silence move 0;
- alpha: Mix 0 bit-exact (0 of 3,686,400 floats); the alpha on the over law to 2.3e-6; the
  clip untouched where no grain lies; the source opaque;
- splat: the GPU against the CPU replica, mass at 0.21–0.33 of the half-float bound, L1
  0.12–0.27% of the mass;
- redraw 0 substeps at an unchanged host time; clock dt to 6.2e-11 s at 499,000,000 ms;
  resize 0 bytes, both plugins, two shapes; state 0 wrong; names 23 and 24 parameters; cues;
- `--pipe`, `--film`, a bad cue and a bad option refused, `| head -c 1` exits 1, both;
- 21 negative controls, 26 mutants, 0 dead controls (17 + 18 swept), 7 shaders through
  glslc, both bundles universal and read by `oxbow` (`SW Eidophone`/`ED01`/source,
  `SW Eido Over`/`ED02`/effect) with `oxbow selftest` passing.

Assumed, not measured: Q (one damping ratio for every mode); the ideal force source and the
free plate (no driver mass or suspension); rigid-body modes in steady state; the hop's form,
β = 1 and H; the powder current's form and P; the rim reflecting (a real plate's sand falls
off); the field held over a frame's substeps; the host bins' law, value and 44.1 kHz rate;
the colours; how Resolume sends its FFT to this plugin (never loaded).

## Would this hold on another rasteriser, at another raster?

Every GL check runs at 1280×720 and 320×180 on this Mac's GPU. All fourteen GL checks
passed on Apple's software renderer at 320×180 on 2026-11-16 (7 min 45 s in all; `--resize`
and `--freeze` about two minutes each), with the same grain numbers as the GPU's; verify
reruns `state threshold hop splat alpha redraw clock` there every time.

- `--modes`, `--circle`, `--plate`, `--audio`, `--names`, `--cues`: no GL, double.
- `--threshold`: exact by construction (a branch on the shader's own a²), checked against a
  double evaluation of the read-back lattice with a 1e-5 margin (float ulps of the lattice and
  the shader's arithmetic). A full-viewport pass, one texel per grain, `texelFetch`: no
  rasterisation is involved, so it holds anywhere.
- `--hop`: a z-test on 614,228 residuals, |z| < 4 on the mean and the variance; float
  arithmetic in the shader, the expectation in double from the read-back lattice.
- `--nodal`, `--freeze`, `--powder`, `--level`, `--select`: grain positions and modal state in
  plate coordinates, not pixels. Tolerances are in the dead band, lattice texels and plate
  units, so the raster changes nothing; the numbers are identical at both.
- `--splat`: against the CPU replica of GL's point rule, within the half-float bound and four
  subpixel bits on the disc edges. A rasteriser with fewer subpixel bits or a different
  point rule would need its own bound; the software renderer's 10 bits pass far inside it.
- `--alpha`: Mix 0 bit-exact (`texelFetch`); the alpha to 1e-5 from the density read back.
- `--device`: the cue's colour to 1e-5; its frame-time limit (no frame over 250 ms while a
  device takes 300 ms to open) measures the renderer as well as the threading: the GPU's
  slowest frame was 6.7 ms, the software renderer's 90.5 ms.
- `--resize`: 0 bytes within one context: a deterministic rasteriser is all it needs.
  `--state`: GL queries, exact. `--redraw`, `--clock`: exact and double.

## The negative controls and mutants

`edtest --negative`: 21 wrong models, each must fail its check (and each check also asserts
the thing measured happened: grains thrown and held, a mode rung, a note heard).

| check | the wrong model | failed |
| --- | --- | --- |
| modes | the ν w_xx w_yy term dropped from the stiffness | 3 checks |
| modes | the twist term 2(1 − ν) written (1 − ν) | 4 |
| circle | the moment condition's (1 − ν) negated | 4 |
| plate | damping ζω_d where ζω_k belongs | 2 |
| plate | every mode coupled to the driver by 1 | 2 |
| plate | no rigid-body response | 2 |
| audio | the pitch in Hz taken as rad/s | 3 |
| audio | the fallback lets bin 0 win | 1 |
| audio | the running peak never released | 1 |
| device | the menu values devices by position, not by name | 4 |
| select | the pitch in Hz taken as rad/s | 6 |
| threshold | grains hop at a > g/2 | 2 |
| hop | sand drifts up the gradient | 4 |
| hop | launch speed a/ω, not √(a² − g²)/ω | 4 |
| nodal | sand drifts up the gradient | 8 |
| powder | powder carried down the gradient | 4 |
| level | the force sized by the rigid response alone | 4 |
| freeze | grains hop at a > g/2 | 2 |
| alpha | the clip's alpha passed through | 2 |
| splat | sand splatted as powder, powder as sand | 4 |
| cues | every control ramped between keys | 1 |

No natural wrong model for `--redraw`, `--clock`, `--resize`, `--state` or `--names`;
a mutant covers the clock.

`tools/mutate.sh`: 26 one-character mutants of the shipped code, built once into a pristine
tree, each copied and rebuilt arm64-only, run at 320×180 under a 300 s alarm (a timeout counts
as caught). All 26 caught on 2026-11-16.

| file | the mutant | caught by |
| --- | --- | --- |
| Shaders.cpp | the gate inverted: the plate's quiet parts thrown | `--threshold` |
| Shaders.cpp | the launch speed √(a² + g²) | `--hop` |
| Shaders.cpp | the hop's diffusion H²v³/9g | `--hop` |
| Shaders.cpp | the drift 20% fast | `--hop` |
| Shaders.cpp | Box–Muller's radius from −3 ln u | `--hop` |
| Shaders.cpp | the powder current over ω² + ω | `--powder` |
| Shaders.cpp | the field's y derivative from the sum of its rows | `--powder` |
| Shaders.cpp | the lattice read a tenth of a texel off | `--threshold` |
| Shaders.cpp | every mode's coefficient on the first mode's shape | `--level` |
| Shaders.cpp | the disc's rim throwing grains off the plate | `--nodal` (survived until "0 off the plate" was required) |
| Shaders.cpp | the grain's disc 20% wide | `--splat` |
| Eidophone.cpp | the grains drawn 1.5× wide of where they are | `--splat` |
| Shaders.cpp | the sand's cover read from the powder's channel | `--alpha` |
| Shaders.cpp | the sand laid over the clip's alpha with + c | `--alpha` |
| Shaders.cpp | the clip passed through at every Mix | `--alpha` |
| Shaders.cpp | the cue over the whole plate, not its rim | `--device` |
| Plate.cpp | the resonator's denominator ω² + ω_d² | `--plate` |
| Plate.cpp | the free part's sine term with −ζω q₀ | `--plate` |
| Plate.cpp | the driver at the edge, not on the axis | `--plate` |
| Plate.cpp | Gain's peak looked for half off the plate | `--level` |
| Modes.cpp | the stiffness's ν term with its transpose subtracted | `--modes` |
| Modes.cpp | the circle's shear condition with −(1 − ν)n² | `--circle` |
| Audio.h | the running peak released at 9 s | `--audio` (survived until the 8 s was a literal) |
| Audio.cpp | a bin's centre a tenth of a bin high | `--audio` (survived until the centre was computed independently) |
| Audio.cpp | every device opened as the system default | `--device` |
| Eidophone.cpp | the substep clock keeping what it ran | `--clock` |

## The sweep's honest list

`python3 tools/sweep.py` sweeps all 17 of the source's controls that have pixels and 18 of
the Over's (the About block's four, the Audio buffer and Rescan have none; the buffer is
driven by `--bins`) and finds every one live. `--bare` drops the per-control contexts and finds
these dead, each for the reason its context exists:

- **Device** and **Bin Law**, both plugins: the base render feeds a tone through earshot's
  `feed()`, which is what earshot analyses whatever the menu says, and Bin Law only acts on
  the host FFT. Their context is `--bins 3`, no feed.
- **Clarity**, both: the base tone is a pure sine, voiced at both ends of 0.5–0.99. Its
  context is a two-partial chord, unvoiced at 0.99.
- **Air**, both: it moves powder, and the default is sand. Its context is Grain = Powder.

## Demo clips and the stills

Every one of Resolume's bundled clips on this Mac through the defaults by `--pipe` with WAV
music, 225 frames at 30 fps (7.5 s), 640×360, the last frame on contact sheets, judged by eye,
not by a metric: the 18 AV clips through the source with their own WAVs and through the Over
with their own clip, and the 15 Shop74 clips (no audio of their own) through the Over with
five AV WAVs in turn.

- **Bass**: the corner arcs, with a ring or a small square round the stem, in five of six;
  Bass 005 a mixture.
- **Beat**: the classic star in four (001, 002, 004, 006); 003 and 005 mixtures of two
  figures, still lines.
- **Synth**: 003 (a held 262 Hz) draws a full figure; 001, 002 and 005 part of one; 004 and
  006 barely move the sand. 004 sits at 1047 Hz, where the hop length H·v²/g, v ∝ 1/ω, is
  about a hundredth of what it is at 100 Hz for the same Gain, so in 7.5 s the sand has
  barely started; 006 is very quiet (mean level 0.002) and voiced a third of the time.
- So every clip moves sand: 10 of 18 draw a clean figure in 7.5 s, 6 a mixture or part of
  one, and 2 (Synth 004 and 006) barely start.
- **The Over**: all 15 Shop74 clips show a figure and the clip. The AV clips are themselves
  near black (Bass, Synth) or grey (Beat); the figure reads on them, faintly where the
  source's did.
- **Alpha**: `Metalive 01` (DXV with alpha) over the Bass 001 WAV: the alpha still is the
  clip's shape plus the sand where the clip is transparent.

The earlier passes (`d0`–`d6`, three WAVs) are what moved the audio law, Gain and Size.

## Render cost

`edtest --bench`, GPU time by `GL_TIME_ELAPSED` around the whole frame at 60 fps (8 substeps
a frame), every grain thrown, median after warm-up, verify's run on 2026-11-16 with other
work on the machine:

| | 720p | 1080p | 4K |
| --- | --- | --- | --- |
| source, defaults (65,536 grains) | 0.27 ms | 0.31 ms | 0.62 ms |
| source, 262,144 grains | 0.61 | 0.64 | |
| source, 16,384 grains | 0.19 | 0.24 | |
| source, defaults, Both | 0.27 | 0.35 | |
| Over, defaults | 0.28 | 0.42 | 0.93 |

1080p at the defaults is 1.9% of a 60 fps frame, and the most grains the Amount control
allows (262,144) 3.8%; the worst single frame of 120 at 1080p was 0.95 ms. A second verify run
minutes later read within 0.06 ms of these, as did a run earlier the same day, before the
splat drew whole-pixel squares.

The grain update (8 substeps over every grain) is most of it at 720p and 1080p and is the
same at any raster; the splat and the final pass grow with the raster and are about half or
more of the 4K figure (from the 720p-to-4K difference, not measured pass by pass). Amount is the quality control: 16,384 to 262,144
grains. One GPU only (an M4 Max); a small GPU or the software renderer is another matter.

## Open questions

- **The audio law is not the spec's.** Gain in g over the music's own recent peak is what
  made the demo clips draw; a VJ who wants loud to mean loud (absolute level) has no switch.
- **High notes draw slowly.** Physics: at fixed peak acceleration the hop length goes as
  1/ω². A synth lead an octave up takes four times as long. Hop scales it; whether the
  default should rise with pitch is open.
- **The lowest mode is under earshot's 40 Hz floor** at the default 40 cm. A smaller plate
  or a lower earshot floor (its buffers are sized for 40 Hz) would fix it.
- **The Over's sand reads sparse** in Fill at the default Amount; more grains cost little
  (see Render cost).
- **Iguchi's 65.3680** (SS-A, ν 0.3) has no counterpart anywhere in the converged spectrum,
  and Leissa's Table 4.67 "more precise upper bounds" column (ref 4.76) lies below converged
  values; both are excluded from the checks, unexplained.
- **Never in Resolume.** Nothing is known about Arena's FFT or clock reaching it, or how it
  reads at a show.
- **Windows by CI only.** MSVC builds it on GitHub's runner; miniaudio's WASAPI backend has
  never opened a device.
