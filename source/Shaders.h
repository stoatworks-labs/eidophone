#pragma once

#include <string>

/**
    The passes, once a frame.

        1. **field**   RG32F 256 x 256 over the plate's bounding square: the
                       analytic acceleration A( x ) = sum_k a_k W_k( x ), the
                       a_k from the CPU (Plate.cpp), the W_k sampled once per
                       Shape or Material into an R32F texture array. The model's
                       peak acceleration is |A| bilinearly interpolated from
                       this lattice, by `fieldAt` below and nowhere else.
        2. **pour**    RGBA32F 512 x 512 grain state ( x, y, a/g, kind ):
                       uniform over the plate, PCG-seeded. On the first frame,
                       on Pour, and when Grain or Shape changes.
        3. **update**  the same, ping-pong, once per 1/480 s substep (at most
                       32 a frame), over the rows Amount uses: a sand grain
                       where a <= g is copied bit for bit; elsewhere the hop
                       (SPEC-eidophone.md, "Grains").
        4. **splat**   points, one per grain, from gl_VertexID (no vertex
                       buffer), additive into RG16F at the output raster:
                       sand in r, powder in g.
        5. **final**   the host's frame: the plate (the material's colour, or
                       the Over's clip), the stem, the grains and their
                       shadows, the capture-state cue on the rim, the Over's
                       alpha and Mix.

    Assembled at InitGL as kVersion + kCommon + the pass by `Assemble()`, the
    one place the order is written; tools/glslc.sh's ASSEMBLED table mirrors it.
*/
namespace eido::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kSplatVertex;
extern const char* const kCommon;
extern const char* const kField;
extern const char* const kFieldFragment;
extern const char* const kPourFragment;
extern const char* const kUpdateFragment;
extern const char* const kSplatFragment;
extern const char* const kFinalFragment;

enum class Pass
{
	Field,
	Pour,
	Update,
	Splat,
	Final
};

/// The fragment shader of a pass (the splat's vertex shader is SplatVertex()).
std::string Assemble( Pass pass );
std::string SplatVertex();

/// Hook bits (the Hooks uniform). A shipped instance sets 0. Bits 0-2 are
/// Plate's (PlateHook); these start above them.
enum Hook : int
{
	kHookHalfGate     = 1 << 4,///< negative: grains hop at a > g/2
	kHookDriftUp      = 1 << 5,///< negative: sand drifts up the gradient of a
	kHookLaunchA      = 1 << 6,///< negative: launch speed a/w, not sqrt( a^2 - g^2 )/w
	kHookPowderDown   = 1 << 7,///< negative: powder carried down the gradient
	kHookAlphaThrough = 1 << 8,///< negative: the Over passes the clip's alpha through
	kHookNoClamp      = 1 << 9,///< diagnostic: no half-texel clamp on a step
	kHookSplatSwap    = 1 << 13///< negative: sand splatted as powder and powder as sand (1 << 10-12 are Audio's)
};

constexpr int kFieldSize = 256;///< the lattice, texels a side over [ -1, 1 ]
constexpr int kStateSize = 512;///< grain state, texels a side
constexpr int kMaxModes  = 66; ///< the rigid pair and 64 elastic modes
constexpr double kSubstep = 1.0 / 480.0;
constexpr int kMaxSubsteps = 32;

} // namespace eido::shaders
