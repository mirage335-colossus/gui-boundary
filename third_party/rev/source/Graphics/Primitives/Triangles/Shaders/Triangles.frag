#version 430 core

layout(std140, binding = 1) uniform Data {
    vec4 uColor;
    float depth, opacity, pad2, pad3;
};

in vec4 vColor;
out vec4 FragColor;

void main()
{
    FragColor = vColor;
    gl_FragDepth = depth;

    FragColor.a *= opacity; // Opacity
}