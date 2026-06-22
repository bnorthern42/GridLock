#version 450
layout(binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
vec3 magma(float t) { return vec3(t, t*0.5, 0.2 + t*0.8); }
void main() {
    float val = texture(tex, v_texcoord).r;
    fragColor = vec4(magma(val), 1.0);
}
