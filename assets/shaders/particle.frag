#version 450

layout(location = 0) in vec4 inColor;
layout(location = 0) out vec4 outColor;

void main() {
    float dist = length(gl_PointCoord - vec2(0.5));
    if (dist > 0.5) {
        discard;
    }
    float alpha = 1.0 - smoothstep(0.45, 0.5, dist);
    outColor = vec4(inColor.rgb, alpha * inColor.a);
}
