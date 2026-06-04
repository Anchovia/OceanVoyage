#version 450

// FFT (Tessendorf) ocean surface. A camera-centred concentric mesh is displaced by the
// GPU-simulated FFT displacement map (height + horizontal choppiness). The map tiles every
// PATCH metres of world space, so sampling by world position keeps the waves world-locked.
// Normals are estimated from height central differences here; a proper FFT slope/normal map
// replaces this in a later step.

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;        // xyz = toward sun, w = dayFactor
    mat4 lightMVP;
    vec4 fogColor;        // rgb = sky color
    vec4 animationParams; // x = game time seconds
    vec4 cameraPos;       // xyz = camera world position
    mat4 reflectionViewProj;
} ubo;

layout(binding = 4) uniform sampler2D oceanDisplacement; // xyz = world displacement (xy choppy, z height)

layout(location = 0) in vec3 inPos; // local ocean mesh position centered on origin (z ignored)

layout(location = 0) out vec3  fragNormal;
layout(location = 1) out vec3  fragWorldPos;
layout(location = 2) out float fragViewDepth;
layout(location = 3) out vec4  fragReflectionClip;

const float SEA_LEVEL = 0.5;
const float GRID_SNAP = 0.5;
const float PATCH      = 256.0; // world size of one FFT tile (must match the compute shaders)
const float FFT_N      = 256.0;

void main() {
    // Snap the mesh origin to the camera at the near cell size so the sea follows the view
    // without sub-cell jitter; the FFT field itself is world-locked via the world-XY sample.
    vec2 cameraSnap = floor(ubo.cameraPos.xy / GRID_SNAP) * GRID_SNAP;
    vec2 worldXY    = inPos.xy + cameraSnap;
    vec2 uv         = worldXY / PATCH;

    vec3 disp = texture(oceanDisplacement, uv).xyz;
    vec3 pos  = vec3(worldXY + disp.xy, SEA_LEVEL + disp.z);

    // Normal from height central differences (one texel apart in each direction).
    float e     = 1.0 / FFT_N;
    float hL    = texture(oceanDisplacement, uv - vec2(e, 0.0)).z;
    float hR    = texture(oceanDisplacement, uv + vec2(e, 0.0)).z;
    float hD    = texture(oceanDisplacement, uv - vec2(0.0, e)).z;
    float hU    = texture(oceanDisplacement, uv + vec2(0.0, e)).z;
    float wStep = 2.0 * (PATCH / FFT_N); // world distance spanned by the central difference
    vec3  normal = normalize(vec3(hL - hR, hD - hU, wStep));

    vec4 worldPos = vec4(pos, 1.0);
    vec4 viewPos  = ubo.view * worldPos;
    gl_Position   = ubo.proj * viewPos;

    fragNormal    = normal;
    fragWorldPos  = pos;
    fragViewDepth = -viewPos.z;
    fragReflectionClip = ubo.reflectionViewProj * worldPos;
}
