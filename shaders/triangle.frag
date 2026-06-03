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

layout(location = 0) flat in vec3 fragNormal;
layout(location = 1) flat in vec3 fragTopColor;
layout(location = 2) flat in vec3 fragSideColor;
layout(location = 3)      in vec4 fragPosLightSpace;
layout(location = 4)      in float fragViewDepth;
layout(location = 0) out vec4 outColor;

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
