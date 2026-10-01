#version 430 core

in vec2 fragUV;
out vec4 outColor;

layout(binding = 0) uniform sampler2D videoTexture;
layout(std140, binding = 1) uniform Data {
    float x, y, w, h;
    float u, v, uw, vh;
    float opacity;
};

void main() {
    outColor = texture(videoTexture, fragUV);
    outColor.a *= opacity;
}
