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
layout(binding = 2) uniform sampler2D grassTex;
layout(binding = 4) uniform sampler2D grassOpacityTex;

layout(location = 0)      in vec3 fragNormal;
layout(location = 1)      in vec2 fragUV;
layout(location = 2)      in vec4 fragPosLightSpace;
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

    // Shadow
    vec3  projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy    = projCoords.xy * 0.5 + 0.5;
    float shadow     = 1.0;
    if (dayFactor > 0.01 && projCoords.z >= 0.0 && projCoords.z <= 1.0) {
        float NdotL = max(dot(normal, lightDir), 0.0);
        float bias  = mix(0.0015, 0.0003, NdotL);
        float texelSize = 1.0 / 2048.0;
        shadow = 0.0;
        for (int x = -2; x <= 2; x++)
            for (int y = -2; y <= 2; y++)
                shadow += texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, projCoords.z - bias));
        shadow /= 25.0;
    }
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
