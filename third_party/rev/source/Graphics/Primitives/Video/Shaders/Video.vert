#version 430 core

layout(location = 0) in float dummyVertexID;

layout(std140, binding = 0) uniform Transform { mat4 uProjection; };
layout(std140, binding = 1) uniform Data {
    float x, y, w, h;
    float u, v, uw, vh;
    float opacity;
};

out vec2 fragUV;

void main() {
    const vec2 offsets[4] = vec2[](
        vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1)
    );
    vec2 corner = offsets[gl_VertexID % 4];
    fragUV = vec2(u, v) + corner * vec2(uw, vh);
    gl_Position = uProjection * vec4(vec2(x, y) + corner * vec2(w, h), 0.0, 1.0);
}
