#version 450

layout(set = 0, binding = 0) uniform sampler2D hdrImage;

layout(push_constant) uniform PC {
    float trailDecay;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(hdrImage, 0));
    vec4 prev = texture(hdrImage, uv);
    outColor = vec4(prev.rgb * pc.trailDecay, prev.a);
}
