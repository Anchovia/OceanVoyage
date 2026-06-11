#version 450

// SMAA 1x pass 3: neighborhood blending on the tone-mapped/graded LDR target.
// Tone mapping and grading already ran in the post pass that produced the LDR
// input, so this is a pure SMAA resolve straight to the swapchain.
layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D blendTex;

layout(push_constant) uniform PostPushConstants {
    vec4 params; // xy = inverse framebuffer size, z = AA mode, w = unused
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

vec3 sampleScene(vec2 p) {
    return textureLod(sceneColor, clamp(p, vec2(0.0), vec2(1.0)), 0.0).rgb;
}

vec4 sampleBlend(vec2 p) {
    return texture(blendTex, clamp(p, vec2(0.0), vec2(1.0)));
}

void main() {
    vec2 r = pc.params.xy;
    vec4 a;
    a.x = sampleBlend(uv + vec2(r.x, 0.0)).a; // right
    a.y = sampleBlend(uv + vec2(0.0, r.y)).g; // top
    a.wz = sampleBlend(uv).xz;                // bottom / left

    vec3 c;
    if (dot(a, vec4(1.0)) < 1e-5) {
        c = sampleScene(uv);
    } else {
        bool horizontal = max(a.x, a.z) > max(a.y, a.w);
        vec4 blendingOffset = vec4(0.0, a.y, 0.0, a.w);
        vec2 blendingWeight = a.yw;

        if (horizontal) {
            blendingOffset = vec4(a.x, 0.0, a.z, 0.0);
            blendingWeight = a.xz;
        }

        blendingWeight /= max(dot(blendingWeight, vec2(1.0)), 0.0001);
        vec4 blendingCoord = blendingOffset * vec4(r.xy, -r.xy) + uv.xyxy;

        c = blendingWeight.x * sampleScene(blendingCoord.xy);
        c += blendingWeight.y * sampleScene(blendingCoord.zw);
    }

    outColor = vec4(c, 1.0);
}
