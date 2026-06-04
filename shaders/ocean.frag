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
    vec4 clipPlane;
    vec4 animationParams;
    vec4 cameraPos;       // xyz = camera world position
    mat4 reflectionViewProj;
} ubo;

layout(binding = 1) uniform sampler2D planarReflection;
layout(binding = 2) uniform sampler2D oceanNormalA;
layout(binding = 3) uniform sampler2D oceanNormalB;
layout(binding = 4) uniform sampler2DArray oceanDisplacement; // per cascade: xyz = world displacement (z = height), w = whitecap seed
layout(binding = 6) uniform sampler2DArray oceanSlope;       // per cascade: rg = world height gradient (dH/dx, dH/dy)

layout(location = 0) in vec3  fragWorldPos;
layout(location = 1) in float fragViewDepth;
layout(location = 2) in vec4  fragReflectionClip;

layout(location = 0) out vec4 outColor;

const float PI    = 3.14159265;
const int   CASCADES = 3;
const float CASCADE_L[3] = float[](2048.0, 512.0, 128.0); // world size per cascade (must match the compute shaders)

float saturate(float v) {
    return clamp(v, 0.0, 1.0);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - saturate(cosTheta), 5.0);
}

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
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

float sunGlitterLobe(float NdotH, float NdotV, float NdotL, float roughness) {
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    return D * G / max(4.0 * NdotV * NdotL, 0.0001);
}

vec3 unpackNormal(vec3 c) {
    return normalize(c * 2.0 - 1.0);
}

struct OceanFrame {
    vec3 tangentX;
    vec3 tangentY;
    vec3 normal;
    vec2 sourceXY;
};

// Sum the displacement of every cascade at a world position (each cascade tiles at its own
// world size). This is the multi-scale surface the rest of the shader works from.
vec3 oceanDispSum(vec2 xy) {
    vec3 d = vec3(0.0);
    for (int c = 0; c < CASCADES; ++c)
        d += texture(oceanDisplacement, vec3(xy / CASCADE_L[c], float(c))).xyz;
    return d;
}

// Base wave frame, evaluated per fragment so detail is not limited by the ocean mesh.
// The rendered ocean is horizontally displaced (choppy), so first invert the displacement to
// find the source-map coordinate, then build the surface frame from the smooth slope map
// (sum of per-cascade bilinearly-filtered height gradients) instead of differencing the
// bilinear displacement — which would expose the texel grid.
OceanFrame oceanBaseFrame(vec2 worldXY) {
    vec2 sourceXY = worldXY;
    for (int i = 0; i < 2; ++i)
        sourceXY = worldXY - oceanDispSum(sourceXY).xy;

    vec2 grad = vec2(0.0);
    for (int c = 0; c < CASCADES; ++c)
        grad += texture(oceanSlope, vec3(sourceXY / CASCADE_L[c], float(c))).rg;

    OceanFrame frame;
    frame.tangentX = normalize(vec3(1.0, 0.0, grad.x));
    frame.tangentY = normalize(vec3(0.0, 1.0, grad.y));
    frame.normal   = normalize(vec3(-grad.x, -grad.y, 1.0));
    frame.sourceXY = sourceXY;
    return frame;
}

vec3 oceanDetailNormal(OceanFrame frame, float t) {
    vec2 p = frame.sourceXY;

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

    return normalize(frame.normal
                   + frame.tangentX * detailSlope.x
                   + frame.tangentY * detailSlope.y);
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

    return max(sky, vec3(0.0));
}

void main() {
    OceanFrame frame = oceanBaseFrame(fragWorldPos.xy);
    vec3  N = oceanDetailNormal(frame, ubo.animationParams.x);
    vec3  V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3  L = normalize(ubo.lightDir.xyz);
    float dayFactor = ubo.lightDir.w;

    const vec3 WATER_F0 = vec3(0.02);

    // Schlick Fresnel (F0 ~ 0.02 for water): grazing angles reflect the sky.
    float NdotV = max(dot(N, V), 0.0);
    vec3  Fv = fresnelSchlick(NdotV, WATER_F0);
    float F = saturate(Fv.r + smoothstep(0.55, 0.98, 1.0 - NdotV) * 0.12);

    const vec3 deepColor       = vec3(0.010, 0.065, 0.105);
    const vec3 midWaterColor   = vec3(0.030, 0.185, 0.230);
    const vec3 scatterColor    = vec3(0.105, 0.300, 0.335);

    // Reflected view direction across the wave-perturbed surface.
    vec3 R = reflect(-V, N);

    // Directional analytic sky fallback for water reflection. The planar target supplies
    // scene silhouettes; this supplies the high-frequency sun/horizon energy water needs.
    vec3  skyRefl = skyRadiance(R, L, dayFactor, ubo.fogColor.rgb);

    vec3  H     = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    vec3  Fs    = fresnelSchlick(VdotH, WATER_F0);
    vec3  sunColor = vec3(1.0, 0.92, 0.70);
    float sunGate = smoothstep(0.02, 0.70, dayFactor);
    float distanceRoughness = smoothstep(90.0, 620.0, fragViewDepth);
    float tightSun = sunGlitterLobe(NdotH, NdotV, NdotL, mix(0.050, 0.082, distanceRoughness));
    float broadSun = sunGlitterLobe(NdotH, NdotV, NdotL, mix(0.150, 0.220, distanceRoughness));
    float grazingPath = pow(saturate(dot(R, L)), 18.0) * clamp(1.0 - NdotV, 0.0, 1.0);
    vec3  sunSpec = (tightSun * 0.32 + broadSun * 0.15 + grazingPath * 0.45)
                  * Fs * sunColor * NdotL * sunGate;
    sunSpec = min(sunSpec, vec3(3.0));

    // Water body: a compact Beer-Lambert style approximation without a scene-depth buffer.
    // Looking through a longer path darkens the body; sun-facing wave slopes add cyan scatter.
    float viewPath = clamp(1.0 / max(NdotV, 0.16), 1.0, 6.0);
    float absorption = exp(-viewPath * 0.42);
    float sunScatter = pow(max(dot(N, L), 0.0), 1.8) * smoothstep(0.03, 0.85, dayFactor);
    float facingScatter = smoothstep(0.15, 0.95, NdotV);
    vec3 water = mix(deepColor, midWaterColor, absorption);
    water = mix(water, scatterColor, sunScatter * facingScatter * 0.28);

    vec3 reflProj = fragReflectionClip.xyz / fragReflectionClip.w;
    vec2 reflUV   = reflProj.xy * 0.5 + 0.5;
    reflUV += N.xy * 0.035 * clamp(1.0 - NdotV, 0.0, 1.0);
    vec3 sceneRefl = texture(planarReflection, clamp(reflUV, vec2(0.0), vec2(1.0))).rgb;
    float validRefl = step(0.0, reflUV.x) * step(reflUV.x, 1.0)
                    * step(0.0, reflUV.y) * step(reflUV.y, 1.0)
                    * step(0.0, reflProj.z) * step(reflProj.z, 1.0);
    vec3 reflection = mix(skyRefl, mix(skyRefl, sceneRefl, 0.68), validRefl);
    float horizonReflection = smoothstep(0.35, 0.98, 1.0 - NdotV);
    reflection += skyRefl * horizonReflection * 0.10 * smoothstep(0.02, 0.95, dayFactor);
    float reflectance = saturate(F * mix(0.82, 1.0, horizonReflection));
    vec3 color = mix(water, reflection, reflectance) + sunSpec;
    color += scatterColor * sunScatter * (1.0 - reflectance) * 0.055;

    // Daylight modulation (night = dim).
    color *= mix(0.22, 1.0, dayFactor);
    color = max(color, water * mix(0.55, 0.92, dayFactor));
    color = mix(color, vec3(luminance(color)), smoothstep(520.0, 1200.0, fragViewDepth) * 0.08);

    // Long-range atmospheric extinction for the sailing camera. Terrain/grass still use
    // short-range fog, but the ocean mesh runs to the horizon.
    const float FOG_DENSITY = 0.00078;
    float fogDepth = fragViewDepth * FOG_DENSITY;
    float fogFactor = exp2(-fogDepth * fogDepth * 1.442695);
    outColor = vec4(mix(ubo.fogColor.rgb, color, fogFactor), 1.0);
}
