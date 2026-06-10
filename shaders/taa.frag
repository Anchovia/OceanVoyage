#version 450

// TAA resolve: blend the current HDR scene against the reprojected history,
// clamped to the current 3x3 neighborhood so stale/disoccluded history cannot
// ghost. Runs before tone mapping (HDR in, HDR out). No jitter yet — this
// pass alone stabilizes temporal shimmer (FFT specular glints); subpixel
// jitter is added on top in a later step.
layout(binding = 0) uniform sampler2D sceneColor;    // current frame, HDR
layout(binding = 1) uniform sampler2D historyColor;  // previous resolve, HDR
layout(binding = 2) uniform sampler2D sceneDepth;    // current depth (post-water)

layout(push_constant) uniform TaaPushConstants {
    mat4 reprojection; // prevViewProj * inverse(curViewProj): current NDC -> previous clip
    vec4 params;       // xy = inverse framebuffer size, z = history valid (0/1), w unused
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const float HISTORY_BLEND = 0.9; // history weight when history is usable

void main() {
    ivec2 pix = ivec2(gl_FragCoord.xy);
    vec3 current = texelFetch(sceneColor, pix, 0).rgb;

    // 3x3 neighborhood bounds of the current frame — the only colors history
    // is allowed to contribute (clips ghosting from movement/disocclusion).
    vec3 nMin = current;
    vec3 nMax = current;
    for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
        if (x == 0 && y == 0) continue;
        vec3 c = texelFetch(sceneColor, pix + ivec2(x, y), 0).rgb;
        nMin = min(nMin, c);
        nMax = max(nMax, c);
    }

    // Reproject this pixel into the previous frame via the depth buffer.
    float depth = texelFetch(sceneDepth, pix, 0).r;
    vec4 prevClip = pc.reprojection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec2 prevUv = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    bool histUsable = pc.params.z > 0.5
        && prevClip.w > 0.0
        && all(greaterThanEqual(prevUv, vec2(0.0)))
        && all(lessThanEqual(prevUv, vec2(1.0)));

    vec3 resolved = current;
    if (histUsable) {
        vec3 history = texture(historyColor, prevUv).rgb;
        history = clamp(history, nMin, nMax);
        resolved = mix(current, history, HISTORY_BLEND);
    }
    outColor = vec4(resolved, 1.0);
}
