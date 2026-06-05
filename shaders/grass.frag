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
layout(binding = 2) uniform sampler2D grassTex;
layout(binding = 4) uniform sampler2D grassOpacityTex;

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

layout(location = 0)      in vec3 fragNormal;
layout(location = 1)      in vec2 fragUV;
layout(location = 2)      in vec3 fragWorldPos;
layout(location = 3)      in float fragViewDepth;
layout(location = 4)      in vec3 fragTint;
layout(location = 5)      in float fragFade;
layout(location = 6)      in float fragRootShade;
layout(location = 7)      in vec3 fragViewPos;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 texel = texture(grassTex, fragUV).rgb;
    float alpha = texture(grassOpacityTex, fragUV).r;
    float alphaCutoff = mix(0.34, 0.12, fragFade);
    if (alpha < alphaCutoff) discard;

    vec3  normal    = normalize(gl_FrontFacing ? fragNormal : -fragNormal);
    vec3  lightDir  = normalize(ubo.lightDir.xyz);
    float dayFactor = ubo.lightDir.w;

    // Cascaded shadow
    float shadow = 1.0;
    if (dayFactor > 0.01)
        shadow = sampleShadowCSM(fragWorldPos, fragViewDepth, normal, lightDir);
    float shadowFactor = max(shadow, 0.4);

    const vec3 SKY_AMBIENT    = vec3(0.96, 0.93, 0.88);
    const vec3 GROUND_AMBIENT = vec3(1.04, 0.90, 0.70);
    float hemi = clamp(normal.z * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambientTint = mix(GROUND_AMBIENT, SKY_AMBIENT, hemi);

    float diff = max(dot(normal, lightDir), 0.0);
    vec3 ambient = ambientTint * mix(0.14, 0.34, dayFactor);
    vec3 direct  = vec3(diff * 0.7 * dayFactor * shadowFactor);
    vec3 litColor = texel * fragTint * (ambient + direct);
    litColor = mix(litColor, litColor * vec3(0.50, 0.58, 0.38), fragRootShade * 0.42);

    // Translucency / back-light: thin blades glow warm when viewed toward the sun.
    // Strongest looking into the light, biased to the blade tips, gated by sun + shadow.
    vec3  Lview     = normalize((ubo.view * vec4(ubo.lightDir.xyz, 0.0)).xyz);
    vec3  viewDir   = normalize(fragViewPos);          // eye -> fragment (view space)
    float backlight = pow(max(dot(viewDir, Lview), 0.0), 3.0); // strongest looking toward the sun
    float tip       = 1.0 - fragRootShade;
    float dayGate   = smoothstep(0.0, 0.15, dayFactor);        // off only near night, keeps golden hour
    vec3  transTint = vec3(0.95, 1.05, 0.45);
    litColor += transTint * fragTint * backlight * tip * dayGate * shadowFactor * 0.5;

    // Fog
    const float FOG_START = 27.0;
    const float FOG_END   = 57.0;
    float fogFactor = clamp((FOG_END - fragViewDepth) / (FOG_END - FOG_START), 0.0, 1.0);
    outColor = vec4(mix(ubo.fogColor.rgb, litColor, fogFactor), 1.0);
}
