# eidophone

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has been run in **Resolume only
> briefly**, in Arena on a Mac and on a Windows VM with no GPU; on the Mac, **Resolume's own FFT
> throws the sand with a demo drum loop playing** (see [In Resolume](#in-resolume)). Everything else below is measured by an offline harness, `edtest`, that drives the real plugin classes in a headless
> GL context on a synthetic clock, on this Mac's GPU at two rasters and partly on Apple's
> software renderer, and reads the plate's modes, the grain state and the picture straight
> back. The plate is checked against published physics, not screenshots. The free square's
> modes match Iguchi's series solution to **3.4e-5** and sit inside Leissa's published
> bounds; the circle's Bessel roots agree with an independent solver to **6.6e-10**. Each
> mode's response is the damped oscillator's closed form to **9.5e-16**. Over 757,980
> grain-substeps where the plate's acceleration is under g, **0** grains changed in any bit;
> over 4,484,878 above it, **0** stayed put. Driven in one mode, **100%** of the sand
> settles on that mode's nodal lines, square and circle. A sine at a mode's frequency, fed
> through earshot's pitch tracker, puts 65–91% of the plate's energy in that mode. On
> Resolume's FFT, bins built the way Arena was measured to build them drive the plate at the
> centre of the bin holding the note, at 48 and at 44.1 kHz (v0.1.1; v0.1.0 was 1.47 semitones
> flat in Arena). **25** deliberately wrong models and **29** one-character mutants of the
> shipped GLSL and C++ are all caught. How a grain hops, how powder is carried, and the plate's damping are
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

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/eidophone/releases/tag/v0.1.0)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`eidophone-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/eidophone/releases/download/v0.1.0/eidophone-0.1.0-macos-universal.dmg) | 1.3 MB |
| Universal (Apple Silicon + Intel) · .zip archive | [`eidophone-macos-universal.zip`](https://github.com/stoatworks-labs/eidophone/releases/latest/download/eidophone-macos-universal.zip) | 1.2 MB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`eidophone-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/eidophone/releases/download/v0.1.0/eidophone-0.1.0-windows-x86_64-setup.exe) | 391 KB |
| x64 · .zip archive | [`eidophone-windows-x86_64.zip`](https://github.com/stoatworks-labs/eidophone/releases/latest/download/eidophone-windows-x86_64.zip) | 540 KB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/eidophone/releases](https://github.com/stoatworks-labs/eidophone/releases).

macOS builds are signed and notarised and open normally. The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

## Video

A video of both plugins, rendered by `edtest` with Resolume's demo music as the sound that
drove the sand, is scheduled on YouTube for 04:45 London time on 20 November 2026. Its link
goes here once it is public.

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
64 bins and no pitch, so the note is a guess from the loudest bin, at its centre. In Arena the
bins are 375 Hz wide, 0–24 kHz (its audio engine runs at 48 kHz; the plugin takes the rate
Resolume reports), so every bass note lands in the first two and the plate rings at 562.5 Hz,
937.5 Hz, 1312.5 Hz and so on. Bin Law Linear, the default, is that layout; Log is for a host
that lays its bins out logarithmically, and in Arena puts the note far too low. Choose a capture device, a loopback
of the music or a microphone, and earshot gives a real pitch. A device that's missing shows
an amber rim on the plate; one that's open but silent (no microphone permission gives zeros,
not an error) a pulsing red one.

## Status

**v0.1.1, 19 November 2026 (v0.1.0 was 18 November), and early days. Run in Resolume only briefly, in
Arena on a Mac and on a Windows VM; on the Mac Resolume's FFT throws the sand with music playing,
and no live audio device has ever been opened.** `oxbow probe` reads the bundles as a host does (`SW Eidophone` /
`ED01` / source, `SW Eido Over` / `ED02` / effect) and `oxbow selftest` renders through each.
There's a [user guide](docs/USER-GUIDE.md) and a [browser demo](#browser-demo); no OpenFX port
and no presets.

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh universal
Release build (19 November 2026, v0.1.1, two and a half minutes). It runs every GL check at **two rasters**,
1280×720 and 320×180, and seven of them again on **Apple's software renderer** at 320×180.
What it establishes:

| check | result |
| --- | --- |
| `--modes` | the free square: Iguchi's nine doubly-symmetric eigenvalues (Leissa's Table 4.65) to **3.4e-5** relative; all 20 values of Leissa's Table 4.67 inside its lower and upper bounds; the lowest, 14.11608, inside the 14.1028–14.1165 bracket; below all six of Ritz's 1909 values; converged to 2.8e-9 and orthonormal to 5.8e-15 |
| `--circle` | the Bessel roots of Leissa's eq. 2.14 against an independent polar Rayleigh–Ritz solve to **6.6e-10**; Leissa's Table 2.6 to 0.31% and Table 2.5 to 0.74%; Table 2.7's nodal radii to 0.0005; Chladni's law approached monotonically, within 0.37% at ten circles |
| `--plate` | every mode on the damped oscillator's closed-form steady state to **9.5e-16**; the rigid shake F/M to 2.2e-16; the ring-down to 2.2e-19; the same at 24, 30, 60 and 240 fps to 2.3e-12; pushed at the centre, only the 19 doubly-symmetric modes move |
| `--audio` | a 440 Hz sine at −20 dBFS gives ω = 2π × 440.00 Hz and −20.00 dBFS; 20 dB down for 2 s, the drive 0.1284 of full against the 8 s release's closed form 0.1284; silence holds the note; the FFT fallback picks the loudest bin other than bin 0, at its centre under each Bin Law, on the host's axis: 48 kHz until the host says otherwise, then 44.1, 48 or 96 kHz as it says |
| `--hostfft` | **Resolume's FFT, through the plugin** (v0.1.1): a sine over a louder bass, made into 64 bins the way Arena 7.29.0 was measured to make them (a 2048-point Hann FFT, 16 fine bins a bin, held 2048 samples) and handed to the plugin as its FFT buffer with the host's rate by `SetSampleRate`. At 48 kHz and at 44.1 kHz, both plugins: five sines at bin centres drive the plate at their own pitch (**0** semitones off), and A4, A5, A6 and C8 at the centre of the bin holding them (at most 126.5 Hz off at 48 kHz, half a bin being 187.5; 134.9 at 44.1 kHz, of 172.3); with no rate sent, 48 kHz. v0.1.0's code fails it: every 48 kHz note **1.47 semitones flat** |
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
| `--negative` | **25** deliberately wrong models, **25** caught |
| mutation | **29** one-character mutants of the shipped GLSL and C++, **29** caught |
| `tools/sweep.py` | all **17** controls of the source and **18** of the Over that have pixels measurably change the picture |
| shaders | all 7 compile through `glslc`, not merely through Apple's driver |
| the bundles | both universal (`x86_64 arm64`), export `plugMain`, ad-hoc sign; `oxbow` reads their name, id and type and renders through both |

Render cost, GPU time by `GL_TIME_ELAPSED`, median after warm-up, every grain thrown, from
verify.sh's run on 18 November 2026 on an M4 Max shared with other work. At the defaults
(65,536 grains) the source takes **0.26 ms** at 720p, **0.31 ms** at 1080p and **0.61 ms** at
4K; the Over **0.28**, **0.41** and **0.87 ms**. At 1080p the most grains Amount allows
(262,144) take 0.64 ms, 16,384 take 0.23, and Both 0.35: inside a
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

### In Resolume

- **Arena 7.29.0 on an Apple Silicon Mac** (M4 Max), with both universal bundles from this
  source (signed ad hoc), mounted over Arena's REST API in a new composition: a blue still on
  layer 1, a test card on layer 2 (an opaque third, a half-transparent third and a clear
  third). Both register: SW Eido Over as an effect with **26** parameters shown (Opacity and
  the 25 declared), SW Eidophone as a source with its **24**, each with its declared defaults
  and the About line `Eidophone v0.1.0 - MIT - Stoatworks Labs, stoatworks-labs.com`. The
  Device menu starts with "Resolume FFT (no pitch)", then "System default" and the Mac's own
  devices: the plugin lists them for its menu when it loads, and opened none (no capture line
  in its log) and no microphone prompt appeared.
- **In silence nothing moves, exactly.** Two snapshots 2 s apart were identical, for each
  plugin. Mix 0 on the Over was identical to the bare card. The source covered the blue layer
  below it entirely (its alpha is 1). In Fill at the default Amount the Over's sand is sparse:
  99.9% of the card's pixels came through within 2 levels.
- **The controls that draw, draw.** View, Grain Size, Amount and Mix (Over), and View, Grain
  Size, Amount, Material and Drive At (source) each changed the picture; Pour, Shape and Grain
  strew the grains afresh, and did. The ones that act through the drive (Size, Thickness,
  Damping, Gain, Clarity, Bin Law, Hop, Air) changed nothing in silence, as they shouldn't.
- **Resolume's FFT throws the sand.** That first check (18 November) played `Beat 001.mov` on
  the layer below, and nothing moved, because Arena's demo `.mov` clips are picture only: their
  sound is in the matching `.wav` files. Re-tested on 19 November 2026 with the v0.1.0 release's
  own bundles, Device on "Resolume FFT (no pitch)", the Audio buffer on Composition and
  `Beat 001.wav` playing on the layer below, in snapshots about a fifth of a second apart: the
  grains hopped on the loop's hits, in 16 of 19 snapshot pairs on the Over and 9 of 19 on the
  source at the default Gain (12 g), and in 19 of 19 and 15 of 19 at 40 g, the most Gain gives,
  where the source's sand began to gather into a figure; Arena's quieter `Bass 001.wav` moved
  them too. With nothing playing, and once the loop was cleared, no pair changed, and the sand
  stayed where it had landed. **Arena's Audio setting decides what they hear:** on External
  (Arena's own audio input) nothing moved, 0 of 24 pairs for each plugin, and the Over on Local
  (over a picture-only card, so hearing nothing) lay still too.
- **v0.1.1 drives the plate at the frequency Resolume's bins hold** (19 November 2026, a
  pre-tag universal build signed ad hoc, the same set-up). Each plugin's log reads "host audio
  at 48000 Hz: Resolume's FFT bins are 375.0 Hz each", and names the bin and frequency once a
  note has held a second. Sines made for the check, on the layer below: **1312.5 Hz** (bin 3's
  centre) drove the plate at **1312.5 Hz**, from a 48 kHz WAV and from a 44.1 kHz one alike
  (Arena's engine is 48 kHz either way), and 880 Hz at 937.5 Hz, the centre of the bin holding
  it; v0.1.0 read them at 1205.9 and 861.3 Hz. When each tone started the sand moved in 19 of 19
  pairs on both plugins; the 44.1 kHz copy of the same tone, played next, moved it about a
  twentieth as far, the sand already lying on that mode's lines, and 880 Hz, another mode, threw
  it again. With the demo loops: `Beat 001.wav` 15 of 19 pairs (Over) and 9 of 19 (source) at
  12 g, 19 and 19 at 40 g; `Bass 001.wav` 13 and 12 at 12 g, 16 and 16 at 40 g; silence, a
  cleared layer, Local and External 0. Both register with every control as declared and the
  About line `Eidophone v0.1.1 - MIT - Stoatworks Labs, stoatworks-labs.com`.
- **Windows: Arena 7.27.1 on win-lab** (Mesa llvmpipe, no GPU, no sound device), with this
  release's own x64 DLLs, from its `.zip`, and before the tag with the release workflow's
  pre-tag build: the fleet's Arena gate passed **15** checks, failed none and skipped 4, the
  same verdicts both times. Both load and register (`ED01`, a source; `ED02`, an
  effect), show all 24 and 26 controls with the declared name, order, type, range and
  default, render, and 7 controls of each move the picture. The 10 and 12 that act only
  through the drive are skipped there (no sound device), and Rescan and Pour (events) are
  recorded inert. Arena's log was clean and it survived the run. The MSVC build has no
  warnings, and `tools/wincheck.ps1` loads each DLL as a host does and reads back its 24 and
  25 parameters.
- It has never been in a show, and nothing was timed inside Resolume.

### Not established

- **Music inside Resolume beyond Arena's demo stems.** Resolume's FFT throws the sand in Arena
  on the Mac (above), but no full mix has been played to it there, Avenue has never loaded it,
  and it has never run on a Windows machine with a GPU or a sound device.
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
- **The audio law is not the spec's, and it is relative.** The note is earshot's pitch, held
  between notes; the loudness is the RMS of earshot's band envelopes, voiced or not; Gain is
  the plate's peak acceleration at the loudest passage of the last few seconds (a running
  peak with an 8 s release). So a quiet track shakes the plate as hard as a loud one, and a VJ
  who wants loud to mean loud has no absolute-level switch.
- **High notes draw slowly**, as 1/f² at the same Gain: a note two octaves up takes sixteen
  times as long. Hop scales every hop; whether the default should rise with pitch is open.
- **The Over's sand reads sparse** in Fill at the default Amount (65,536 grains over the whole
  frame); more grains cost little (Render cost, above).
- **Two published values are left out, unexplained.** Iguchi's 65.3680 (SS-A, ν 0.3) has no
  counterpart anywhere in the converged spectrum, and Leissa's Table 4.67 "more precise upper
  bounds" column (ref 4.76) lies below converged values. Both are excluded from `--modes`.
- No sand falling off the edge (the rim holds it), no heaps with an angle of repose, no
  grain–grain collisions, no thick-plate corrections.

## Browser demo

[eidophone-demo.stoatworks-labs.com](https://eidophone-demo.stoatworks-labs.com/) runs both
plugins in your browser: their own C++ (the plugin class, the plate's modes and oscillators in
double, the audio law and earshot under it) compiled unmodified to WebAssembly, drawing with
their own GLSL in WebGL2. The page is the host, and it never opens or lists an audio device or
asks for a microphone. It feeds the plugin through earshot's `feed()` as `edtest --wav` does: a
track of held notes on the default plate's modes (D2, then 96.57 Hz for the four-pointed star,
188.22, 257.61 and 428.69 Hz), made by `edtest`'s own `sine()`, or an audio file of yours
decoded in the page and never uploaded; or nothing at all, with `edtest --bins 1`'s made-up FFT
in the Audio buffer for the plugin's own fallback, the one behind its default Device. Device,
Rescan and the live-device path are not in the browser. A frame of the page matched `edtest`'s
on the same settings, audio and frames in all but 0.3% of its colour values or fewer, in eight
cases across both plugins ([AGENTS.md](AGENTS.md#browser-demo)). It is a demo, not the plugin,
and the page lists what it leaves out.

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
