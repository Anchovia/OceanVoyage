#version 450
#extension GL_GOOGLE_include_directive : require
#include "shared_constants.h"

// FFT (Tessendorf) ocean surface. A camera-centred concentric mesh is displaced by the
// GPU-simulated FFT displacement map (height + horizontal choppiness). The map is a stack
// of cascades, each tiling at its own world size; summing them by world position gives the
// multi-scale, world-locked surface. The normal is derived per-fragment (see ocean.frag).

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;        // xyz = toward sun, w = dayFactor
    mat4 lightMVP;
    vec4 fogColor;        // rgb = sky color
    vec4 clipPlane;
    vec4 animationParams; // x = game time seconds
    vec4 cameraPos;       // xyz = camera world position
    mat4 reflectionViewProj;
} ubo;

layout(binding = 4) uniform sampler2DArray oceanDisplacement; // per cascade: xyz = world displacement, w = whitecap seed
layout(binding = 5) uniform sampler2D oceanWakeMask;          // r = foam, g = turbulence, b = wake height, a = churn

layout(location = 0) in vec3 inPos; // local ocean mesh position centered on origin (z ignored)

layout(location = 0) out vec3  fragWorldPos;
layout(location = 1) out float fragViewDepth;
layout(location = 2) out vec4  fragReflectionClip;

const float SEA_LEVEL = SHARED_SEA_LEVEL;
const float GRID_SNAP = 0.5;
const int   CASCADES  = SHARED_OCEAN_CASCADES;
const float CASCADE_L[CASCADES] = float[](SHARED_OCEAN_CASCADE_L);
const float WAKE_WORLD_SIZE = SHARED_OCEAN_WAKE_WORLD_SIZE;
const float WAKE_TEXEL_UV = 1.0 / float(SHARED_OCEAN_WAKE_N);
const float WAKE_TEXEL_WORLD = WAKE_WORLD_SIZE * WAKE_TEXEL_UV;

void main() {
    // Snap the mesh origin to the camera at the near cell size so the sea follows the view
    // without sub-cell jitter; the FFT field itself is world-locked via the world-XY sample.
    vec2 cameraSnap = floor(ubo.cameraPos.xy / GRID_SNAP) * GRID_SNAP;
    vec2 worldXY    = inPos.xy + cameraSnap;

    vec3 disp = vec3(0.0);
    for (int c = 0; c < CASCADES; ++c)
        disp += texture(oceanDisplacement, vec3(worldXY / CASCADE_L[c], float(c))).xyz;

    vec2 wakeUV = worldXY / WAKE_WORLD_SIZE;
    vec4 wake = texture(oceanWakeMask, wakeUV);
    float wakeHeight = wake.b * 0.52;
    float wakeL = texture(oceanWakeMask, wakeUV - vec2(WAKE_TEXEL_UV, 0.0)).b;
    float wakeR = texture(oceanWakeMask, wakeUV + vec2(WAKE_TEXEL_UV, 0.0)).b;
    float wakeD = texture(oceanWakeMask, wakeUV - vec2(0.0, WAKE_TEXEL_UV)).b;
    float wakeU = texture(oceanWakeMask, wakeUV + vec2(0.0, WAKE_TEXEL_UV)).b;
    vec2 wakeSlope = vec2(wakeR - wakeL, wakeU - wakeD) / max(2.0 * WAKE_TEXEL_WORLD, 0.001);
    vec2 wakeChop = -wakeSlope * clamp(0.95 + wake.g * 1.10 + wake.a * 0.64, 0.0, 2.45);

    vec3 pos  = vec3(worldXY + disp.xy + wakeChop, SEA_LEVEL + disp.z + wakeHeight);

    vec4 worldPos = vec4(pos, 1.0);
    vec4 viewPos  = ubo.view * worldPos;
    gl_Position   = ubo.proj * viewPos;

    fragWorldPos  = pos;
    fragViewDepth = -viewPos.z;
    fragReflectionClip = ubo.reflectionViewProj * worldPos;
}
