#version 450

layout(push_constant) uniform PushConstants {
    mat4 lightMVP;
} pc;

layout(location = 0) in vec3  inPosition;
layout(location = 3) in vec3  instancePos;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceRot;

void main() {
    // Same transform as object.vert (scale -> Z rotation -> translate)
    float s = sin(instanceRot);
    float c = cos(instanceRot);
    vec3 p  = inPosition * instanceScale;
    vec3 rp = vec3(p.x * c - p.y * s, p.x * s + p.y * c, p.z);
    vec3 worldPos = rp + instancePos;
    gl_Position = pc.lightMVP * vec4(worldPos, 1.0);
}
