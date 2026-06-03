#version 450

layout(binding = 0) uniform sampler2D grassOpacityTex;

layout(location = 0) in vec2 fragUV;

void main() {
    float opacity = texture(grassOpacityTex, fragUV).r;
    if (opacity < 0.28) discard;
}
