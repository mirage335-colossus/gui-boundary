#version 430 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aEdgeDist;

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

layout(std140, binding = 1) uniform Data {
    vec4 uColor;
    float depth, opacity, pad2, pad3;
};

out vec4 vColor;
out float vEdgeDist;

void main() {

    vColor = (aColor.a != 0.0) ? aColor : uColor;
    vEdgeDist = aEdgeDist;
    
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
