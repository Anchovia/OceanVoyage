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
const vec2 POISSON_DISK[16] = vec2[](
    vec2(-0.9420, -0.3991), vec2( 0.9456, -0.7689),
    vec2(-0.0942, -0.9294), vec2( 0.3449,  0.2939),
    vec2(-0.9159,  0.4577), vec2(-0.8154, -0.8791),
    vec2(-0.3828,  0.2768), vec2( 0.9748,  0.7565),
    vec2( 0.4432, -0.9751), vec2( 0.5374, -0.4737),
    vec2(-0.2649, -0.4189), vec2( 0.7919,  0.1909),
    vec2(-0.2419,  0.9971), vec2(-0.8141,  0.9144),
    vec2( 0.1998,  0.7864), vec2( 0.1438, -0.1410)
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
    float nightFactor = 1.0 - smoothstep(0.02, 0.38, dayFactor);
    vec3 moonDir = normalize(-sunDir);
    float moonUp = saturate(moonDir.z);
    float moonGate = nightFactor * smoothstep(0.02, 0.72, moonUp);

    vec3 nightZenith = vec3(0.010, 0.015, 0.035);
    vec3 dayZenith = fogColor * vec3(0.48, 0.66, 1.12);
    vec3 zenith = mix(nightZenith, dayZenith, smoothstep(0.02, 0.85, dayFactor));

    vec3 horizon = fogColor * (1.12 + 0.24 * lowSun)
                 + vec3(0.45, 0.20, 0.055) * lowSun * smoothstep(0.02, 0.55, dayFactor);
    vec3 sky = mix(horizon, zenith, pow(viewUp, 0.55));

    float horizonAerial = exp(-viewUp * 7.5) * smoothstep(0.02, 0.75, dayFactor);
    sky += horizon * horizonAerial * 0.16;

    float sunCos = saturate(dot(dir, sunDir));
    float moonCos = saturate(dot(dir, moonDir));
    vec3 sunTint = mix(vec3(1.0, 0.48, 0.18), vec3(1.0, 0.93, 0.72), sunUp);
    vec3 moonTint = vec3(0.55, 0.68, 1.0);

    float miePower = mix(14.0, 86.0, sunUp);
    float sunGlow = pow(sunCos, miePower) * smoothstep(0.02, 0.70, dayFactor);
    float sunAureole = pow(sunCos, mix(2.2, 7.5, sunUp)) * smoothstep(0.02, 0.85, dayFactor);
    float sunDisc = smoothstep(0.99855, 0.99992, sunCos) * smoothstep(0.02, 0.35, dayFactor);
    sky += sunTint * (sunAureole * 0.34 + sunGlow * mix(1.35, 0.64, sunUp) + sunDisc * 8.0);

    float moonGlow = pow(moonCos, 38.0) * moonGate;
    float moonDisc = smoothstep(0.99880, 0.99992, moonCos) * moonGate;
    sky += moonTint * (moonGlow * 0.42 + moonDisc * 4.2);

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

float cascadeFilterScale(int cascade) {
    if (cascade == 0) return 1.10;
    if (cascade == 1) return 1.45;
    return 1.85;
}

float sampleShadowCascade(vec3 worldPos, vec3 normal, vec3 lightDir, int cascade) {
    vec4 lightSpace = ubo.lightMVPCascade[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpace.xyz / lightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if (projCoords.z < 0.0 || projCoords.z > 1.0) return 1.0;

    float ndotl = max(dot(normal, lightDir), 0.0);
    float radius = cascadeFilterScale(cascade) * mix(1.28, 1.0, smoothstep(0.08, 0.65, ubo.lightDir.w));
    radius *= mix(1.16, 1.0, ndotl);
    float margin = radius * SHADOW_MAP_TEXEL;
    if (projCoords.x < margin || projCoords.x > 1.0 - margin ||
        projCoords.y < margin || projCoords.y > 1.0 - margin)
        return 1.0;

    float bias = (mix(0.00135, 0.00028, ndotl) + (1.0 - ndotl) * 0.00022)
               * cascadeBiasScale(cascade);
    float angle = hash12(floor(worldPos.xy * 1.7) + vec2(float(cascade) * 17.0, 3.0)) * 6.28318530;
    float shadow = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 offset = rotate2(POISSON_DISK[i], angle) * radius * SHADOW_MAP_TEXEL;
        shadow += texture(shadowMap, vec4(projCoords.xy + offset, float(cascade), projCoords.z - bias));
    }
    return shadow / 16.0;
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
    float nightFactor = 1.0 - smoothstep(0.02, 0.38, dayFactor);
    vec3 moonDir = normalize(-L);
    float moonUp = saturate(moonDir.z);
    float moonGate = nightFactor * smoothstep(0.02, 0.72, moonUp);

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

    vec3 Hm = normalize(moonDir + V + vec3(0.0, 0.0, 0.0001));
    float NdotMoon = saturate(dot(N, moonDir));
    float NdotMoonH = saturate(dot(N, Hm));
    float VdotMoonH = saturate(dot(V, Hm));
    vec3 Fm = fresnelSchlick(VdotMoonH, F0);
    float Dm = distributionGGX(NdotMoonH, roughness);
    float Gm = geometrySmith(NdotV, NdotMoon, roughness);
    vec3 specularMoon = (Dm * Gm * Fm) / max(4.0 * NdotV * NdotMoon, 0.0001);
    vec3 moonRadiance = vec3(0.38, 0.48, 0.70) * moonGate * 0.24;
    vec3 moonDirect = (kD * albedo / PI + specularMoon) * moonRadiance * NdotMoon;

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

    vec3 color = albedo * ambient + direct + moonDirect + ambientSpec;
    color = mix(color, color * vec3(0.58, 0.72, 0.78) + skyRefl * 0.10, wetLine * 0.28);

    float rim = pow(1.0 - NdotV, 3.0) * (0.16 + 0.24 * smoothstep(0.02, 0.55, dayFactor));
    color += skyRefl * rim * (0.40 + specMask * 0.35);

    const float FOG_START = 90.0;
    const float FOG_END   = 320.0;
    float fogFactor = clamp((FOG_END - fragViewDepth) / (FOG_END - FOG_START), 0.0, 1.0);
    outColor = vec4(mix(ubo.fogColor.rgb, color, fogFactor), 1.0);
}
