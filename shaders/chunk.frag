#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    mat4 lightMVP;
    vec4 fogColor;
} ubo;

layout(binding = 1) uniform sampler2DShadow shadowMap;
layout(binding = 3) uniform sampler2DArray terrainTex;

layout(location = 0) flat in vec3 fragNormal;
layout(location = 1)      in vec3 fragColor;
layout(location = 2)      in vec4 fragPosLightSpace;
layout(location = 3)      in float fragViewDepth;
layout(location = 4)      in vec2 fragUV;
layout(location = 5) flat in float fragLayer;
layout(location = 0) out vec4 outColor;

float materialTextureStrength(float layer) {
    int materialLayer = int(floor(layer + 0.5));
    switch (materialLayer) {
        case 0: return 0.62; // grass top
        case 1: return 0.82; // grass side
        case 2: return 1.05; // dirt
        case 3: return 1.15; // stone
        case 4: return 0.90; // wood
        case 5: return 0.58; // leaves
        case 6: return 1.05; // farmland
        case 7: return 0.90; // wheat
        case 8: return 0.35; // water fallback
        default: return 0.80;
    }
}

vec3 sampleMaterialDetail(vec2 uv, float layer) {
    if (layer < 0.0) return vec3(1.0);

    vec3 tex = texture(terrainTex, vec3(uv, layer)).rgb;
    float luma = dot(tex, vec3(0.299, 0.587, 0.114));

    float strength = materialTextureStrength(layer);
    float detail = clamp(1.0 + (luma - 0.5) * (0.80 * strength),
                         mix(1.0, 0.68, strength),
                         mix(1.0, 1.26, strength));
    vec3 chroma = tex / max(luma, 0.08);
    chroma = clamp(chroma, vec3(0.60), vec3(1.55));

    return mix(vec3(1.0), chroma, 0.24 * strength) * detail;
}

void main() {
    vec3  normal    = normalize(fragNormal);
    vec3  lightDir  = normalize(ubo.lightDir.xyz);
    float dayFactor = ubo.lightDir.w;

    // Shadow
    vec3  projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy    = projCoords.xy * 0.5 + 0.5;
    float shadow     = 1.0;
    if (dayFactor > 0.01 && projCoords.z >= 0.0 && projCoords.z <= 1.0) {
        float NdotL = max(dot(normal, lightDir), 0.0);
        float bias  = mix(0.0015, 0.0003, NdotL);
        float texel = 1.0 / 2048.0;
        shadow = 0.0;
        for (int x = -2; x <= 2; x++)
            for (int y = -2; y <= 2; y++)
                shadow += texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * texel, projCoords.z - bias));
        shadow /= 25.0;
    }
    float shadowFactor = max(shadow, 0.4);

    const vec3 SKY_AMBIENT    = vec3(0.96, 0.93, 0.88); // soft warm sky (no cool tint)
    const vec3 GROUND_AMBIENT = vec3(1.04, 0.90, 0.70); // warmer ground bounce
    float hemi = clamp(normal.z * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambientTint = mix(GROUND_AMBIENT, SKY_AMBIENT, hemi);

    float diff = max(dot(normal, lightDir), 0.0);
    vec3 ambient = ambientTint * mix(0.10, 0.30, dayFactor);
    vec3 direct  = vec3(diff * 0.7 * dayFactor * shadowFactor);

    // Material texture contributes low-strength detail; vertex color owns the style hue.
    vec3 materialDetail = sampleMaterialDetail(fragUV, fragLayer);
    vec3 litColor = fragColor * materialDetail * (ambient + direct);

    // Fog
    const float FOG_START = 27.0;
    const float FOG_END   = 57.0;
    float fogFactor = clamp((FOG_END - fragViewDepth) / (FOG_END - FOG_START), 0.0, 1.0);
    outColor = vec4(mix(ubo.fogColor.rgb, litColor, fogFactor), 1.0);
}
