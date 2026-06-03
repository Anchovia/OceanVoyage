#version 450

// SMAA 1x pass 2: horizontal/vertical blending weight calculation.
layout(binding = 0) uniform sampler2D edgesTex;
layout(binding = 1) uniform sampler2D areaTex;
layout(binding = 2) uniform sampler2D searchTex;

layout(push_constant) uniform PostPushConstants {
    vec4 params; // xy = inverse framebuffer size, z = AA mode, w = unused
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const int   SMAA_MAX_SEARCH_STEPS = 32;
const float SMAA_AREATEX_MAX_DISTANCE = 16.0;
const vec2  SMAA_AREATEX_PIXEL_SIZE = vec2(1.0 / 160.0, 1.0 / 560.0);
const float SMAA_AREATEX_SUBTEX_SIZE = 1.0 / 7.0;
const vec2  SMAA_SEARCHTEX_SIZE = vec2(66.0, 33.0);
const vec2  SMAA_SEARCHTEX_PACKED_SIZE = vec2(64.0, 16.0);
const float SMAA_CORNER_ROUNDING_NORM = 0.25;
const int   SMAA_MAX_SEARCH_STEPS_DIAG = 16;
const float SMAA_AREATEX_MAX_DISTANCE_DIAG = 20.0;

vec2 sampleEdges(vec2 p) {
    return textureLod(edgesTex, clamp(p, vec2(0.0), vec2(1.0)), 0.0).rg;
}

vec2 sampleEdgesOffset(vec2 p, ivec2 offset) {
    vec2 coord = p + vec2(offset) * pc.params.xy;
    return textureLod(edgesTex, clamp(coord, vec2(0.0), vec2(1.0)), 0.0).rg;
}

float searchLength(vec2 e, float offset) {
    vec2 scale = SMAA_SEARCHTEX_SIZE * vec2(0.5, -1.0);
    vec2 bias = SMAA_SEARCHTEX_SIZE * vec2(offset, 1.0);

    scale += vec2(-1.0, 1.0);
    bias += vec2(0.5, -0.5);

    scale /= SMAA_SEARCHTEX_PACKED_SIZE;
    bias /= SMAA_SEARCHTEX_PACKED_SIZE;

    return textureLod(searchTex, scale * e + bias, 0.0).r;
}

float searchXLeft(vec2 texcoord, float end) {
    vec2 r = pc.params.xy;
    vec2 e = vec2(0.0, 1.0);
    while (texcoord.x > end && e.g > 0.8281 && e.r == 0.0) {
        e = sampleEdges(texcoord);
        texcoord -= vec2(2.0 * r.x, 0.0);
    }
    float offset = -(255.0 / 127.0) * searchLength(e, 0.0) + 3.25;
    return texcoord.x + r.x * offset;
}

float searchXRight(vec2 texcoord, float end) {
    vec2 r = pc.params.xy;
    vec2 e = vec2(0.0, 1.0);
    while (texcoord.x < end && e.g > 0.8281 && e.r == 0.0) {
        e = sampleEdges(texcoord);
        texcoord += vec2(2.0 * r.x, 0.0);
    }
    float offset = -(255.0 / 127.0) * searchLength(e, 0.5) + 3.25;
    return texcoord.x - r.x * offset;
}

float searchYUp(vec2 texcoord, float end) {
    vec2 r = pc.params.xy;
    vec2 e = vec2(1.0, 0.0);
    while (texcoord.y > end && e.r > 0.8281 && e.g == 0.0) {
        e = sampleEdges(texcoord);
        texcoord -= vec2(0.0, 2.0 * r.y);
    }
    float offset = -(255.0 / 127.0) * searchLength(e.gr, 0.0) + 3.25;
    return texcoord.y + r.y * offset;
}

float searchYDown(vec2 texcoord, float end) {
    vec2 r = pc.params.xy;
    vec2 e = vec2(1.0, 0.0);
    while (texcoord.y < end && e.r > 0.8281 && e.g == 0.0) {
        e = sampleEdges(texcoord);
        texcoord += vec2(0.0, 2.0 * r.y);
    }
    float offset = -(255.0 / 127.0) * searchLength(e.gr, 0.5) + 3.25;
    return texcoord.y - r.y * offset;
}

vec2 area(vec2 dist, float e1, float e2, float offset) {
    vec2 texcoord = SMAA_AREATEX_MAX_DISTANCE * round(4.0 * vec2(e1, e2)) + dist;
    texcoord = SMAA_AREATEX_PIXEL_SIZE * texcoord + 0.5 * SMAA_AREATEX_PIXEL_SIZE;
    texcoord.y = SMAA_AREATEX_SUBTEX_SIZE * offset + texcoord.y;
    return textureLod(areaTex, texcoord, 0.0).rg;
}

void detectHorizontalCornerPattern(inout vec2 weights, vec4 coords, vec2 d) {
    vec2 leftRight = step(d.xy, d.yx);
    vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;
    rounding /= max(leftRight.x + leftRight.y, 0.0001);

    vec2 factor = vec2(1.0);
    factor.x -= rounding.x * sampleEdgesOffset(coords.xy, ivec2(0, 1)).r;
    factor.x -= rounding.y * sampleEdgesOffset(coords.zw, ivec2(1, 1)).r;
    factor.y -= rounding.x * sampleEdgesOffset(coords.xy, ivec2(0, -2)).r;
    factor.y -= rounding.y * sampleEdgesOffset(coords.zw, ivec2(1, -2)).r;

    weights *= clamp(factor, vec2(0.0), vec2(1.0));
}

void detectVerticalCornerPattern(inout vec2 weights, vec4 coords, vec2 d) {
    vec2 leftRight = step(d.xy, d.yx);
    vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;
    rounding /= max(leftRight.x + leftRight.y, 0.0001);

    vec2 factor = vec2(1.0);
    factor.x -= rounding.x * sampleEdgesOffset(coords.xy, ivec2(1, 0)).g;
    factor.x -= rounding.y * sampleEdgesOffset(coords.zw, ivec2(1, 1)).g;
    factor.y -= rounding.x * sampleEdgesOffset(coords.xy, ivec2(-2, 0)).g;
    factor.y -= rounding.y * sampleEdgesOffset(coords.zw, ivec2(-2, 1)).g;

    weights *= clamp(factor, vec2(0.0), vec2(1.0));
}

// Diagonal pattern detection (ported from Iryoku SMAA.hlsl). Diagonal area data
// lives in the right half of the same areaTex (texcoord.x += 0.5). subsampleIndices
// is zero for SMAA 1x, so the diagonal area subtex offsets collapse to 0.

// Decode two binary edge values packed into one bilinear-filtered fetch.
vec2 decodeDiagBilinearAccess(vec2 e) {
    e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);
    return round(e);
}

vec4 decodeDiagBilinearAccess(vec4 e) {
    e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);
    return round(e);
}

// Search for the diagonal line end along 'dir' (non-bilinear fetch path).
vec2 searchDiag1(vec2 texcoord, vec2 dir, out vec2 e) {
    vec4 coord = vec4(texcoord, -1.0, 1.0);
    vec3 t = vec3(pc.params.xy, 1.0);
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9) {
        coord.xyz = t * vec3(dir, 1.0) + coord.xyz;
        e = textureLod(edgesTex, coord.xy, 0.0).rg;
        coord.w = dot(e, vec2(0.5));
    }
    return coord.zw;
}

// Search for the diagonal line end along 'dir' (bilinear-optimized fetch path).
vec2 searchDiag2(vec2 texcoord, vec2 dir, out vec2 e) {
    vec4 coord = vec4(texcoord, -1.0, 1.0);
    coord.x += 0.25 * pc.params.x;
    vec3 t = vec3(pc.params.xy, 1.0);
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9) {
        coord.xyz = t * vec3(dir, 1.0) + coord.xyz;
        e = textureLod(edgesTex, coord.xy, 0.0).rg;
        e = decodeDiagBilinearAccess(e);
        coord.w = dot(e, vec2(0.5));
    }
    return coord.zw;
}

// Sample the diagonal area LUT (right half of areaTex) for a diagonal distance.
vec2 areaDiag(vec2 dist, vec2 e, float offset) {
    vec2 texcoord = vec2(SMAA_AREATEX_MAX_DISTANCE_DIAG) * e + dist;
    texcoord = SMAA_AREATEX_PIXEL_SIZE * texcoord + 0.5 * SMAA_AREATEX_PIXEL_SIZE;
    texcoord.x += 0.5;
    texcoord.y += SMAA_AREATEX_SUBTEX_SIZE * offset;
    return textureLod(areaTex, texcoord, 0.0).rg;
}

// Conditional move: where cond is true, copy value into variable (SMAAMovc).
void movc(bvec2 cond, inout vec2 variable, vec2 value) {
    if (cond.x) variable.x = value.x;
    if (cond.y) variable.y = value.y;
}

// Search diagonal patterns and return the corresponding blend weights.
vec2 calculateDiagWeights(vec2 texcoord, vec2 e, vec4 subsampleIndices) {
    vec2 weights = vec2(0.0);
    vec4 rt = pc.params.xyxy;

    vec4 d;
    vec2 end;
    if (e.r > 0.0) {
        d.xz = searchDiag1(texcoord, vec2(-1.0, 1.0), end);
        d.x += float(end.y > 0.9);
    } else {
        d.xz = vec2(0.0);
    }
    d.yw = searchDiag1(texcoord, vec2(1.0, -1.0), end);

    if (d.x + d.y > 2.0) {
        vec4 coords = vec4(-d.x + 0.25, d.x, d.y, -d.y - 0.25) * rt + texcoord.xyxy;
        vec4 c;
        c.xy = sampleEdgesOffset(coords.xy, ivec2(-1, 0));
        c.zw = sampleEdgesOffset(coords.zw, ivec2(1, 0));
        c.yxwz = decodeDiagBilinearAccess(c.xyzw);
        vec2 cc = vec2(2.0) * c.xz + c.yw;
        movc(bvec2(step(vec2(0.9), d.zw)), cc, vec2(0.0));
        weights += areaDiag(d.xy, cc, subsampleIndices.z);
    }

    d.xz = searchDiag2(texcoord, vec2(-1.0, -1.0), end);
    if (sampleEdgesOffset(texcoord, ivec2(1, 0)).r > 0.0) {
        d.yw = searchDiag2(texcoord, vec2(1.0, 1.0), end);
        d.y += float(end.y > 0.9);
    } else {
        d.yw = vec2(0.0);
    }

    if (d.x + d.y > 2.0) {
        vec4 coords = vec4(-d.x, -d.x, d.y, d.y) * rt + texcoord.xyxy;
        vec4 c;
        c.x = sampleEdgesOffset(coords.xy, ivec2(-1, 0)).g;
        c.y = sampleEdgesOffset(coords.xy, ivec2(0, -1)).r;
        c.zw = sampleEdgesOffset(coords.zw, ivec2(1, 0)).gr;
        vec2 cc = vec2(2.0) * c.xz + c.yw;
        movc(bvec2(step(vec2(0.9), d.zw)), cc, vec2(0.0));
        weights += areaDiag(d.xy, cc, subsampleIndices.w).gr;
    }

    return weights;
}

void main() {
    vec2 r = pc.params.xy;
    vec2 frameSize = 1.0 / r;
    vec2 pixcoord = uv * frameSize;

    vec4 offset0 = r.xyxy * vec4(-0.25, -0.125, 1.25, -0.125) + uv.xyxy;
    vec4 offset1 = r.xyxy * vec4(-0.125, -0.25, -0.125, 1.25) + uv.xyxy;
    vec4 offset2 = vec4(offset0.xz, offset1.yw) +
                   vec4(-2.0, 2.0, -2.0, 2.0) * r.xxyy * float(SMAA_MAX_SEARCH_STEPS);

    vec4 weights = vec4(0.0);
    vec2 e = texture(edgesTex, uv).rg;

    if (e.g > 0.0) {
        // Diagonals have both north and west edges; search here first and give
        // diagonal patterns priority over horizontal/vertical processing.
        weights.rg = calculateDiagWeights(uv, e, vec4(0.0));

        if (weights.r == -weights.g) { // diag found nothing -> orthogonal search
            vec2 d;
            vec3 coords;
            coords.x = searchXLeft(offset0.xy, offset2.x);
            coords.y = offset1.y;
            d.x = coords.x;

            float e1 = textureLod(edgesTex, coords.xy, 0.0).r;

            coords.z = searchXRight(offset0.zw, offset2.y);
            d.y = coords.z;

            d = abs(round(frameSize.xx * d - pixcoord.xx));
            vec2 sqrtD = sqrt(d);
            float e2 = sampleEdgesOffset(coords.zy, ivec2(1, 0)).r;

            weights.rg = area(sqrtD, e1, e2, 0.0);
            coords.y = uv.y;
            detectHorizontalCornerPattern(weights.rg, coords.xyzy, d);
        } else {
            e.r = 0.0; // diag found -> skip vertical processing
        }
    }

    if (e.r > 0.0) {
        vec2 d;
        vec3 coords;
        coords.y = searchYUp(offset1.xy, offset2.z);
        coords.x = offset0.x;
        d.x = coords.y;

        float e1 = textureLod(edgesTex, coords.xy, 0.0).g;

        coords.z = searchYDown(offset1.zw, offset2.w);
        d.y = coords.z;

        d = abs(round(frameSize.yy * d - pixcoord.yy));
        vec2 sqrtD = sqrt(d);
        float e2 = sampleEdgesOffset(coords.xz, ivec2(0, 1)).g;

        weights.ba = area(sqrtD, e1, e2, 0.0);
        coords.x = uv.x;
        detectVerticalCornerPattern(weights.ba, coords.xyxz, d);
    }

    outColor = weights;
}
