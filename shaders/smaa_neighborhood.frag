#version 450

// SMAA 1x pass 3: neighborhood blending plus HDR tone mapping/color grade.
layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D blendTex;

layout(push_constant) uniform PostPushConstants {
    vec4 params; // xy = inverse framebuffer size, z = AA mode, w = unused
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const float EXPOSURE   = 1.03;
const float CONTRAST   = 1.05;
const float SATURATION = 1.10;
const vec3  SHADOW_TINT = vec3(1.00, 0.99, 0.98);
const vec3  HIGH_TINT   = vec3(1.07, 1.02, 0.92);
const float VIGNETTE    = 0.22;

vec3 sampleScene(vec2 p) {
    return textureLod(sceneColor, clamp(p, vec2(0.0), vec2(1.0)), 0.0).rgb;
}

vec4 sampleBlend(vec2 p) {
    return texture(blendTex, clamp(p, vec2(0.0), vec2(1.0)));
}

vec3 toneMapACES(vec3 c) {
    c *= EXPOSURE;
    return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
}

vec3 applyGrade(vec3 c) {
    c = toneMapACES(c);
    c = (c - 0.5) * CONTRAST + 0.5;

    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(luma), c, SATURATION);

    float t = clamp(dot(c, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
    c *= mix(SHADOW_TINT, HIGH_TINT, t);

    float vig = smoothstep(0.85, 0.25, length(uv - 0.5));
    c *= mix(1.0 - VIGNETTE, 1.0, vig);

    return clamp(c, 0.0, 1.0);
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

    outColor = vec4(applyGrade(c), 1.0);
}
