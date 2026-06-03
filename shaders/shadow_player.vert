#version 450

layout(push_constant) uniform PushConstants {
    mat4 lightMVP;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec3 instancePos;

void main() {
    gl_Position = pc.lightMVP * vec4(inPosition + instancePos, 1.0);
}
