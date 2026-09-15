#version 300 es

precision mediump float;
in vec2 vTexCoord;
uniform sampler2D uTexture;
out vec4 fragColor;

void main() {
    fragColor = texture(uTexture, vTexCoord);
}