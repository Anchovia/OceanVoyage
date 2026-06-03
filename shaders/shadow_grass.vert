#version 450

layout(push_constant) uniform PushConstants {
    mat4 lightMVP;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 instancePos;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceRot;

layout(location = 0) out vec2 fragUV;

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

    vec3 p = inPosition;
    p.xy *= mix(0.88, 1.14, h0);
    p.z  *= mix(0.86, 1.12, h1);
    p   *= instanceScale;

    vec3 rp = vec3(p.x * c - p.y * s, p.x * s + p.y * c, p.z);
    vec3 worldPos = rp + instancePos;

    gl_Position = pc.lightMVP * vec4(worldPos, 1.0);
    fragUV = inUV;
}
