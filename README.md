# eidophone

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has **never been loaded into
> Resolume**, on macOS or Windows, and has never been built on Windows. Everything below is
> measured by an offline harness, `edtest`, that drives the real plugin classes in a headless
> GL context on a synthetic clock, on this Mac's GPU at two rasters and partly on Apple's
> software renderer, and reads the plate's modes, the grain state and the picture straight
> back. The plate is checked against published physics, not screenshots. The free square's
> modes match Iguchi's series solution to **3.4e-5** and sit inside Leissa's published
> bounds; the circle's Bessel roots agree with an independent solver to **6.6e-10**. Each
> mode's response is the damped oscillator's closed form to **9.5e-16**. Over 757,980
> grain-substeps where the plate's acceleration is under g, **0** grains changed in any bit;
> over 4,484,878 above it, **0** stayed put. Driven in one mode, **100%** of the sand
> settles on that mode's nodal lines, square and circle. A sine at a mode's frequency, fed
> through earshot's pitch tracker, puts 65–91% of the plate's energy in that mode. **21**
> deliberately wrong models and **26** one-character mutants of the shipped GLSL and C++
> are all caught. How a grain hops, how powder is carried, and the plate's damping are
> **assumptions**: see [Status](#status).

A Chladni plate with sand on it, sounded by the music, for [Resolume](https://resolume.com)
Arena and Avenue, as two FFGL plugins: **SW Eidophone**, a source that is a metal plate seen
from above with sand on it, and **SW Eido Over**, an effect in which your clip is printed on
the plate and the sand lies on top.

![A square grey aluminium plate on black with a dark stem at its centre, pale sand gathered
into the classic Chladni star: four cusped lines curving in from the corners, joined by a
rounded square round the middle](docs/eidophone.png)

<sub>One frame of the source, rendered by `edtest`, the offline harness, not captured from
Resolume, on 16 November 2026: the defaults, 7.5 s into Resolume's bundled `Beat 001` music
fed from its WAV (`--film 225 --fps 30 --wav "Beat 001.wav"`).</sub>

![Resolume's IntoTheGlow_02 clip, a gold and navy kaleidoscope, filling the frame, with pale
sand lying on it in rings and curves where the plate is still](docs/over.png)

<sub>The Over on its defaults on the same harness, Resolume's bundled `IntoTheGlow_02` as
the clip and the `Synth 003` WAV as the music, its 225th frame through `--pipe` (`--over
--pipe --fps 30 --size 1280x720 --wav "Synth 003.wav"`).</sub>

## The one idea

**A plate rings in the shapes it can ring in; sand sits still only where the plate is still
enough to hold it.**

A free metal plate, square or round, is pushed at its centre by a force that follows the
music's pitch and loudness. It answers as a sum of its own modes, each a damped resonator: a
mode whose frequency is near the note rings up, the rest barely move. A grain of sand on the
plate is pressed down by gravity and lifted by the plate; while the plate's downward
acceleration is less than g, the grain can't leave. Where the vibration beats gravity, the
grain is thrown, lands a little off, and is thrown again, drifting toward where the plate
moves less. Only near the nodal lines, where the plate barely moves, can it rest. Fine powder
does the opposite: the plate stirs the air over it into currents toward the places of
greatest motion (Faraday, 1831), and the powder rides them there.

The name is Megan Watts Hughes's instrument: a membrane strewn with sand that she sang into to
make "voice figures". Here the membrane is a plate and the singer is whatever is playing.

So the plugins are the plate, the sand and the ear:

- **the plate** is Kirchhoff's thin plate, free on every edge, in aluminium, brass or steel
  (each from a manufacturer's datasheet). The square's modes are computed by Rayleigh–Ritz
  (no closed form exists), the circle's exactly from Bessel functions;
- **the sand** is 65,536 grains (up to 262,144) that each hop by Newton's threshold and
  random-walk toward the nodal lines; **powder** goes the other way;
- **the ear** is earshot, the fleet's audio library: a capture device of your choice gives a
  real pitch. With no device, Resolume's own FFT drives the plate, and that has **no pitch**:
  the plugin guesses the note from the loudest of its 64 bins, and the menu says so.

## What falls out

None of this is drawn. It's the plate and the sand doing what they do.

- **The figure changes with the note.** Each mode has its own nodal lines. A bass note on the
  default plate draws arcs in the corners and a ring round the stem; a little higher, the
  four-pointed star.
- **Quiet freezes the figure, exactly.** Below g no grain moves at all, so a quiet passage
  keeps the last loud one's figure, bit for bit, as long as it lasts.
- **Louder draws thinner lines.** Sand can rest only where the plate's acceleration stays
  under g, a band round each nodal line that narrows as the drive grows.
- **A new note half-redraws the old figure.** Sand already in the new mode's still band never
  moves; the rest is thrown toward the new lines. Until it settles, both figures show.
- **Powder draws the negative.** With Grain on Powder or Both it heaps up exactly where the
  sand is thrown away from.
- **A loud wrong note smears everything.** Far from every mode the plate moves bodily with the
  force, everywhere at once, and loud enough no line holds.
- **High notes draw slowly.** At the same strength, a grain's hop is shorter as the pitch goes
  up (as 1/f²): a bass note draws its figure in a few seconds, a note two octaves higher takes
  far longer.
- **The circle draws rings.** Pushed at its centre, a round plate can only ring in its
  axisymmetric modes. Move Drive At off the centre and the diameters appear.

## Controls

| Group | |
| --- | --- |
| **Plate** | Shape (Square, Circle), Size (side or diameter, 10–60 cm), Thickness (0.3–3 mm), Material (Aluminium, Brass, Steel), Drive At (where it's pushed, 0–0.8 of the way from the centre to the edge), Damping (Q, 5–500). |
| **Audio** | Device ("Resolume FFT (no pitch)", "System default", then your capture devices, remembered by name), Rescan (list the devices again), Gain (how hard the loudest recent passage shakes the plate, 0.5–40 g), Clarity (how sure the pitch tracker must be to call it a note, 0.5–0.99), Bin Law (Linear, Log: how Resolume's FFT bins map to frequency; FFT only), Audio (Resolume's FFT input). |
| **Grains** | Grain (Sand, Powder, Both), Amount (1,024–262,144 grains), Hop (how far a thrown grain goes, 0.25–4), Air (how strongly powder is carried, 0–100), Pour (strew the grains again). |
| **View** | View (Fit: the whole plate; Fill: the plate covers the frame), Grain Size (0.5–4 px at 1080 lines), Mix (Over). |

**The defaults** are a 40 cm square of 1 mm aluminium pushed at its centre, Q 40, 65,536
grains of sand, Gain 12 g, Clarity 0.80, on Resolume's FFT. The source shows the whole plate
on black; the Over fills the frame with it, so your clip fills the frame too.

**Gain** is the plate's peak acceleration at the loudest passage of the last few seconds,
whatever the note: a quiet track shakes it as hard as a loud one, and at the default 12 g a
passage more than about 22 dB under the recent peak leaves the plate under g and the figure
frozen.

**Device.** On "Resolume FFT (no pitch)" the plate gets only what Resolume's audio FFT gives:
64 bins and no pitch, so the note is a guess from the loudest bin (under Linear the bins are
345 Hz wide, and every bass note lands in the first two). Choose a capture device, a loopback
of the music or a microphone, and earshot gives a real pitch. A device that's missing shows
an amber rim on the plate; one that's open but silent (no microphone permission gives zeros,
not an error) a pulsing red one.

## Status

**v0.1.0, 16 November 2026, unreleased, and early days.** It has **never been loaded into
Resolume** on either platform and never been built on Windows. `oxbow probe` reads the
bundles as a host does (`SW Eidophone` / `ED01` / source, `SW Eido Over` / `ED02` / effect)
and `oxbow selftest` renders through each. There's no user guide yet, no OpenFX port and no
browser demo.

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh universal
Release build (16 November 2026, two minutes). It runs every GL check at **two rasters**,
1280×720 and 320×180, and seven of them again on **Apple's software renderer** at 320×180.
What it establishes:

| check | result |
| --- | --- |
| `--modes` | the free square: Iguchi's nine doubly-symmetric eigenvalues (Leissa's Table 4.65) to **3.4e-5** relative; all 20 values of Leissa's Table 4.67 inside its lower and upper bounds; the lowest, 14.11608, inside the 14.1028–14.1165 bracket; below all six of Ritz's 1909 values; converged to 2.8e-9 and orthonormal to 5.8e-15 |
| `--circle` | the Bessel roots of Leissa's eq. 2.14 against an independent polar Rayleigh–Ritz solve to **6.6e-10**; Leissa's Table 2.6 to 0.31% and Table 2.5 to 0.74%; Table 2.7's nodal radii to 0.0005; Chladni's law approached monotonically, within 0.37% at ten circles |
| `--plate` | every mode on the damped oscillator's closed-form steady state to **9.5e-16**; the rigid shake F/M to 2.2e-16; the ring-down to 2.2e-19; the same at 24, 30, 60 and 240 fps to 2.3e-12; pushed at the centre, only the 19 doubly-symmetric modes move |
| `--audio` | a 440 Hz sine at −20 dBFS gives ω = 2π × 440.00 Hz and −20.00 dBFS; 20 dB down for 2 s, the drive 0.1284 of full against the 8 s release's closed form 0.1284; silence holds the note; the FFT fallback picks the loudest bin other than bin 0, at its centre under each Bin Law |
| `--select` | a sine at a mode's frequency, through earshot's pitch tracker, gives that mode **91.3%**, **89.8%** and **65.3%** of the plate's elastic energy (160, 201 and 371 Hz), the largest share each time |
| `--threshold` | over 20 substeps of a two-mode field: **0** of 757,980 grain-substeps under g changed in any bit; **0** of 4,484,878 over g stayed put |
| `--hop` | 614,228 thrown grain-substeps: after the drift, the residual's mean is +0.0001 σ along the gradient and −0.0009 σ across, its variance **1.0001** of the model's |
| `--nodal` | one mode at 16 g, square and circle, two modes each: **100.00%** of 16,384 grains settle on the analytic nodal lines, none ever off the plate |
| `--powder` | under g, every grain of powder moves by the current's law to within 0.59 of a float allowance; with Both, **92.9%** of the powder within 0.2 of the antinodes (a uniform scatter: 12.6%) and 100% of the sand on the nodal lines |
| `--level` | a steady sine at Gain 5 g settles at **0.943–0.990** × Gain at 45, 429, 444 and 1700 Hz, exactly as the plugin's coarse grid predicts |
| `--freeze` | 10 s at 0.700 g: **0** of 393,216 grain-readings changed; after 2 s of loud music, 8 s of silence move **0** |
| `--device` | the menu by name; a device that takes 0.3 s to open never stalls a frame (the next one 0.5 ms); Missing amber and Silent red on the rim; a device found again by name after Rescan; no real device opened |
| `--alpha` | the Over at Mix 0 returns the clip with **0** of 3,686,400 floats different; its alpha is the grains laid over the clip's, to 2.3e-6; the clip untouched where no grain lies; the source's alpha is 1 |
| `--splat` | the density the picture is drawn from against a CPU replica of GL's point rule: every grain once, at its place, to 0.12–0.27% of the mass (Apple's software renderer, 10 subpixel bits and float blending: 0.0005%) |
| `--clock`, `--resize`, `--state`, `--redraw` | at a 499,000,000 ms host clock the frames stay 1/60 s apart to 6e-11 s; a raster change mid-run is **0 bytes** different from a fresh instance; the host's GL state comes back; a repeated host time runs no substep |
| `--names`, `--cues` | 24 and 25 parameters (23 and 24 before the About block's User guide button), within FFGL's 16 characters, unique, the About block last; a cue sheet ramps and steps them |
| `--pipe` | both plugins: 2.5 frames in, exactly 2 out; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **21** deliberately wrong models, **21** caught |
| mutation | **26** one-character mutants of the shipped GLSL and C++, **26** caught |
| `tools/sweep.py` | all **17** controls of the source and **18** of the Over that have pixels measurably change the picture |
| shaders | all 7 compile through `glslc`, not merely through Apple's driver |
| the bundles | both universal (`x86_64 arm64`), export `plugMain`, ad-hoc sign; `oxbow` reads their name, id and type and renders through both |

Render cost, GPU time by `GL_TIME_ELAPSED`, median after warm-up, every grain thrown, from
verify.sh's run on 16 November 2026 on an M4 Max shared with other work. At the defaults (65,536 grains) the source takes **0.27 ms** at 720p, **0.31 ms** at
1080p and **0.62 ms** at 4K; the Over **0.28**, **0.42** and **0.93 ms**. At 1080p the most
grains Amount allows (262,144) take 0.64 ms, 16,384 take 0.24, and Both 0.35: inside a
60 fps frame with room to spare at any setting. The grain update (8 substeps a frame over
every grain) is most of it below 4K and is the same at any raster. macOS
figures only, on one GPU.

**Resolume's demo clips.** All 18 of the AV clips this Mac has went through the source with
their own WAVs, and through the Over with their own clips, for 7.5 s each at 640×360 by
`--pipe`; the 15 Shop74 clips (no sound of their own) went through the Over with five of the
AV WAVs in turn. Judged by eye on contact sheets: every clip moves sand; 10 of 18 draw a
clean figure in 7.5 s (five of the Bass clips' corner arcs, four of the Beat clips' star, and
Synth 003), six a mixture or part of one, and two (Synth 004 at 1047 Hz, Synth 006, very
quiet) barely start. All 15 Shop74 clips show the clip and a figure on it. The spec's first
audio law (a fixed force for a fixed level, on clearly pitched frames only) left the plate
still through most of these clips; the law now follows the music's level over its own recent
peak.

### Not established

- **Resolume.** Never loaded, on either platform, so nothing here is known about how Arena
  sends its FFT or clock to it, or how it looks or performs in a composition.
- **Windows.** Not yet run in a host. `.github/workflows/ci.yml` builds both DLLs with MSVC
  and loads each as a host does (`tools/wincheck.ps1`).
- **A real capture device.** The device menu, the opens and the rim cues are checked against a
  scripted backend; no microphone or loopback has fed it. Music came from WAVs through
  earshot's `feed()`.
- **The hop is a model.** The threshold (a > g) and the launch speed √(a² − g²)/ω are Newton;
  the hop's length scale, the drift toward the nodal lines and its strength are chosen. Real
  grains bounce off a tilted, moving plate in ways this does not resolve.
- **The powder current is a model.** Faraday saw powder carried to the antinodes by air
  currents and saw it fail in a vacuum; the current's form and strength here are assumptions.
- **The damping is an assumption.** One Q for every mode; nothing published was found for a
  sand-loaded plate on a driver.
- **The driver is ideal.** A force with no mass or suspension of its own, on an otherwise free
  plate; a real voice coil shifts the centre modes.
- **The default plate's lowest mode** (near 37.5 Hz) is under earshot's 40 Hz pitch floor,
  so only the FFT fallback or a note's overtones can ring it.
- **One note at a time.** Chords are not separated; the pitch tracker gives one.
- No sand falling off the edge (the rim holds it), no heaps with an angle of repose, no
  grain–grain collisions, no thick-plate corrections.

## Build

macOS (universal: Apple Silicon and Intel), from a checkout with its submodule
(`git submodule update --init --recursive`):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Eidophone.bundle` and `Eido Over.bundle` into
`~/Documents/Resolume Arena/Extra Effects`. Restart Arena and they're under Sources as
**SW Eidophone** and under Effects as **SW Eido Over**.

Windows needs Visual Studio 2022 and vcpkg (for GLEW, from `vcpkg.json`); see
`.github/workflows/ci.yml`.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, the earshot snapshot, a fresh universal
build with no warnings, both bundles' architecture, plist and ad-hoc signature, a host's
view of them through `oxbow`, every check at two rasters and seven on the software
renderer, the pipe, the negative controls, the mutants, the sweep for dead controls and the
render cost. No step opens an audio device. `CLAUDE.md` lists the checks one by one.

Render a frame of your own:

```bash
./build/edtest --out /tmp/eidophone.png --set "Shape=Circle" --set "Grain=Both"
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
