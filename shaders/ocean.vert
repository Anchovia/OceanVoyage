#version 450

// Gerstner-wave ocean surface. A flat grid (binding 0) is displaced into rolling
// waves; the grid follows the camera in world space so the sea always fills the view.
// Analytic normals are derived from the wave partial derivatives.

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

layout(location = 0) in vec3 inPos; // flat grid position centered on origin (z ignored)

layout(location = 0) out vec3  fragNormal;
layout(location = 1) out vec3  fragWorldPos;
layout(location = 2) out float fragViewDepth;
layout(location = 3) out vec4  fragReflectionClip;

const float SEA_LEVEL = 0.5;
const int   NWAVES    = 4;
const float STEEPNESS = 0.6;
const float GRAVITY   = 9.8;

// Per-wave: xy = direction, z = wavelength, w = amplitude.
const vec4 waves[NWAVES] = vec4[NWAVES](
    vec4( 1.0,  0.0,  9.0,  0.115),
    vec4( 0.6,  0.8,  5.0,  0.060),
    vec4(-0.8,  0.4,  13.0, 0.085),
    vec4( 0.2, -1.0,  3.3,  0.030)
);

void main() {
    // Snap the grid origin to the camera (integer world units) so the sea follows the
    // view without sub-cell jitter relative to the world-locked wave phase.
    vec2  worldXY = inPos.xy + vec2(floor(ubo.cameraPos.x), floor(ubo.cameraPos.y));
    float t       = ubo.animationParams.x;

    vec3 pos      = vec3(worldXY, SEA_LEVEL);
    vec3 tangent  = vec3(1.0, 0.0, 0.0);
    vec3 binormal = vec3(0.0, 1.0, 0.0);

    for (int i = 0; i < NWAVES; i++) {
        vec2  d = normalize(waves[i].xy);
        float L = waves[i].z;
        float A = waves[i].w;
        float k = 6.2831853 / L;
        float w = sqrt(GRAVITY * k);
        float Q = STEEPNESS / (k * A * float(NWAVES)); // limit steepness to avoid loops
        float phase = k * dot(d, worldXY) - w * t;
        float c = cos(phase);
        float s = sin(phase);

        pos.x += Q * A * d.x * c;
        pos.y += Q * A * d.y * c;
        pos.z += A * s;

        float kA = k * A;
        tangent  += vec3(-Q * d.x * d.x * kA * s,
                         -Q * d.x * d.y * kA * s,
                          d.x * kA * c);
        binormal += vec3(-Q * d.x * d.y * kA * s,
                         -Q * d.y * d.y * kA * s,
                          d.y * kA * c);
    }

    vec3 normal = normalize(cross(tangent, binormal)); // points +Z (up)

    vec4 worldPos = vec4(pos, 1.0);
    vec4 viewPos  = ubo.view * worldPos;
    gl_Position   = ubo.proj * viewPos;

    fragNormal    = normal;
    fragWorldPos  = pos;
    fragViewDepth = -viewPos.z;
    fragReflectionClip = ubo.reflectionViewProj * worldPos;
}
