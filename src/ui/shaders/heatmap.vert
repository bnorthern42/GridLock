#version 450
layout(location = 0) out vec2 v_texcoord;
void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(pos * 2.0f - 1.0f, 0.0f, 1.0f);
    v_texcoord = pos;
}
