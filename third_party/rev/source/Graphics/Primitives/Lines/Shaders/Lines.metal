#include <metal_stdlib>
using namespace metal;

// --- Uniform Buffers ---

// Projection matrix (matches std140 binding = 0)
struct Transform {
    float4x4 uProjection;
};

// Stroke color + parameters (matches std140 binding = 1)
struct Data {
    float4 color;       // uColor
    float depth;
    float pad1;
    float pad2;
    float pad3;
};

// --- Vertex Input / Output ---

struct VertexIn {
    float2 aPos      [[attribute(0)]];
    float4 aColor    [[attribute(1)]];
    float  aEdgeDist [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 vColor;
    float  vEdgeDist;
};

// --- Vertex Shader ---

vertex VertexOut vertex_main(VertexIn in              [[stage_in]],
                             constant Transform& transform [[buffer(10)]],
                             constant Data& data          [[buffer(11)]])
{
    VertexOut out;

    // Apply projection
    out.position = transform.uProjection * float4(in.aPos, 0.0, 1.0);

    // Choose vertex or uniform color
    out.vColor = (in.aColor.a != 0.0) ? in.aColor : data.color;

    // Pass through edge distance
    out.vEdgeDist = in.aEdgeDist;

    return out;
}

// --- Fragment Shader ---

fragment float4 fragment_main(VertexOut in [[stage_in]])
{
    // Edge distance AA
    float d = fabs(in.vEdgeDist);
    float w = fwidth(in.vEdgeDist);
    float alpha = 1.0 - smoothstep(1.0 - w, 1.0, d);

    return float4(in.vColor.rgb, in.vColor.a * alpha);
}
