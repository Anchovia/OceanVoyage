#version 450

// Hero ship vertex shader: a full model matrix (push constant) lets the ship bob and
// tilt with the wave surface. This path is for imported textured ship assets.

layout(binding = 0) uniform UniformBufferObject {
    mat4 model; // unused here (ship transform comes from the push constant)
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    mat4 lightMVP;
    vec4 fogColor;
    vec4 clipPlane;
    vec4 animationParams;
    vec4 cameraPos;
} ubo;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_ClipDistance[1];
};

layout(push_constant) uniform ShipPush {
    mat4 model; // ship world transform: translate * orient(wave normal, heading)
} pc;

layout(location = 0) in vec3  inPosition;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec3  inTangent;
layout(location = 3) in vec2  inUV;

layout(location = 0)      out vec3  fragNormal;
layout(location = 1)      out vec3  fragTangent;
layout(location = 3)      out float fragViewDepth;
layout(location = 4)      out vec2  fragUV;
layout(location = 5)      out vec3  fragWorldPos;

void main() {
    vec4 worldPos     = pc.model * vec4(inPosition, 1.0);
    vec4 viewPos      = ubo.view * worldPos;
    gl_Position       = ubo.proj * viewPos;
    gl_ClipDistance[0] = dot(worldPos.xyz, ubo.clipPlane.xyz) + ubo.clipPlane.w;
    fragNormal        = mat3(pc.model) * inNormal;
    fragTangent       = mat3(pc.model) * inTangent;
    fragViewDepth     = -viewPos.z;
    fragUV            = inUV;
    fragWorldPos      = worldPos.xyz;
}
