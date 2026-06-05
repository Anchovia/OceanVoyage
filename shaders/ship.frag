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
const float PI = 3.14159265;
const float SEA_LEVEL = 0.5;

float saturate(float v) {
    return clamp(v, 0.0, 1.0);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - saturate(cosTheta), 5.0);
}

float distributionGGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 0.0001);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return NdotV / max(NdotV * (1.0 - k) + k, 0.0001);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 skyRadiance(vec3 dir, vec3 sunDir, float dayFactor, vec3 fogColor) {
    dir = normalize(dir);
    sunDir = normalize(sunDir);

    float viewUp = saturate(dir.z);
    float sunUp = saturate(sunDir.z);
    float lowSun = 1.0 - smoothstep(0.18, 0.78, sunUp);

    vec3 nightZenith = vec3(0.010, 0.015, 0.035);
    vec3 dayZenith = fogColor * vec3(0.48, 0.66, 1.12);
    vec3 zenith = mix(nightZenith, dayZenith, smoothstep(0.02, 0.85, dayFactor));

    vec3 horizon = fogColor * (1.12 + 0.24 * lowSun)
                 + vec3(0.45, 0.20, 0.055) * lowSun * smoothstep(0.02, 0.55, dayFactor);
    vec3 sky = mix(horizon, zenith, pow(viewUp, 0.55));

    float horizonAerial = exp(-viewUp * 7.5) * smoothstep(0.02, 0.75, dayFactor);
    sky += horizon * horizonAerial * 0.16;

    float sunCos = saturate(dot(dir, sunDir));
    float miePower = mix(14.0, 86.0, sunUp);
    float sunGlow = pow(sunCos, miePower) * smoothstep(0.02, 0.70, dayFactor);
    float sunDisc = smoothstep(0.99965, 0.99995, sunCos) * smoothstep(0.02, 0.35, dayFactor);
    vec3 sunTint = mix(vec3(1.0, 0.48, 0.18), vec3(1.0, 0.93, 0.72), sunUp);
    sky += sunTint * (sunGlow * mix(0.95, 0.46, sunUp) + sunDisc * 2.2);

    return max(sky, vec3(0.0));
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
    float sunUp = saturate(L.z);
    float dayGate = smoothstep(0.01, 0.09, dayFactor);

    vec3 albedo = texture(shipAlbedo, fragUV).rgb;
    vec3 specTex = texture(shipSpecular, fragUV).rgb;
    float specMask = clamp(dot(specTex, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);

    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3 H = normalize(L + V + vec3(0.0, 0.0, 0.0001));
    vec3 R = reflect(-V, N);
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float shadow = max(sampleShadow(fragWorldPos, fragViewDepth, N, L, dayFactor), 0.28);

    float roughness = mix(0.72, 0.32, specMask);
    vec3 F0 = mix(vec3(0.035), clamp(specTex, vec3(0.02), vec3(0.86)), specMask * 0.62);
    vec3 F = fresnelSchlick(VdotH, F0);
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);

    vec3 sunTint = mix(vec3(1.0, 0.52, 0.24), vec3(1.0, 0.92, 0.72), sunUp);
    vec3 sunRadiance = sunTint * mix(0.72, 1.35, smoothstep(0.05, 0.95, dayFactor)) * dayGate;
    vec3 kD = (1.0 - F) * (1.0 - specMask * 0.45);
    vec3 direct = (kD * albedo / PI + specular) * sunRadiance * NdotL * shadow;

    vec3 skyColor = skyRadiance(N, L, dayFactor, ubo.fogColor.rgb);
    vec3 seaBounce = mix(vec3(0.010, 0.050, 0.070),
                         vec3(0.040, 0.175, 0.205),
                         smoothstep(0.02, 0.85, dayFactor));
    float hemi = saturate(N.z * 0.5 + 0.5);
    vec3 ambient = mix(seaBounce, skyColor, hemi) * mix(0.52, 0.34, dayFactor);

    vec3 skyRefl = skyRadiance(R, L, dayFactor, ubo.fogColor.rgb);
    vec3 viewFresnel = fresnelSchlick(NdotV, F0);
    float wetLine = 1.0 - smoothstep(0.03, 0.62, abs(fragWorldPos.z - SEA_LEVEL));
    float wetSpec = wetLine * (0.35 + specMask * 0.45);
    vec3 ambientSpec = skyRefl * viewFresnel * (0.16 + specMask * 0.34 + wetSpec * 0.38);

    vec3 color = albedo * ambient + direct + ambientSpec;
    color = mix(color, color * vec3(0.58, 0.72, 0.78) + skyRefl * 0.10, wetLine * 0.28);

    float rim = pow(1.0 - NdotV, 3.0) * (0.16 + 0.24 * smoothstep(0.02, 0.55, dayFactor));
    color += skyRefl * rim * (0.40 + specMask * 0.35);

    const float FOG_START = 90.0;
    const float FOG_END   = 320.0;
    float fogFactor = clamp((FOG_END - fragViewDepth) / (FOG_END - FOG_START), 0.0, 1.0);
    outColor = vec4(mix(ubo.fogColor.rgb, color, fogFactor), 1.0);
}
