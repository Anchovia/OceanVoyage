#version 450

// Hero ship vertex shader: a full model matrix (push constant) lets the ship bob AND
// tilt with the wave surface (the instanced object pipeline only does yaw). Outputs
// match chunk.frag, which this pipeline reuses for shading.

layout(binding = 0) uniform UniformBufferObject {
    mat4 model; // unused here (ship transform comes from the push constant)
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    mat4 lightMVP;
    vec4 fogColor;
} ubo;

layout(push_constant) uniform ShipPush {
    mat4 model; // ship world transform: translate * orient(wave normal, heading)
} pc;

layout(location = 0) in vec3  inPosition;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec3  inColor;
layout(location = 3) in vec2  inUV;
layout(location = 4) in float inLayer;

layout(location = 0) flat out vec3  fragNormal;
layout(location = 1)      out vec3  fragColor;
layout(location = 2)      out vec4  fragPosLightSpace;
layout(location = 3)      out float fragViewDepth;
layout(location = 4)      out vec2  fragUV;
layout(location = 5) flat out float fragLayer;

void main() {
    vec4 worldPos     = pc.model * vec4(inPosition, 1.0);
    vec4 viewPos      = ubo.view * worldPos;
    gl_Position       = ubo.proj * viewPos;
    fragNormal        = mat3(pc.model) * inNormal; // orientation is orthonormal (no skew)
    fragColor         = inColor;
    fragPosLightSpace = ubo.lightMVP * worldPos;
    fragViewDepth     = -viewPos.z;
    fragUV            = inUV;
    fragLayer         = inLayer;
}
