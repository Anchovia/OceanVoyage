#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    mat4 lightMVP;
    vec4 fogColor;
    vec4 animationParams;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 instancePos;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceRot;

layout(location = 0)      out vec3 fragNormal;
layout(location = 1)      out vec2 fragUV;
layout(location = 2)      out vec4 fragPosLightSpace;
layout(location = 3)      out float fragViewDepth;
layout(location = 4)      out vec3 fragTint;
layout(location = 5)      out float fragFade;
layout(location = 6)      out float fragRootShade;
layout(location = 7)      out vec3 fragViewPos;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    float s = sin(instanceRot);
    float c = cos(instanceRot);

    float h0 = hash12(instancePos.xy + vec2(11.0, 23.0));
    float h1 = hash12(instancePos.xy + vec2(37.0, 53.0));
    float h2 = hash12(instancePos.xy + vec2(71.0, 97.0));

    vec3 p = inPosition;
    p.xy *= mix(0.88, 1.14, h0);
    p.z  *= mix(0.86, 1.12, h1);

    float rootShade = 1.0 - smoothstep(0.015, 0.20, p.z);
    float topFactor = smoothstep(0.08, 0.38, p.z);
    vec2 windDir = normalize(vec2(0.82, 0.42));
    float time = ubo.animationParams.x;
    float gust = 0.65 + 0.35 * sin(time * 0.45 + instancePos.x * 0.11 + instancePos.y * 0.07);
    float wave = sin(time * 2.40 + instancePos.x * 1.30 + instancePos.y * 1.70 + h0 * 6.2831853);
    float cross = sin(time * 1.30 + instancePos.x * 0.60 - instancePos.y * 0.80 + h1 * 6.2831853);
    p.xy += windDir * (wave * 0.018 * gust + cross * 0.006) * topFactor;

    p   *= instanceScale;

    vec3 rp = vec3(p.x * c - p.y * s, p.x * s + p.y * c, p.z);
    vec3 worldPos = rp + instancePos;

    vec3 n = inNormal;
    vec3 rn = vec3(n.x * c - n.y * s, n.x * s + n.y * c, n.z);

    vec4 viewPos      = ubo.view * vec4(worldPos, 1.0);
    float viewDepth   = -viewPos.z;
    gl_Position       = ubo.proj * viewPos;
    fragNormal        = rn;
    fragUV            = inUV;
    fragPosLightSpace = ubo.lightMVP * vec4(worldPos, 1.0);
    fragViewDepth     = viewDepth;
    fragTint          = mix(vec3(0.76, 0.98, 0.72), vec3(0.90, 1.04, 0.68), h2);
    fragFade          = clamp(1.0 - smoothstep(36.0, 62.0, viewDepth), 0.0, 1.0);
    fragRootShade     = rootShade;
    fragViewPos       = viewPos.xyz;
}
