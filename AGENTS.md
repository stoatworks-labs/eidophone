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
- **Generated since registration (2026-11-18):** `source/StoatworksAbout.h` by
  stoatworks-backend's `sync-about.py` (the hook and a User guide button, so 24 and 25
  parameters) and `ATTRIBUTIONS.md` by `sync-attributions.py`, whose earshot row is
  earshot's own `code` row from its docs/INTEGRATING.md, copied unchanged.
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
the colours; how Resolume sends its FFT to this plugin (in Arena on the Mac it never moved
the sand: README, In Resolume).

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
- **Never moved to music inside Resolume.** In Arena 7.29.0 on the Mac (2026-11-18) both
  plugins registered, drew and answered their drawing controls, but with Beat 001 playing on
  the layer below, 23 snapshots were identical on the defaults and at Gain 40 g, Bin Law Log:
  Resolume's FFT gave the fallback nothing that threw a grain (woofer saw the same there).
  Whether the FFT was empty on that Mac's audio set-up was not found out. Never in a show.
- **Windows on win-lab only.** MSVC builds it on GitHub's runner, and win-lab's Arena 7.27.1
  (llvmpipe, no sound device) passed the fleet's gate on the release workflow's DLLs (15
  passed, 0 failed, 4 skipped); miniaudio's WASAPI backend has never opened a device.

## Browser demo

https://eidophone-demo.stoatworks-labs.com/ (2026-11-18), from `demo/`, deployed by
`wrangler.toml` (a Worker route: the zone is at Cloudflare's 100 custom domains, so
`eidophone-demo` is a proxied AAAA `100::` record made through the API) and
`.github/workflows/deploy.yml` on every push to main. woofer's pattern, itself mirrorball's
and fountain's.

- **It runs the plugin, it doesn't port it.** `demo/eidophone-core.wasm` is `Eidophone.cpp`
  and everything it calls (Diag, Controls, Eigen, Modes, Plate, Audio, PassBuffer, Shaders),
  earshot's snapshot (every `src/*.cpp` but `Miniaudio.cpp`/`MiniaudioImpl.cpp`, with
  `NoCapture.cpp`: its own `EARSHOT_CAPTURE=OFF` build, so the module has no device backend at
  all) and the FFGL SDK's plugin classes, FFGLLog, FFGLShader, FFGLFBO, the Scoped* bindings,
  FFGLScreenQuad and FFGLUtilities, compiled unmodified by emscripten 6.0.5 at -O3
  (`demo/tools/build-wasm.sh`). The mode solvers, the oscillators, the audio law, the fallback
  and earshot's pitch tracker are that C++, in double. Left out: `SourcePlugin.cpp` and
  `EffectPlugin.cpp` (the registrations; the glue constructs the plugin itself). The script
  fails if a `source/*.cpp` or an earshot `src/*.cpp` it doesn't list appears.
- **What is the page's**: `demo/wasm/glue.cpp` plays the host (constructs the source or the
  Over, reads their declarations back through the SDK so the panel is built from them,
  forwards SetFloatParameter, SetTime, the Audio buffer's elements and ProcessOpenGL as
  edtest's Rig does, presets the clock to seconds after InitGL as Rig::Init does, and hands
  the page the harness's accessors: the frame's drive, substeps and grains, the nearest mode
  the drive point couples to, earshot's state, the plugin's clock and step counter);
  `demo/wasm/gl_shim.cpp` takes glShaderSource (refusing any text that isn't byte for byte one
  of the page's seven assemblies of `demo/shaders.js`, then the kit's `port()`),
  glEnable/glDisable/glIsEnabled (GL_PROGRAM_POINT_SIZE, which WebGL2 lacks; ES sizes a point
  by the splat's own gl_PointSize) and glTexImage2D (below); the host row; the read-outs'
  units, the tooltips, the twelve presets (in metres, g, Q, grains and factors, through the
  plugin's own `ParamFrom*` via `ed_param_from`), the lines under the picture and Diag's log
  from emscripten's memory file system (`EIDOPHONE_LOG_DIR=/eidophone/logs`). Off the panel:
  Device, Rescan, the Audio buffer and the About block (the panel's note says so). Pour is a
  button: 1 then 0, the plugin pours on the rising edge.
- **WebGL2's format trap, and the answer.** The FFGL SDK's `FFGLFBO::Initialise` allocates the
  field lattice (RG32F) and the splat's density (RG16F) as `glTexImage2D( …, GL_RGBA,
  GL_FLOAT, NULL )`. Desktop GL takes any client format with no data; WebGL2 takes only ES
  3.0's pairs, and for those two formats the pair is GL_RG, so it answers INVALID_OPERATION and
  the framebuffer is incomplete. The shim gives an allocation with no data of RG32F or RG16F
  the format GL_RG instead (nothing about what is allocated changes) and aborts on anything
  else it doesn't expect. The grain state (RGBA32F) and the mode shapes (an R32F 2D array by
  glTexImage3D/glTexSubImage3D from GL_RED) are ES pairs already. No format was changed: every
  target renders under EXT_color_buffer_float (the page stops without it), the splat blends
  into RG16F (no EXT_float_blend needed), and the plugin reads nothing back, so bubble's
  R32F-read-as-RGBA trap doesn't arise.
- **The audio, never a device.** The page never calls a device API (`check_shaders.py` fails if
  a served file names getUserMedia, enumerateDevices, mediaDevices or an AudioContext other
  than the offline one), the module cannot open one, and `ed_prepare` sets
  `EIDOPHONE_NO_DEVICES=1` so `AudioInput::Refresh` never asks. The host row's choices: the
  page's track (held sines at −12 dBFS made by edtest's `sine()`, copied character for
  character with `defaultToneHz()` and held to `tools/edtest/main.cpp` by `check_shaders.py`,
  with a 50 ms raised-cosine fade at each end and 1.5 s of silence after each: D2 73.42 Hz for
  7 s, then the centre modes 96.57 Hz (the star) for 7 s, 188.22 for 9, 257.61 for 10 and
  428.69, edtest `--tone 0`, for 12, looped every 52.5 s); edtest `--bins 1` (nothing fed;
  the plugin's fallback, the one behind its default Device, on the made-up buffer, written
  once: 516.8 Hz under Linear, 23.6 Hz under Log); and a file of the visitor's, decoded by
  `decodeAudioData` in an `OfflineAudioContext` at 48 kHz (no output device), mono to both
  channels, looped (edtest `--wav` plays once). Before the frame at page time t every sample to
  (t + 1/60) × 48 000 has been fed, as edtest's Feeder feeds frame f to (f + 1)/fps. Nothing is
  played aloud. Every change of audio or plugin, and Restart, is a new instance from time 0 (a
  feed lasts until a selectDevice, which nothing here calls). The page's default audio is the
  track, where the plugin's own default Device is "Resolume FFT (no pitch)".
- **The clock: preset, not voted.** In Resolume the plugin votes for four frames on whether
  SetTime is in seconds or milliseconds and runs on the wall clock until then. Here a new
  instance's first frame builds its mode table in WebAssembly, and under the vote that
  wall-clock time would become up to 32 substeps of grain walk the harness never takes; so
  the glue presets seconds, as edtest does, and the page's frames are edtest's frames. The
  page sends seconds from frame deltas (capped at 0.1 s by the kit); paused, a moved control
  redraws with no substep and nothing fed; Step is 1/60 s, eight substeps.
- **Photosensitivity: no note.** The release survey (2026-11-18, xenon's flashcheck rule:
  whole frame and 25 third-of-frame windows, 640×360 at 60 fps, 7.5 s a clip) ran SW Eidophone
  at 14 settings on 33 tracks (462 runs, Gain 40 g, the most and biggest grains, Pour four times
  a second among them) and SW Eido Over at 6 settings on all 33 of Resolume's demo clips: none
  over the line (at most 2 transitions in a second in any window for the source; the Over never
  took a clip over it). So the page has no warning and no gate, its defaults are the plugin's,
  and its notes say so in one line.
- **Checked by `tools/verify.sh`** (`demo/tools/check_shaders.py`, step "Browser demo's
  copies"): the ten GLSL pieces character for character and the order Assemble(),
  SplatVertex() and InitGL join them in for the seven stages and five programs; all 38
  uniforms set by the setter their type takes (array uniforms by `fv` with
  `shaders::kMaxModes`), nothing undeclared set; edtest's `sine()` and `defaultToneHz()`, its
  four `--bins` lines, its clock preset, feed lead and tone level in the glue; no served file
  naming a device API; the hashes of every input the .wasm was built from
  (`demo/wasm/inputs.sha256`, 68 files) and the SDK pin. A one-character shader change and a
  one-byte change to `source/Plate.cpp` each fail it. Any change under `source/` or
  `external/earshot/` needs `demo/tools/build-wasm.sh` rerun and its outputs committed (mode
  644).
- **Compared with edtest, once (2026-11-18), by `demo/tools/crosscheck.py`** (in the repo;
  nothing runs it by itself: it needs Chrome, cdpshot and a built edtest). Headless Chrome on
  the real GPU (ANGLE on Metal), the page paused, the settings applied, the audio chosen (a new
  instance and its frame at 0), then N presses of Step: N + 1 frames, the last at N/60 s, with
  (N + 1) × 800 samples fed. edtest renders N + 1 frames with the same host values: `--pipe
  --fps 60 --wav` on the page's own track samples, read out of its WebAssembly memory into a
  float WAV (the Over fed the page's own input frame, read back at Mix 0), or the plain render
  with `--bins 1` for the fallback (`--pipe` has no `--bins`; that PNG has no alpha). Apple
  M4 Max, 640×360 (691,200 colour values a case):

  | case | N | colour values off | max | 8×8 blocks, max |
  | --- | --- | --- | --- | --- |
  | source, defaults, the D2 note | 330 | 321 | 5 codes | 0.0 codes |
  | source, defaults, the star note 1.5 s in | 600 | 523 | 16 codes | 0.3 codes |
  | source, Circle, Drive At 0.4, Both | 300 | 1,949 | 2 codes | 0.1 codes |
  | source, 30 cm brass, Gain 30 g, Fill | 240 | 861 | 14 codes | 0.3 codes |
  | Over, defaults (scene) | 330 | 859 | 14 codes | 0.1 codes |
  | Over, Fit, Mix 50%, Powder (bars) | 240 | 572 | 1 code | 0.1 codes |
  | source, Resolume FFT (`--bins 1`) | 240 | 15 | 1 code | 0.0 codes |
  | Over, Resolume FFT, Bin Law Log (grid) | 180 | 347 | 2 codes | 0.0 codes |

  Alpha identical wherever compared; the drive (73.42 Hz, F₀ 3.9832 N on the defaults) and the
  substep count identical. Two negative controls on the first case fail the mark (under 1% of
  values off and no 8×8 block past 1 code), as they must: Gain one slider step (0.001) off puts
  18,636 values off (2.7%), up to 71 codes, and the other Shape 158,304. Run twice, the same
  numbers. So the wasm build lands on the Mac's grain for grain to within a few hundred
  values: the chaotic walk has not amplified the libm and shader-compiler differences in 10 s
  on this GPU. Another GPU is another matter.
- **Also checked on the page** (CDP, the real GPU): the defaults read back as the plugin's on
  both (the source in Fit, the Over in Fill with Mix 100%), and switching the plugin resets to
  that constructor's defaults; the corner arcs and the ring round the stem are drawn within
  2.4 s of loading, and the star when its note comes; Shape changes the picture paused, and Gain
  one slider step changes it (the negative control above); the fallback reads 516.8 Hz; a 44.1 kHz
  mono WAV of 188.22 Hz chosen through the file input decodes and gives earshot's pitch
  188.22 Hz on the mode at 188.22 Hz; all twelve presets render with no GL error; Defaults
  gives the constructor's values back and two Restarts give the same first frame; embed mode
  shows the picture alone; no horizontal scroll at 375 px; no device call (getUserMedia,
  enumerateDevices and AudioContext wrapped from outside, none called); no console errors.
- **Cost, Chrome on an M4 Max at 960×540**: 60 frames a second (the display's rate) at the
  default 65,536 grains and at 262,144, the most Amount allows, so the plugin's range is kept;
  a frame's CPU side about 1.6 ms. A change of Shape or Material holds the page for that
  frame: about half a second for the circle's first table, about a tenth for a change to a
  table already built (the tables are cached per shape and ν for the visit; the 66 mode shapes
  are uploaded again on every change).
- **Every gap, as the page discloses it** (banner, the panel's note, the "does not reproduce"
  list): it runs the plugin, not a port, and the units, tooltips and presets are the page's;
  no audio device or microphone, earshot built without capture, Device, Rescan and the Audio
  buffer off the panel, the live-device path (Missing, Failed, the rim cues, the remembered
  name) absent; the page's audio is not the plugin's default "Resolume FFT (no pitch)", where
  the note is a guess from the loudest of 64 bins, but a real pitch through `feed()`; the
  fallback runs only on `--bins 1`'s made-up buffer; the track or a file is decoded, resampled
  to 48 kHz and looped in the page, never played aloud; every audio or plugin change is a new
  instance from time 0, and the clock is preset to seconds rather than voted; a frame is a
  display refresh, below 15 a second the plugin drops substeps; the plugin's own limits (high
  notes draw slowly, as 1/f²; the lowest mode, near 37.5 Hz, under earshot's 40 Hz floor; one
  note at a time; the hop, the powder current and the damping are assumptions; no sand off
  the edge, no angle of repose, no collisions); GL is emscripten's WebGL2 with five shimmed
  entry points and the kit's GLSL port; WebAssembly has no fused multiply-add and musl's libm;
  the clip is the kit's and premultiplied, so the kit's alpha clip is left out, and the
  Over's partly transparent rim in Fit shows a little brighter on the page's canvas; the
  About block is off the panel; the flash survey found nothing over the line; nothing is measured, and the plugin has never been in
  Resolume or opened a device.
- **Traps hit**: the FFGLFBO format trap above (WebGL2's INVALID_OPERATION on RG32F/RG16F with
  GL_RGBA); GL_PROGRAM_POINT_SIZE is an INVALID_ENUM in WebGL2 and GLState saves and restores
  it every frame; `edtest --pipe` ignores `--bins` (the crosscheck uses the plain render for
  the fallback, whose PNG writes alpha 1) and the plain render with no `--wav` and no `--bins`
  plays `--tone 0`; the kit's canvas has no `preserveDrawingBuffer`, so a paused page reads
  back zeros unless the attribute is forced from outside (`Page.addScriptToEvaluateOnNewDocument`
  with a `getContext` override); the kit's inspector note says every declared parameter is on
  the panel, so the page appends which four are not; the read-out column fits about ten
  characters, so units went into the tooltips; the shared checkouts of the sibling demos lag
  origin, so the references were read from `git archive origin/main`.
