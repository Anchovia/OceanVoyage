#version 450

// SMAA 1x pass 1: luma edge detection on the tone-mapped/graded LDR target.
// The input is already display-referred (post pass ran first); only a gamma
// step remains to make the luma perceptual, as SMAA's threshold expects.
layout(binding = 0) uniform sampler2D sceneColor;

layout(push_constant) uniform PostPushConstants {
    vec4 params; // xy = inverse framebuffer size, z = AA mode, w = unused
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const float SMAA_THRESHOLD = 0.05;
const float SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR = 2.0;

float luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 sampleScene(vec2 p) {
    vec3 c = textureLod(sceneColor, clamp(p, vec2(0.0), vec2(1.0)), 0.0).rgb;
    return pow(c, vec3(1.0 / 2.2));
}

void main() {
    vec2 r = pc.params.xy;
    vec2 x = vec2(r.x, 0.0);
    vec2 y = vec2(0.0, r.y);

    float L = luma(sampleScene(uv));
    float Lleft = luma(sampleScene(uv - x));
    float Ltop = luma(sampleScene(uv - y));

    vec4 delta;
    delta.xy = abs(L - vec2(Lleft, Ltop));
    vec2 edges = step(vec2(SMAA_THRESHOLD), delta.xy);

    if (dot(edges, vec2(1.0)) == 0.0) {
        discard;
    }

    float Lright = luma(sampleScene(uv + x));
    float Lbottom = luma(sampleScene(uv + y));
    delta.zw = abs(L - vec2(Lright, Lbottom));

    vec2 maxDelta = max(delta.xy, delta.zw);

    float LleftLeft = luma(sampleScene(uv - 2.0 * x));
    float LtopTop = luma(sampleScene(uv - 2.0 * y));
    delta.zw = abs(vec2(Lleft, Ltop) - vec2(LleftLeft, LtopTop));

    maxDelta = max(maxDelta, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);
    edges *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);

    outColor = vec4(edges, 0.0, 0.0);
}
