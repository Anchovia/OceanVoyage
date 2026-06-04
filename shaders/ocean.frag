#version 450

// Ocean surface shading: Fresnel sky reflection + depth-tinted water body + sun
// specular, then fog. Stage 1 reflection is the flat sky color (SSR comes later).

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;        // xyz = toward sun, w = dayFactor
    mat4 lightMVP;
    vec4 fogColor;        // rgb = sky color
    vec4 animationParams;
    vec4 cameraPos;       // xyz = camera world position
} ubo;

layout(location = 0) in vec3  fragNormal;
layout(location = 1) in vec3  fragWorldPos;
layout(location = 2) in float fragViewDepth;

layout(location = 0) out vec4 outColor;

void main() {
    vec3  N = normalize(fragNormal);
    vec3  V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3  L = normalize(ubo.lightDir.xyz);
    float dayFactor = ubo.lightDir.w;

    // Schlick Fresnel (F0 ~ 0.02 for water): grazing angles reflect the sky.
    float NdotV = max(dot(N, V), 0.0);
    float F = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

    const vec3 deepColor    = vec3(0.02, 0.10, 0.16);
    const vec3 shallowColor = vec3(0.10, 0.28, 0.34);
    vec3 skyColor = ubo.fogColor.rgb;

    // Water body: deeper/darker looking straight down, lighter at grazing angles.
    vec3 water = mix(shallowColor, deepColor, NdotV);
    vec3 color = mix(water, skyColor, F);

    // Sun specular highlight (Blinn-Phong).
    vec3  H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 200.0);
    color += vec3(1.0, 0.95, 0.85) * spec * dayFactor * 0.8;

    // Daylight modulation (night = dim).
    color *= mix(0.25, 1.0, dayFactor);

    // Foam: whitecaps on wave crests (high displaced height), broken up by a cheap
    // animated pattern so it reads patchy instead of banded. (SEA_LEVEL = 0.5)
    float crest   = smoothstep(0.60, 0.80, fragWorldPos.z);
    float breakup = 0.5 + 0.5 * sin(fragWorldPos.x * 2.3 + ubo.animationParams.x * 1.5)
                              * sin(fragWorldPos.y * 1.9 - ubo.animationParams.x * 1.1);
    float foam    = crest * mix(0.55, 1.0, breakup);
    vec3  foamColor = vec3(0.85, 0.90, 0.92) * mix(0.30, 1.0, dayFactor);
    color = mix(color, foamColor, foam * 0.8);

    // Distance fog to the sky color (matches the chunk shader).
    const float FOG_START = 27.0;
    const float FOG_END   = 57.0;
    float fogFactor = clamp((FOG_END - fragViewDepth) / (FOG_END - FOG_START), 0.0, 1.0);
    outColor = vec4(mix(ubo.fogColor.rgb, color, fogFactor), 1.0);
}
