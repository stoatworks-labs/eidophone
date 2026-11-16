# Attributions

Eidophone is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is a provisional hand copy: the master lists live in the `stoatworks-backend` repo and
are pushed out by `scripts/sync-attributions.py` once a project is registered there.
Eidophone is not registered yet, so this file was written by hand on 2026-11-16, adapted
from haze's, and will be replaced by the generated one.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The ear — Stoatworks earshot

<https://github.com/stoatworks-labs/earshot> (its intended home; the repository is local and unpushed as of 2026-11-16)  
Licence: MIT  
Copyright: Stoatworks Labs

Vendored as a snapshot at external/earshot, commit `8124191`, unmodified (external/earshot/SNAPSHOT). The pitch (McLeod's NSDF with its clarity), the band envelopes, the partials, the host-FFT fallback, the capture devices and their states, `deviceKey`, `feed()` for WAVs, and the scripted capture backend the harness uses so nothing opens a real device. earshot's own attributions, in external/earshot/ATTRIBUTIONS.md, carry over: miniaudio (below), spasis's capture and pattern's onset and tempo code, and the methods it cites.

### The plugins' shape, the harness and the tooling — Stoatworks haze

`~/dev/haze` (local, no remote as of 2026-11-16)  
Licence: MIT  
Copyright: Stoatworks Labs

Two plugins on one OBJECT core with an `isEffect` flag, the table of controls, the host-clock vote, the harness's host, PNG writer, cue sheets, `--pipe`/`--film` (SIGPIPE ignored, a closed stdout exits 1), `--negative` with its perturbation hooks, `--state`, `--names`, `--cues`, `--resize`, `--offline` and `--bench`, and tools/verify.sh, tools/mutate.sh, tools/glslc.sh, tools/sweep.py, the CI and release workflows and scripts/release-lib.sh are haze's (ferrofluid's, zoetrope's, schlieren's and tinsel's before it), adapted.

### The device menu — Stoatworks spasis, by way of woofer

<https://github.com/stoatworks-labs/spasis>  
Licence: MIT  
Copyright: Stoatworks Labs

The capture-device menu's rules are spasis's: devices remembered by name, never by index; a Silent state for a device that is open and gives only zeros; the open off the render thread; Rescan filling spare slots in an option whose element count is fixed at declaration. The way source/Audio.cpp wraps earshot for it (never `selectDevice("")` during a feed, `EIDOPHONE_NO_DEVICES` for verification) follows woofer's (`~/dev/woofer`, local), a sibling built on earshot the same day.

### The pass buffers and the log — Stoatworks millpond

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed) and source/Diag.{h,cpp} are millpond's, by way of haze. Adapted.

### The GL state guard — Stoatworks vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/GLState.h is radar's, which is vectrix's, which lifted it from `resolume-scopes`' Scopes.cpp: the host's GL state captured and put back.

### The symmetric eigensolver — JAMA (NIST and The MathWorks), after EISPACK

<https://math.nist.gov/javanumerics/jama/>  
Licence: public domain ("released to the public domain", JAMA's page, read 2026-11-16)  
Copyright: none claimed; JAMA is a joint product of The MathWorks and NIST

source/Eigen.cpp's Householder tridiagonalisation and implicit QL follow EISPACK's tred2 and tql2 in the arrangement of JAMA's EigenvalueDecomposition, rewritten in C++ for row-major `std::vector<double>`.

### The About block — Stoatworks xenon, by way of haze

<https://github.com/stoatworks-labs/xenon>  
Licence: MIT  
Copyright: Stoatworks Labs

source/StoatworksAbout.h, StoatworksAboutLinks.h and StoatworksAboutParams.h are provisional hand copies of haze's (xenon's before it), with `guide` empty (no user guide exists).

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### miniaudio — David Reid

<https://github.com/mackron/miniaudio>  
Licence: your choice of public domain (the Unlicense) or MIT No Attribution (MIT-0)  
Copyright: David Reid

Arrives inside the earshot snapshot at external/earshot/third_party/miniaudio, version 0.11.25, unmodified. Capture device enumeration and the audio callback, on CoreAudio and WASAPI.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Leissa's Vibration of Plates

A. W. Leissa, *Vibration of Plates*, NASA SP-160 (1969), read from the NASA PDF: §2.1.3 (the free circular plate's frequency equation 2.14, Tables 2.5, 2.6 and 2.7, the asymptote 2.17) and the free square plate's Tables 4.65 (Iguchi's series solution) and 4.67 (Bazley, Fox and Stadter's lower bounds and the Rayleigh–Ritz upper bounds), with the bounds quoted on p. 104. `edtest --modes` and `--circle` hold the plate's modes to these values; two misprints in the circle's tables are excluded by name and said so.

### Gander and Wanner on Ritz's square plate

M. J. Gander and G. Wanner, "From Euler, Ritz, and Galerkin to Modern Computing", SIAM Review 54(4), 627–666 (2012), doi:10.1137/100804036: §5, Ritz's 1909 free square plate (ν = 0.225), the 6×6 matrix on p. 650. `edtest --modes` requires every converged eigenvalue to lie below the six it gives, as Ritz values must.

### Faraday on powders and figures

M. Faraday, "On a Peculiar Class of Acoustical Figures; and on Certain Forms Assumed by Groups of Particles upon Vibrating Elastic Surfaces", Phil. Trans. R. Soc. Lond. 121 (1831): why fine powder goes to the antinodes (currents of air over the plate, failing in an exhausted receiver). The model's powder current follows his account; its form is an assumption.

### The materials' datasheets

Aerospace Specification Metals' Aluminum 6061-T6 and AISI Type 304 Stainless Steel datasheets and Concast Metal Products' C26000 reference sheet: the modulus, density and Poisson's ratio of each plate material, each read on 2026-11-16 (SPEC-eidophone.md tabulates them with the exact figures used).

## Standards and published specifications

What the implementation is measured against.

- **Kirchhoff plate theory** — the thin isotropic plate, free on every edge, flexural rigidity D = Eh³/12(1 − ν²): the model of source/Modes.cpp, as Leissa sets it out.
- **Standard acceleration of gravity** — g_n = 9.80665 m s⁻², exact (NIST CODATA): the threshold every grain is held to.
- **PCG** — M. E. O'Neill's permuted congruential generator, one 32-bit LCG step and its RXS-M-XS output permutation used as a stateless hash: every grain's random numbers.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Chladni figures and Megan Watts Hughes's voice figures

Ernst Chladni's sand figures on bowed plates, and the Welsh singer Megan (Margaret) Watts Hughes's eidophone: a membrane strewn with sand or lycopodium that she sang into, recorded in her book *The Eidophone; Voice Figures* (1904; known here only from secondary sources). The name is hers. Built from the physics; nothing is copied from anyone's software, visualiser or recordings.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
