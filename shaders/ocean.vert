#version 450

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

layout(location = 0) in vec3 inPos; // local ocean mesh position centered on origin (z ignored)

layout(location = 0) out vec3  fragWorldPos;
layout(location = 1) out float fragViewDepth;
layout(location = 2) out vec4  fragReflectionClip;

const float SEA_LEVEL = 0.5;
const float GRID_SNAP = 0.5;
const int   CASCADES  = 3;
const float CASCADE_L[3] = float[](2048.0, 512.0, 128.0); // must match the compute shaders

void main() {
    // Snap the mesh origin to the camera at the near cell size so the sea follows the view
    // without sub-cell jitter; the FFT field itself is world-locked via the world-XY sample.
    vec2 cameraSnap = floor(ubo.cameraPos.xy / GRID_SNAP) * GRID_SNAP;
    vec2 worldXY    = inPos.xy + cameraSnap;

    vec3 disp = vec3(0.0);
    for (int c = 0; c < CASCADES; ++c)
        disp += texture(oceanDisplacement, vec3(worldXY / CASCADE_L[c], float(c))).xyz;
    vec3 pos  = vec3(worldXY + disp.xy, SEA_LEVEL + disp.z);

    vec4 worldPos = vec4(pos, 1.0);
    vec4 viewPos  = ubo.view * worldPos;
    gl_Position   = ubo.proj * viewPos;

    fragWorldPos  = pos;
    fragViewDepth = -viewPos.z;
    fragReflectionClip = ubo.reflectionViewProj * worldPos;
}
