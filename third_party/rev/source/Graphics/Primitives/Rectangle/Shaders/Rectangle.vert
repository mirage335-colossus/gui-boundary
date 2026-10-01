#version 430 core

layout(location = 0) in float dummyVertexID;

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;                                   // Rect
    vec4 fillColor;                                    // Fill color
    float tl, tr, bl, br;                               // Corner radii
    float l_width, r_width, t_width, b_width;           // Border widths
    vec4 l_color, r_color, t_color, b_color;            // Border colors
    float shadowX, shadowY, shadowSize, shadowBlur;
    vec4 shadowColor;
    float opacity;
};

out vec2 fragLocalPos;
out vec4 fragColor;
out vec4 cornerMask;
out vec4 sideMask;

void main() {

    // Vertex lookup tables
    //--------------------------------------------------

    // Tl, tr, bl, br
    const vec2 offsets[4] = vec2[](
        vec2(0, 0), vec2(1, 0),
        vec2(1, 1), vec2(0, 1)
    );

    // L, r, t, b
    const vec4 sideMasks[4] = vec4[](
        vec4(1,0,1,0), vec4(0,1,1,0),
        vec4(0,1,0,1), vec4(1,0,0,1)
    );

    // Tl, tr, bl, br
    const vec4 cornerMasks[4] = vec4[](
        vec4(1,0,0,0), vec4(0,1,0,0),
        vec4(0,0,1,0), vec4(0,0,0,1) 
    );

    // Compute geometry
    //--------------------------------------------------

    int vid = gl_VertexID % 4;

    cornerMask = cornerMasks[vid];
    vec2 cornerOffset = offsets[vid];

    // Expand rect and attributes
    float shadowExtent = max(shadowSize + shadowBlur, 0);
    vec2 expandedOrigin = vec2(x, y) - vec2(shadowExtent);
    vec2 expandedSize   = vec2(w, h) + vec2(shadowExtent * 2.0);
    vec2 expandedPos    = vec2(shadowX, shadowY) + expandedOrigin + cornerOffset * expandedSize;

    // Side scores, based on actual distance to the original rect sides.
    // These are intentionally not 0/1 masks anymore.
    float minDim = max(min(w, h), 1.0);

    float dLeft   = expandedPos.x - x;
    float dRight  = (x + w) - expandedPos.x;
    float dTop    = expandedPos.y - y;
    float dBottom = (y + h) - expandedPos.y;

    sideMask = vec4(
        1.0 - dLeft   / minDim,
        1.0 - dRight  / minDim,
        1.0 - dTop    / minDim,
        1.0 - dBottom / minDim
    );

    // Final projection: expanded position, not original
    fragLocalPos = expandedPos;
    gl_Position = uProjection * vec4(expandedPos, 0.0, 1.0);
}
