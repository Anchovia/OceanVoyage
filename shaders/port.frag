#version 450
#extension GL_GOOGLE_include_directive : require
#include "shared_constants.h"

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    mat4 lightMVP;
    vec4 fogColor;
    vec4 clipPlane;
    vec4 animationParams;
    vec4 cameraPos;
    mat4 reflectionViewProj;
    mat4 invViewProj;
    mat4 prevViewProj;
    vec4 temporalParams;
    mat4 lightMVPCascade[3];
    vec4 cascadeSplits;
} ubo;

layout(binding = 1) uniform sampler2DArrayShadow shadowMap;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragViewDepth;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

const float SHADOW_MAP_TEXEL = 1.0 / float(SHARED_SHADOW_MAP_SIZE);
const float SHADOW_NEAR_DEPTH = 0.5;
const vec2 POISSON_DISK[8] = vec2[](
    vec2(-0.9420, -0.3991), vec2( 0.9456, -0.7689),
    vec2(-0.0942, -0.9294), vec2( 0.3449,  0.2939),
    vec2(-0.9159,  0.4577), vec2(-0.3828,  0.2768),
    vec2( 0.9748,  0.7565), vec2( 0.5374, -0.4737)
);

float saturate(float v) {
    return clamp(v, 0.0, 1.0);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 rotate2(vec2 v, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

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

float sampleShadowCascade(vec3 worldPos, vec3 normal, vec3 lightDir, int cascade) {
    vec4 lightSpace = ubo.lightMVPCascade[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpace.xyz / lightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if (projCoords.z < 0.0 || projCoords.z > 1.0) return 1.0;

    float ndotl = max(dot(normal, lightDir), 0.0);
    float radius = mix(1.55, 1.05, smoothstep(0.08, 0.65, ubo.lightDir.w));
    float margin = radius * SHADOW_MAP_TEXEL;
    if (projCoords.x < margin || projCoords.x > 1.0 - margin ||
        projCoords.y < margin || projCoords.y > 1.0 - margin)
        return 1.0;

    float bias = mix(0.0012, 0.00022, ndotl) * (1.0 + 0.42 * float(cascade));
    float angle = hash12(floor(worldPos.xy * 1.3) + vec2(float(cascade) * 13.0, 5.0)) * 6.28318530;
    float shadow = 0.0;
    for (int i = 0; i < 8; ++i) {
        vec2 offset = rotate2(POISSON_DISK[i], angle) * radius * SHADOW_MAP_TEXEL;
        shadow += texture(shadowMap, vec4(projCoords.xy + offset, float(cascade), projCoords.z - bias));
    }
    return shadow / 8.0;
}

float sampleShadow(vec3 worldPos, float viewDepth, vec3 normal, vec3 lightDir, float dayFactor) {
    if (dayFactor <= 0.01) return 1.0;
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

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(ubo.lightDir.xyz);
    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3 albedo = fragColor.rgb;
    float emissive = max(fragColor.a, 0.0);
    float dayFactor = ubo.lightDir.w;
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));

    float shadow = max(sampleShadow(fragWorldPos, fragViewDepth, N, L, dayFactor), 0.32);
    vec3 warmSun = mix(vec3(1.0, 0.54, 0.28), vec3(1.0, 0.92, 0.72), saturate(L.z));
    vec3 direct = albedo * warmSun * NdotL * shadow * smoothstep(0.02, 0.55, dayFactor) * 1.18;

    vec3 skyAmbient = ubo.fogColor.rgb * mix(0.36, 0.52, dayFactor);
    vec3 seaBounce = vec3(0.035, 0.105, 0.120) * (0.55 + 0.45 * dayFactor);
    float hemi = saturate(N.z * 0.5 + 0.5);
    vec3 ambient = albedo * (mix(seaBounce, skyAmbient, hemi) + vec3(0.045, 0.040, 0.034));

    float rim = pow(1.0 - NdotV, 3.0) * 0.08;
    vec3 color = ambient + direct + ubo.fogColor.rgb * rim;

    float night = 1.0 - smoothstep(0.10, 0.45, dayFactor);
    float beaconGate = smoothstep(0.1, 0.4, emissive);
    color += vec3(1.0, 0.72, 0.34) * emissive * (0.85 + night * 3.2);
    color += vec3(1.0, 0.55, 0.18) * beaconGate * pow(1.0 - NdotV, 2.0) * (0.55 + night * 1.4);

    const float FOG_START = 140.0;
    const float FOG_END = 760.0;
    float fogFactor = clamp((FOG_END - fragViewDepth) / (FOG_END - FOG_START), 0.0, 1.0);
    fogFactor = mix(fogFactor, 1.0, beaconGate * 0.35);
    outColor = vec4(mix(ubo.fogColor.rgb, color, fogFactor), 1.0);
}
