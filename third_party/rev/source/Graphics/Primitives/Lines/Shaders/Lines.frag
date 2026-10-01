#version 430 core

layout(std140, binding = 1) uniform Data {
    vec4 uColor;
    float depth, opacity, pad2, pad3;
};

in vec4 vColor;
in float vEdgeDist;

out vec4 FragColor;

void main() {
    float d = abs(vEdgeDist);
    float alpha = 1.0 - smoothstep(1.0 - fwidth(vEdgeDist), 1.0, d);
    FragColor = vec4(vColor.rgb, vColor.a * alpha);

    FragColor.a *= opacity; // Opacity
}
