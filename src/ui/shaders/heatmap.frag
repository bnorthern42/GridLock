#version 450
layout(binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform Constants {
    float min_val;
    float max_val;
} pc;

vec3 magma(float t) { return vec3(t, t*0.5, 0.2 + t*0.8); }

void main() {
    float val = texture(tex, v_texcoord).r;
    float norm_val;
    if (pc.max_val == pc.min_val) {
        norm_val = 0.5;
    } else {
        norm_val = (val - pc.min_val) / (pc.max_val - pc.min_val);
    }
    fragColor = vec4(magma(norm_val), 1.0);
}
