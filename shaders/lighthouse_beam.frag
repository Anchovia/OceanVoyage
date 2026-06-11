#version 450
#extension GL_GOOGLE_include_directive : require
#include "shared_constants.h"

layout(binding = 1) uniform sampler2D sceneDepth;

layout(binding = 2) uniform UniformBufferObject {
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
    vec4 localLightPosRadius[SHARED_LOCAL_LIGHT_COUNT];
    vec4 localLightColorIntensity[SHARED_LOCAL_LIGHT_COUNT];
    vec4 spotLightPosRadius[SHARED_SPOT_LIGHT_COUNT];
    vec4 spotLightDirAngle[SHARED_SPOT_LIGHT_COUNT];
    vec4 spotLightColorIntensity[SHARED_SPOT_LIGHT_COUNT];
} ubo;

layout(push_constant) uniform PostPushConstants {
    vec4 params; // xy = inverse framebuffer size, zw unused here
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

float saturate(float v) {
    return clamp(v, 0.0, 1.0);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 reconstructWorldPosition(vec2 p, float depth) {
    vec4 clip = vec4(p * 2.0 - 1.0, depth, 1.0);
    vec4 world = ubo.invViewProj * clip;
    float invW = 1.0 / ((abs(world.w) > 0.0001) ? world.w : 0.0001);
    return world.xyz * invW;
}

float phaseHG(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / max(pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5), 0.0001);
}

void main() {
    float dayFactor = ubo.lightDir.w;
    float night = 1.0 - smoothstep(0.10, 0.52, dayFactor);
    if (night <= 0.001) {
        outColor = vec4(0.0);
        return;
    }

    vec3 cameraPos = ubo.cameraPos.xyz;
    float depth = texture(sceneDepth, uv).r;
    vec3 rayEnd = reconstructWorldPosition(uv, depth >= 0.9999 ? 1.0 : depth);
    vec3 ray = rayEnd - cameraPos;
    float maxDistance = min(length(ray), 760.0);
    if (maxDistance <= 0.01) {
        outColor = vec4(0.0);
        return;
    }

    vec3 viewDir = ray / maxDistance;
    float opaqueLimit = depth >= 0.9999 ? maxDistance : max(maxDistance - 0.7, 0.0);
    float jitter = hash12(gl_FragCoord.xy + vec2(ubo.animationParams.x * 13.17, 7.0));

    vec3 scatter = vec3(0.0);
    const int STEP_COUNT = 28;
    for (int s = 0; s < STEP_COUNT; s++) {
        float t = (float(s) + 0.35 + jitter * 0.45) / float(STEP_COUNT);
        float distAlongRay = min(t * opaqueLimit, 760.0);
        vec3 samplePos = cameraPos + viewDir * distAlongRay;
        float viewFade = exp(-distAlongRay * 0.0026);

        for (int i = 0; i < SHARED_SPOT_LIGHT_COUNT; i++) {
            vec4 posRadius = ubo.spotLightPosRadius[i];
            float radius = posRadius.w;
            if (radius <= 0.0) continue;

            vec3 fromLight = samplePos - posRadius.xyz;
            float distToLight = length(fromLight);
            vec3 lightToSample = fromLight / max(distToLight, 0.0001);
            vec3 spotDir = normalize(ubo.spotLightDirAngle[i].xyz);
            float beamOuter = max(ubo.spotLightDirAngle[i].w, 0.9860);
            float cone = smoothstep(beamOuter, min(0.999, beamOuter + 0.010),
                                    dot(lightToSample, spotDir));
            cone = cone * cone * cone;
            float beamRadius = radius * 0.68;
            float range = saturate(1.0 - distToLight / max(beamRadius, 0.001));
            float heightFade = smoothstep(0.3, 7.0, samplePos.z) * (1.0 - smoothstep(42.0, 92.0, samplePos.z));
            float axialFade = smoothstep(14.0, 58.0, distToLight);
            float phase = min(phaseHG(dot(-viewDir, -lightToSample), 0.38), 2.4);
            vec3 light = ubo.spotLightColorIntensity[i].rgb * ubo.spotLightColorIntensity[i].w;
            scatter += light * cone * range * range * range * heightFade * axialFade * viewFade * phase * 0.00085;
        }
    }

    vec3 fogTint = mix(vec3(1.0, 0.62, 0.24), ubo.fogColor.rgb * vec3(1.45, 1.12, 0.82), 0.22);
    scatter *= fogTint * night;
    scatter = min(scatter, vec3(0.18));
    outColor = vec4(scatter, 1.0);
}
