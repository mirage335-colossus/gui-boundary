#version 430 core

layout(location = 0) in float dummyVertexID;

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;
    vec4 color;
    float rotation;
    float opacity;
};

out vec2 fragUV;

void main() {

    // TL, TR, BR, BL in your order:
    const vec2 offsets[4] = vec2[](
        vec2(0, 1), // top-left
        vec2(1, 1), // top-right
        vec2(1, 0), // bottom-right
        vec2(0, 0)  // bottom-left
    );

    int vid = gl_VertexID % 4;

    vec2 cornerOffset = offsets[vid];
    vec2 origin = vec2(x, y);
    vec2 size = vec2(w, h);

    // Local quad position
    vec2 pxPos = origin + cornerOffset * size;

    //--------------- Rotate around center ------------------

    // Compute quad center
    vec2 center = origin + size * 0.5;

    // Offset from center
    vec2 pos = pxPos - center;

    // Rotation matrix
    float s = sin(rotation);
    float c = cos(rotation);
    mat2 R = mat2(c, -s,
                  s,  c);

    // Rotate and reapply translation
    pxPos = center + R * pos;

    // Output UV (simple full-quad mapping)
    fragUV = cornerOffset;

    gl_Position = uProjection * vec4(pxPos, 0.0, 1.0);
}