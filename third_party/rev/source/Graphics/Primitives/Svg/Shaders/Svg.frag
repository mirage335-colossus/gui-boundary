#version 430 core

in vec2 fragUV;
out vec4 outColor;

layout(binding = 0) uniform sampler2D svgTex;

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;
    vec4 color;
    float rotation;
    float opacity;
};

void main() {

    outColor = texture(svgTex, fragUV);

    if (color.r + color.g + color.b + color.a > 0.0f) {
        outColor = vec4(color.rgb, outColor.a * color.a);
    }

    outColor.a *= opacity; // Opacity
}
