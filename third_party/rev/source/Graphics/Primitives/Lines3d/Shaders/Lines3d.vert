#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in float a;
layout(location = 4) in float b;

layout(std140, binding = 1) uniform Data {
    vec4 uColor;
    float depth, opacity, pad2, pad3;
};

layout(std140, binding = 2) uniform Camera {
    mat4 uViewProj;
    vec4 uLightDir;
    vec4 uEyePos;
    vec4 uLightDir2;
};

// Per-actor transform stack: uViewProj × uWorld × uModel.
// Both default to identity, so unset actors render exactly as before.
layout(std140, binding = 3) uniform Model {
    mat4 uWorld;
    mat4 uModel;
};

out vec4 vColor;

void main()
{
    vColor = (aColor.a != 0.0) ? aColor : uColor;
    gl_Position = uViewProj * uWorld * uModel * vec4(aPos, 1.0);
}