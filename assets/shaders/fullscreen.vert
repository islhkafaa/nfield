#version 450

// Generates a clip-space triangle that covers the entire screen.
// No vertex buffer needed; invoke with vkCmdDraw(3, 1, 0, 0).
void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
