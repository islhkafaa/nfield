#version 450

layout(set = 0, binding = 0) uniform sampler2D hdrImage;
layout(set = 0, binding = 1) uniform sampler2D bloomImage;

layout(push_constant) uniform PC {
    float bloomIntensity;
} pc;

layout(location = 0) out vec4 outColor;

vec3 tonemap(vec3 hdr) {
    return hdr / (hdr + vec3(1.0));
}

void main() {
    vec2 uv   = gl_FragCoord.xy / vec2(textureSize(hdrImage, 0));
    vec3 hdr  = texture(hdrImage, uv).rgb;
    vec3 glow = texture(bloomImage, uv).rgb;

    vec3 combined = hdr + glow * pc.bloomIntensity;
    outColor = vec4(tonemap(combined), 1.0);
}
