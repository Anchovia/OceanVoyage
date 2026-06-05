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
} ubo;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

float saturate(float v) {
    return clamp(v, 0.0, 1.0);
}

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float starField(vec3 dir) {
    float viewUp = saturate(dir.z);
    vec2 p = dir.xy / max(viewUp + 0.10, 0.10);
    vec2 cell = floor(p * 440.0);
    float rnd = hash21(cell);
    float star = step(0.9965, rnd) * smoothstep(0.22, 0.90, viewUp);
    return star * (0.65 + 0.35 * hash21(cell + vec2(37.0, 19.0)));
}

vec3 skyRadiance(vec3 dir, vec3 sunDir, float dayFactor, vec3 fogColor) {
    dir = normalize(dir);
    sunDir = normalize(sunDir);

    float viewUp = saturate(dir.z);
    float sunUp = saturate(sunDir.z);
    float lowSun = 1.0 - smoothstep(0.18, 0.78, sunUp);
    float nightFactor = 1.0 - smoothstep(0.02, 0.38, dayFactor);
    vec3 moonDir = normalize(-sunDir);
    float moonUp = saturate(moonDir.z);
    float moonGate = nightFactor * smoothstep(0.02, 0.72, moonUp);

    vec3 nightZenith = vec3(0.010, 0.015, 0.035);
    vec3 dayZenith = fogColor * vec3(0.48, 0.66, 1.12);
    vec3 zenith = mix(nightZenith, dayZenith, smoothstep(0.02, 0.85, dayFactor));

    vec3 horizon = fogColor * (1.12 + 0.24 * lowSun)
                 + vec3(0.45, 0.20, 0.055) * lowSun * smoothstep(0.02, 0.55, dayFactor);
    vec3 sky = mix(horizon, zenith, pow(viewUp, 0.55));

    float horizonAerial = exp(-viewUp * 7.5) * smoothstep(0.02, 0.75, dayFactor);
    sky += horizon * horizonAerial * 0.16;

    float sunCos = saturate(dot(dir, sunDir));
    float moonCos = saturate(dot(dir, moonDir));
    vec3 sunTint = mix(vec3(1.0, 0.48, 0.18), vec3(1.0, 0.93, 0.72), sunUp);
    vec3 moonTint = vec3(0.55, 0.68, 1.0);

    float miePower = mix(14.0, 86.0, sunUp);
    float sunGlow = pow(sunCos, miePower) * smoothstep(0.02, 0.70, dayFactor);
    float sunAureole = pow(sunCos, mix(2.2, 7.5, sunUp)) * smoothstep(0.02, 0.85, dayFactor);
    float sunDisc = smoothstep(0.99855, 0.99992, sunCos) * smoothstep(0.02, 0.35, dayFactor);
    sky += sunTint * (sunAureole * 0.34 + sunGlow * mix(1.35, 0.64, sunUp) + sunDisc * 8.0);

    float moonGlow = pow(moonCos, 38.0) * moonGate;
    float moonDisc = smoothstep(0.99880, 0.99992, moonCos) * moonGate;
    sky += moonTint * (moonGlow * 0.42 + moonDisc * 4.2);

    float stars = starField(dir) * nightFactor;
    sky += vec3(0.65, 0.78, 1.0) * stars * 0.45;

    return max(sky, vec3(0.0));
}

void main() {
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 farPoint = ubo.invViewProj * vec4(ndc, 1.0, 1.0);
    vec3 worldPos = farPoint.xyz / farPoint.w;
    vec3 dir = normalize(worldPos - ubo.cameraPos.xyz);

    vec3 sky = skyRadiance(dir, ubo.lightDir.xyz, ubo.lightDir.w, ubo.fogColor.rgb);
    outColor = vec4(sky, 1.0);
}
