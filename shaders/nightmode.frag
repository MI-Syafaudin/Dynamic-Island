#version 300 es
precision highp float;

in vec2 v_texcoord;
out vec4 fragColor;

uniform sampler2D tex;

void main() {
    vec4 pixColor = texture(tex, v_texcoord);
    // Warm eye comfort filter (gentle blue light reduction ~4500K)
    // Enforce opaque alpha = 1.0 to guarantee direct KMS scanout stability and eliminate flicker
    fragColor = vec4(pixColor.r, pixColor.g * 0.88, pixColor.b * 0.68, 1.0);
}
