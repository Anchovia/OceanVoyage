#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    mat4 lightMVP;          // legacy (cascade 0)
    vec4 fogColor;
    vec4 clipPlane;
    vec4 animationParams;
    vec4 cameraPos;
    mat4 reflectionViewProj;
    mat4 invViewProj;
    mat4 prevViewProj;
    vec4 temporalParams;
    mat4 lightMVPCascade[3]; // CSM per-cascade transforms
    vec4 cascadeSplits;      // xyz = cascade far view-depths
} ubo;

layout(binding = 1) uniform sampler2DArrayShadow shadowMap;

const float SHADOW_MAP_TEXEL = 1.0 / 2048.0;
const float SHADOW_NEAR_DEPTH = 0.5;

float cascadeFarDepth(int cascade) {
    if (cascade == 0) return ubo.cascadeSplits.x;
    if (cascade == 1) return ubo.cascadeSplits.y;
    return ubo.cascadeSplits.z;
}

float cascadeNearDepth(int cascade) {
    if (cascade == 0) return SHADOW_NEAR_DEPTH;
    if (cascade == 1) return ubo.cascadeSplits.x;
    return ubo.cascadeSplits.y;
}

float cascadeBiasScale(int cascade) {
    if (cascade == 0) return 1.0;
    if (cascade == 1) return 1.55;
    return 2.20;
}

float sampleShadowCascade(vec3 worldPos, vec3 normal, vec3 lightDir, int cascade) {
    vec4 lightSpace = ubo.lightMVPCascade[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpace.xyz / lightSpace.w;
    projCoords.xy   = projCoords.xy * 0.5 + 0.5;
    if (projCoords.z < 0.0 || projCoords.z > 1.0) return 1.0;

    float NdotL = max(dot(normal, lightDir), 0.0);
    float bias  = mix(0.00135, 0.00028, NdotL) * cascadeBiasScale(cascade);
    float shadow = 0.0;
    for (int x = -2; x <= 2; x++)
        for (int y = -2; y <= 2; y++)
            shadow += texture(shadowMap, vec4(projCoords.xy + vec2(x, y) * SHADOW_MAP_TEXEL, float(cascade), projCoords.z - bias));
    return shadow / 25.0;
}

// Cascaded shadow: blend across split boundaries to hide cascade resolution changes.
float sampleShadowCSM(vec3 worldPos, float viewDepth, vec3 normal, vec3 lightDir) {
    int cascade = 0;
    if (viewDepth > ubo.cascadeSplits.x) cascade = 1;
    if (viewDepth > ubo.cascadeSplits.y) cascade = 2;
    if (viewDepth > ubo.cascadeSplits.z) return 1.0;

    float shadow = sampleShadowCascade(worldPos, normal, lightDir, cascade);
    if (cascade < 2) {
        float splitFar = cascadeFarDepth(cascade);
        float splitNear = cascadeNearDepth(cascade);
        float blendBand = clamp((splitFar - splitNear) * 0.14, 5.0, 18.0);
        float blend = smoothstep(splitFar - blendBand, splitFar, viewDepth);
        if (blend > 0.0) {
            float nextShadow = sampleShadowCascade(worldPos, normal, lightDir, cascade + 1);
            shadow = mix(shadow, nextShadow, blend);
        }
    }
    return shadow;
}

layout(location = 0) flat in vec3 fragNormal;
layout(location = 1) flat in vec3 fragTopColor;
layout(location = 2) flat in vec3 fragSideColor;
layout(location = 3)      in vec3 fragWorldPos;
layout(location = 4)      in float fragViewDepth;
layout(location = 0) out vec4 outColor;

void main() {
    vec3  normal    = normalize(fragNormal);
    vec3  lightDir  = normalize(ubo.lightDir.xyz);
    float dayFactor = ubo.lightDir.w;

    // Cascaded shadow
    float shadow = 1.0;
    if (dayFactor > 0.01)
        shadow = sampleShadowCSM(fragWorldPos, fragViewDepth, normal, lightDir);
    float shadowFactor = max(shadow, 0.4);

    // top face (normal.z > 0.9) uses topColor, sides use sideColor
    float isTop  = step(0.9, fragNormal.z);
    vec3  color  = mix(fragSideColor, fragTopColor, isTop);

    const vec3 SKY_AMBIENT    = vec3(0.96, 0.93, 0.88); // soft warm sky (no cool tint)
    const vec3 GROUND_AMBIENT = vec3(1.04, 0.90, 0.70); // warmer ground bounce
    float hemi = clamp(normal.z * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambientTint = mix(GROUND_AMBIENT, SKY_AMBIENT, hemi);

    float diff = max(dot(normal, lightDir), 0.0);
    vec3 ambient = ambientTint * mix(0.10, 0.30, dayFactor);
    vec3 direct  = vec3(diff * 0.7 * dayFactor * shadowFactor);
    vec3 litColor = color * (ambient + direct);

    // Fog
    const float FOG_START = 27.0;
    const float FOG_END   = 57.0;
    float fogFactor = clamp((FOG_END - fragViewDepth) / (FOG_END - FOG_START), 0.0, 1.0);
    outColor = vec4(mix(ubo.fogColor.rgb, litColor, fogFactor), 1.0);
}
