#version 450

// Post-process: HDR scene color -> tone mapping -> color grading -> swapchain.
layout(binding = 0) uniform sampler2D sceneColor;

layout(push_constant) uniform PostPushConstants {
    vec4 params; // xy = inverse framebuffer size, z = AA mode, w = unused
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

// --- Grade params (tune to taste) ---
const float EXPOSURE   = 1.03;
const float CONTRAST   = 1.05;   // around mid grey
const float SATURATION = 1.10;
const vec3  SHADOW_TINT = vec3(1.00, 0.99, 0.98); // neutral / faintly warm shadows
const vec3  HIGH_TINT   = vec3(1.07, 1.02, 0.92); // warmer highlights
const float VIGNETTE    = 0.22;  // 0 = off, 1 = strong

const float FXAA_EDGE_THRESHOLD_MIN = 0.0312;
const float FXAA_EDGE_THRESHOLD     = 0.125;
const float FXAA_REDUCE_MUL         = 1.0 / 8.0;
const float FXAA_REDUCE_MIN         = 1.0 / 128.0;
const float FXAA_SPAN_MAX           = 8.0;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

vec3 toneMapACES(vec3 c) {
    c *= EXPOSURE;
    return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
}

// FXAA edge/direction decisions use perceptual luma after tone mapping so HDR
// highlights do not dominate edge thresholds.
float lumaPerceptual(vec3 linearC) {
    return luminance(pow(toneMapACES(linearC), vec3(1.0 / 2.2)));
}

vec3 sampleScene(vec2 p) {
    return texture(sceneColor, clamp(p, vec2(0.0), vec2(1.0))).rgb;
}

vec3 applyFxaa(vec2 p) {
    vec2 rcpFrame = pc.params.xy;

    vec3 rgbM  = sampleScene(p);
    vec3 rgbNW = sampleScene(p + rcpFrame * vec2(-1.0, -1.0));
    vec3 rgbNE = sampleScene(p + rcpFrame * vec2( 1.0, -1.0));
    vec3 rgbSW = sampleScene(p + rcpFrame * vec2(-1.0,  1.0));
    vec3 rgbSE = sampleScene(p + rcpFrame * vec2( 1.0,  1.0));

    float lumaM  = lumaPerceptual(rgbM);
    float lumaNW = lumaPerceptual(rgbNW);
    float lumaNE = lumaPerceptual(rgbNE);
    float lumaSW = lumaPerceptual(rgbSW);
    float lumaSE = lumaPerceptual(rgbSE);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD)) {
        return rgbM;
    }

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * FXAA_REDUCE_MUL, FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * rcpFrame;

    vec3 rgbA = 0.5 * (
        sampleScene(p + dir * (1.0 / 3.0 - 0.5)) +
        sampleScene(p + dir * (2.0 / 3.0 - 0.5))
    );
    vec3 rgbB = 0.5 * rgbA + 0.25 * (
        sampleScene(p + dir * -0.5) +
        sampleScene(p + dir *  0.5)
    );

    float lumaB = lumaPerceptual(rgbB);
    if (lumaB < lumaMin || lumaB > lumaMax) {
        return rgbA;
    }
    return rgbB;
}

vec3 applyGrade(vec3 c) {
    c = toneMapACES(c);

    // contrast around mid grey
    c = (c - 0.5) * CONTRAST + 0.5;

    // saturation
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(luma), c, SATURATION);

    // split tone: cool shadows, warm highlights (by luminance)
    float t = clamp(dot(c, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
    c *= mix(SHADOW_TINT, HIGH_TINT, t);

    // vignette (screen-space radial)
    float vig = smoothstep(0.85, 0.25, length(uv - 0.5));
    c *= mix(1.0 - VIGNETTE, 1.0, vig);

    return clamp(c, 0.0, 1.0);
}

void main() {
    int aaMode = int(pc.params.z + 0.5);
    vec3 c = (aaMode == 0) ? sampleScene(uv) : applyFxaa(uv);
    outColor = vec4(applyGrade(c), 1.0);
}
