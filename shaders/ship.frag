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
layout(binding = 5) uniform sampler2D shipAlbedo;
layout(binding = 6) uniform sampler2D shipNormal;
layout(binding = 7) uniform sampler2D shipSpecular;

layout(location = 0)      in vec3 fragNormal;
layout(location = 1)      in vec3 fragTangent;
layout(location = 3)      in float fragViewDepth;
layout(location = 4)      in vec2 fragUV;
layout(location = 5)      in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

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
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if (projCoords.z < 0.0 || projCoords.z > 1.0) return 1.0;

    float ndotl = max(dot(normal, lightDir), 0.0);
    float bias = mix(0.00165, 0.00032, ndotl) * cascadeBiasScale(cascade);
    float shadow = 0.0;
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y)
            shadow += texture(shadowMap, vec4(projCoords.xy + vec2(x, y) * SHADOW_MAP_TEXEL, float(cascade), projCoords.z - bias));
    return shadow / 25.0;
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
    vec3 T = normalize(fragTangent - N * dot(N, fragTangent));
    vec3 B = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    vec3 tangentNormal = texture(shipNormal, fragUV).xyz * 2.0 - 1.0;
    tangentNormal.xy *= 0.72;
    N = normalize(TBN * tangentNormal);

    vec3 L = normalize(ubo.lightDir.xyz);
    float dayFactor = ubo.lightDir.w;

    vec3 albedo = texture(shipAlbedo, fragUV).rgb;
    vec3 specTex = texture(shipSpecular, fragUV).rgb;
    float specMask = clamp(dot(specTex, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);

    float shadow = max(sampleShadow(fragWorldPos, fragViewDepth, N, L, dayFactor), 0.34);
    float ndotl = max(dot(N, L), 0.0);

    const vec3 SKY_AMBIENT    = vec3(0.96, 0.93, 0.88);
    const vec3 GROUND_AMBIENT = vec3(1.04, 0.90, 0.70);
    float hemi = clamp(N.z * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambientTint = mix(GROUND_AMBIENT, SKY_AMBIENT, hemi);

    vec3 ambient = ambientTint * mix(0.12, 0.32, dayFactor);
    vec3 direct = vec3(ndotl * 0.86 * dayFactor * shadow);

    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), mix(28.0, 86.0, specMask))
               * specMask * dayFactor * shadow * 0.34;

    vec3 color = albedo * (ambient + direct) + vec3(spec);

    const float FOG_START = 90.0;
    const float FOG_END   = 320.0;
    float fogFactor = clamp((FOG_END - fragViewDepth) / (FOG_END - FOG_START), 0.0, 1.0);
    outColor = vec4(mix(ubo.fogColor.rgb, color, fogFactor), 1.0);
}
