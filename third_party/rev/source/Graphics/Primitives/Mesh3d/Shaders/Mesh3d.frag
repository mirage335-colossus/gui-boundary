#version 430 core

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

in vec4 vColor;
in vec3 vNormal;

out vec4 FragColor;

void main()
{
    vec3 N = normalize(vNormal);

    vec3 keyLight = normalize(uLightDir.xyz);
    vec3 fillLight = normalize(uLightDir2.xyz);

    float keyDiffuse = max(dot(N, keyLight), 0.0);
    float fillDiffuse = max(dot(N, fillLight), 0.0);

    // Two-direction CAD lighting: key + fill.
    float shade = 0.22 + 0.58 * keyDiffuse + 0.32 * fillDiffuse;

    FragColor = vec4(vColor.rgb * shade, vColor.a);

    FragColor.a *= opacity; // Opacity
}
