#version 300 es
precision mediump float;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

uniform sampler2D tex;

void main() {
    vec4 pixColor = texture(tex, v_texcoord);
    // Warm night light filter (reduces blue light spectrum ~4500K)
    pixColor.r = min(1.0, pixColor.r * 1.0);
    pixColor.g = min(1.0, pixColor.g * 0.88);
    pixColor.b = min(1.0, pixColor.b * 0.68);
    fragColor = pixColor;
}
