#include <metal_stdlib>
using namespace metal;

// --- Uniform Buffers ---

// Projection matrix (binding = 0)
struct Transform
{
    float4x4 uProjection;
};

// Uniform color (binding = 1)
struct Data
{
    float4 uColor; // (r,g,b,a)
};

// --- Vertex Input / Output ---

struct VertexIn
{
    float2 aPos   [[attribute(0)]];
    float4 aColor [[attribute(1)]];
};

struct VertexOut
{
    float4 position [[position]];
    float4 vColor;
};

// --- Vertex Shader ---

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Transform& transform [[buffer(10)]],
                             constant Data& data [[buffer(11)]])
{
    VertexOut out;
    out.position = transform.uProjection * float4(in.aPos, 0.0, 1.0);

    // Proper override: if vertex color is empty (alpha == 0), use uniform color instead.
    out.vColor = (in.aColor.a != 0.0) ? in.aColor : data.uColor;

    return out;
}

// --- Fragment Shader ---

fragment float4 fragment_main(VertexOut in [[stage_in]])
{
    // Output the already-decided color from the vertex shader.
    return in.vColor;
}
