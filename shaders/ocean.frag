#version 450

// Ocean surface shading: per-fragment FFT normal + tiled normal-map detail, Fresnel planar
// reflection, GGX sun specular, depth-tinted water body, then fog.

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;        // xyz = toward sun, w = dayFactor
    mat4 lightMVP;
    vec4 fogColor;        // rgb = sky color
    vec4 animationParams;
    vec4 cameraPos;       // xyz = camera world position
    mat4 reflectionViewProj;
} ubo;

layout(binding = 1) uniform sampler2D planarReflection;
layout(binding = 2) uniform sampler2D oceanNormalA;
layout(binding = 3) uniform sampler2D oceanNormalB;
layout(binding = 4) uniform sampler2D oceanDisplacement; // xyz = world displacement (z = height)

layout(location = 0) in vec3  fragWorldPos;
layout(location = 1) in float fragViewDepth;
layout(location = 2) in vec4  fragReflectionClip;

layout(location = 0) out vec4 outColor;

const float PI    = 3.14159265;
const float PATCH  = 256.0; // world size of one FFT tile (must match the compute shaders)
const float FFT_N  = 512.0;

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

vec3 unpackNormal(vec3 c) {
    return normalize(c * 2.0 - 1.0);
}

// Base wave normal from the FFT displacement height, evaluated per fragment so detail is not
// limited by the ocean mesh tessellation. Central differences one texel apart in world space.
vec3 oceanBaseNormal(vec2 worldXY) {
    vec2  uv    = worldXY / PATCH;
    float e     = 1.0 / FFT_N;
    float hL    = texture(oceanDisplacement, uv - vec2(e, 0.0)).z;
    float hR    = texture(oceanDisplacement, uv + vec2(e, 0.0)).z;
    float hD    = texture(oceanDisplacement, uv - vec2(0.0, e)).z;
    float hU    = texture(oceanDisplacement, uv + vec2(0.0, e)).z;
    float wStep = 2.0 * (PATCH / FFT_N);
    return normalize(vec3(hL - hR, hD - hU, wStep));
}

vec3 oceanDetailNormal(vec3 baseN, vec3 worldPos, float t) {
    vec2 p = worldPos.xy;

    vec2 uvA0 = p * 0.035 + vec2( 0.018,  0.007) * t;
    vec2 uvA1 = vec2(p.y, -p.x) * 0.052 + vec2(-0.014,  0.019) * t;
    vec2 uvB0 = p * 0.110 + vec2(-0.041,  0.028) * t;
    vec2 uvB1 = vec2(-p.y, p.x) * 0.170 + vec2( 0.067, -0.036) * t;

    vec3 nA0 = unpackNormal(texture(oceanNormalA, uvA0).rgb);
    vec3 nA1 = unpackNormal(texture(oceanNormalA, uvA1).rgb);
    vec3 nB0 = unpackNormal(texture(oceanNormalB, uvB0).rgb);
    vec3 nB1 = unpackNormal(texture(oceanNormalB, uvB1).rgb);

    vec2 detailSlope = nA0.xy * 0.18
                     + nA1.xy * 0.12
                     + nB0.xy * 0.075
                     + nB1.xy * 0.045;

    return normalize(vec3(baseN.xy + detailSlope, max(baseN.z, 0.08)));
}

void main() {
    vec3  N = oceanDetailNormal(oceanBaseNormal(fragWorldPos.xy), fragWorldPos, ubo.animationParams.x);
    vec3  V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3  L = normalize(ubo.lightDir.xyz);
    float dayFactor = ubo.lightDir.w;

    const vec3  WATER_F0  = vec3(0.02);
    const float ROUGHNESS = 0.12;

    // Schlick Fresnel (F0 ~ 0.02 for water): grazing angles reflect the sky.
    float NdotV = max(dot(N, V), 0.0);
    vec3  Fv = fresnelSchlick(NdotV, WATER_F0);
    float F = Fv.r;

    const vec3 deepColor    = vec3(0.02, 0.10, 0.16);
    const vec3 shallowColor = vec3(0.10, 0.28, 0.34);

    // Reflected view direction across the wave-perturbed surface.
    vec3 R = reflect(-V, N);

    // Procedural sky reflection: a subtle gradient around the sky color.
    vec3  skyRefl = mix(ubo.fogColor.rgb * 1.04, ubo.fogColor.rgb * 0.90, clamp(R.z, 0.0, 1.0));

    vec3  H     = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float D     = distributionGGX(NdotH, ROUGHNESS);
    float G     = geometrySmith(NdotV, NdotL, ROUGHNESS);
    vec3  Fs    = fresnelSchlick(VdotH, WATER_F0);
    vec3  sunColor = vec3(1.0, 0.92, 0.70);
    vec3  sunSpec = (D * G * Fs / max(4.0 * NdotV * NdotL, 0.0001))
                  * sunColor * NdotL * smoothstep(0.02, 0.70, dayFactor) * 1.25;

    // Water body: deeper/darker looking straight down, lighter at grazing angles.
    vec3 water = mix(shallowColor, deepColor, NdotV);

    vec3 reflProj = fragReflectionClip.xyz / fragReflectionClip.w;
    vec2 reflUV   = reflProj.xy * 0.5 + 0.5;
    reflUV += N.xy * 0.035 * clamp(1.0 - NdotV, 0.0, 1.0);
    vec3 sceneRefl = texture(planarReflection, clamp(reflUV, vec2(0.0), vec2(1.0))).rgb;
    float validRefl = step(0.0, reflUV.x) * step(reflUV.x, 1.0)
                    * step(0.0, reflUV.y) * step(reflUV.y, 1.0)
                    * step(0.0, reflProj.z) * step(reflProj.z, 1.0);
    vec3 reflection = mix(skyRefl, mix(skyRefl, sceneRefl, 0.72), validRefl);
    vec3 color = mix(water, reflection, F) + sunSpec;

    // Daylight modulation (night = dim).
    color *= mix(0.25, 1.0, dayFactor);

    // Distance fog to the sky color (matches the chunk shader).
    const float FOG_START = 27.0;
    const float FOG_END   = 57.0;
    float fogFactor = clamp((FOG_END - fragViewDepth) / (FOG_END - FOG_START), 0.0, 1.0);
    outColor = vec4(mix(ubo.fogColor.rgb, color, fogFactor), 1.0);
}
