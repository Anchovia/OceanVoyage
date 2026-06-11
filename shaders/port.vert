#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    mat4 lightMVP;
    vec4 fogColor;
    vec4 clipPlane;
    vec4 animationParams;
    vec4 cameraPos;
    mat4 reflectionViewProj;
    mat4 invViewProj;
    mat4 prevViewProj;
    vec4 temporalParams;
    mat4 lightMVPCascade[3];
    vec4 cascadeSplits;
} ubo;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_ClipDistance[1];
};

layout(push_constant) uniform PortPush {
    mat4 model;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragViewDepth;
layout(location = 3) out vec3 fragWorldPos;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    vec4 viewPos = ubo.view * worldPos;
    gl_Position = ubo.proj * viewPos;
    gl_ClipDistance[0] = dot(worldPos.xyz, ubo.clipPlane.xyz) + ubo.clipPlane.w;
    fragNormal = mat3(pc.model) * inNormal;
    fragColor = inColor;
    fragViewDepth = -viewPos.z;
    fragWorldPos = worldPos.xyz;
}
