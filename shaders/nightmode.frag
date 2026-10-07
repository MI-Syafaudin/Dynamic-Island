precision mediump float;
varying vec2 v_texcoord;
uniform sampler2D tex;

void main() {
    vec4 color = texture2D(tex, v_texcoord);
    // Warm night light filter (reduces blue light spectrum ~4500K)
    color.r = min(1.0, color.r * 1.0);
    color.g = min(1.0, color.g * 0.88);
    color.b = min(1.0, color.b * 0.68);
    gl_FragColor = color;
}
