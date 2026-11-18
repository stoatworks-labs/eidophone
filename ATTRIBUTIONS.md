# Attributions

Eidophone is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### earshot — Stoatworks earshot

<https://github.com/stoatworks-labs/earshot>  
Licence: MIT  
Copyright: Stoatworks Labs

external/earshot/ is earshot, the fleet's shared audio front end, as a snapshot: one commit taken with git archive and left unmodified, named in external/earshot/SNAPSHOT. It is built into the plugin by add_subdirectory and supplies the capture device, chosen by name with a Silent state, and the audio analysis the plugin reads every frame. It brings miniaudio with it, credited below.

### The plugins' shape, the harness and the tooling — Stoatworks haze, from ferrofluid

<https://github.com/stoatworks-labs/haze>  
Licence: MIT  
Copyright: Stoatworks Labs

Two plugins on one OBJECT core with an isEffect flag, the table of controls, the host-clock vote, the harness's host, PNG writer, cue sheets, --pipe/--film (SIGPIPE ignored, a closed stdout exits 1), --negative with its perturbation hooks, --state, --names, --cues, --resize, --offline and --bench, and tools/verify.sh, tools/mutate.sh, tools/glslc.sh, tools/sweep.py and scripts/release-lib.sh are haze's (ferrofluid's, zoetrope's, schlieren's and tinsel's before it), adapted.

### The device menu — Stoatworks spasis, by way of woofer

<https://github.com/stoatworks-labs/spasis>  
Licence: MIT  
Copyright: Stoatworks Labs

The capture-device menu's rules are spasis's: devices remembered by name, never by index; a Silent state for a device that is open and gives only zeros; the open off the render thread; Rescan filling spare slots in an option whose element count is fixed at declaration. The way source/Audio.cpp wraps earshot for it (never selectDevice("") during a feed, EIDOPHONE_NO_DEVICES for verification) follows woofer's, a sibling built on earshot the same day.

### The pass buffers and the log — Stoatworks millpond

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed) and source/Diag.{h,cpp} are millpond's, by way of haze. Adapted.

### The GL state guard — Stoatworks vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/GLState.h is radar's, which is vectrix's, which lifted it from resolume-scopes' Scopes.cpp: the host's GL state captured and put back.

### The release workflow and the DLL check — Stoatworks mirrorball, haze and pinhole

<https://github.com/stoatworks-labs/mirrorball>  
Licence: MIT  
Copyright: Stoatworks Labs

.github/workflows/ci.yml and release.yml are mirrorball's, which are haze's (xenon's, slitscan's, with isohypse's two-plugin parts): universal bundles and Windows DLLs, ATTRIBUTIONS.md in each zip, the Windows installer per user into Resolume's Extra Effects. tools/wincheck.ps1, which loads each DLL as a host loads it and reads back its name, id, type and parameters, is pinhole's (penumbra's, pepper's). Adapted.

### The symmetric eigensolver — JAMA (NIST and The MathWorks), after EISPACK

<https://math.nist.gov/javanumerics/jama/>  
Licence: Public domain  
Copyright: none claimed; JAMA is a joint product of The MathWorks and NIST

source/Eigen.cpp's Householder tridiagonalisation and implicit QL follow EISPACK's tred2 and tql2 in the arrangement of JAMA's EigenvalueDecomposition ("released to the public domain", JAMA's page, read 2026-11-16), rewritten in C++ for row-major std::vector<double>.

### The browser demo's build and kit — Stoatworks woofer, mirrorball and fountain, and polyhedral before them

<https://github.com/stoatworks-labs/woofer>  
Licence: MIT  
Copyright: Stoatworks Labs

demo/tools/build-wasm.sh, which compiles the plugins' own C++ and the earshot snapshot (without its capture backend) to WebAssembly unmodified, demo/wasm/glue.cpp (the page as the host), demo/wasm/gl_shim.cpp, demo/tools/check_shaders.py and demo/tools/crosscheck.py are woofer's, which are mirrorball's, pinhole's and fountain's, polyhedral's pattern; the page around them is the fleet's shared demo kit (demo/vendor/). Adapted.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

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

### miniaudio, inside earshot

<https://github.com/mackron/miniaudio>  
Licence: Public Domain (Unlicense) or MIT-0, at your choice  
Copyright: David Reid

Arrives inside the vendored earshot snapshot, at external/earshot/third_party/miniaudio/miniaudio.h, unmodified. Not vendored separately.

earshot's capture backend: it lists the capture devices and runs the audio callback, on CoreAudio and WASAPI. The plugin never calls miniaudio itself; it arrives only with earshot.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Leissa's Vibration of Plates — A. W. Leissa

Vibration of Plates, NASA SP-160 (1969): §2.1.3 (the free circular plate's frequency equation 2.14, Tables 2.5, 2.6 and 2.7, the asymptote 2.17) and the free square plate's Tables 4.65 (Iguchi's series solution) and 4.67 (Bazley, Fox and Stadter's lower bounds and the Rayleigh–Ritz upper bounds), with the bounds quoted on p. 104. edtest --modes and --circle hold the plate's modes to these values; two misprints in the circle's tables are excluded by name and said so.

### Gander and Wanner on Ritz's square plate — M. J. Gander and G. Wanner

<https://doi.org/10.1137/100804036>

"From Euler, Ritz, and Galerkin to Modern Computing", SIAM Review 54(4), 627–666 (2012), §5: Ritz's 1909 free square plate (ν = 0.225), the 6×6 matrix on p. 650. edtest --modes requires every converged eigenvalue to lie below the six it gives, as Ritz values must.

### Faraday on powders and figures — M. Faraday

<https://www.jstor.org/stable/107936>

"On a Peculiar Class of Acoustical Figures; and on Certain Forms Assumed by Groups of Particles upon Vibrating Elastic Surfaces", Phil. Trans. R. Soc. Lond. 121 (1831): why fine powder goes to the antinodes (currents of air over the plate, failing in an exhausted receiver). The model's powder current follows his account; its form is an assumption.

### The materials' datasheets

Aerospace Specification Metals' Aluminum 6061-T6 and AISI Type 304 Stainless Steel datasheets and Concast Metal Products' C26000 reference sheet: the modulus, density and Poisson's ratio of each plate material, each read on 2026-11-16 (SPEC-eidophone.md tabulates them with the exact figures used).

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Chladni figures and Megan Watts Hughes's voice figures

Ernst Chladni's sand figures on bowed plates, and the Welsh singer Megan (Margaret) Watts Hughes's eidophone: a membrane strewn with sand or lycopodium that she sang into, recorded in her book The Eidophone; Voice Figures (1904; known here only from secondary sources). The name is hers. Built from the physics; nothing is copied from anyone's software, visualiser or recordings.

## Standards and published specifications

What the implementation is measured against.

- **Kirchhoff plate theory** — The thin isotropic plate, free on every edge, flexural rigidity D = Eh³/12(1 − ν²): the model of source/Modes.cpp, as Leissa sets it out.
- **Standard acceleration of gravity** — g_n = 9.80665 m s⁻², exact (NIST CODATA): the threshold every grain is held to, and the unit of Gain.
- **PCG** — "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation" (2014): one 32-bit LCG step and its RXS-M-XS output permutation used as a stateless hash, every grain's random numbers.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
