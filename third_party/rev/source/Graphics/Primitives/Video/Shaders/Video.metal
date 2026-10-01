#include <metal_stdlib>

DEFINITIONS

using namespace metal;

struct Transform { float4x4 uProjection; };
struct Data {
    float x, y, w, h;
    float u, v, uw, vh;
    float opacity;
};
struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut vertex_main(
    uint vid [[vertex_id]],
    constant Transform& transform [[buffer(10)]],
    constant Data& data [[buffer(11)]]) {

    const float2 corners[4] = {
        float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 1)
    };
    const ushort indices[6] = { 0, 1, 2, 0, 2, 3 };
    float2 corner = corners[indices[vid]];

    VertexOut out;
    out.position = transform.uProjection * float4(float2(data.x, data.y) + corner * float2(data.w, data.h), 0, 1);
    out.uv = float2(data.u, data.v) + corner * float2(data.uw, data.vh);
    return out;
}

fragment float4 fragment_main(
    VertexOut in [[stage_in]],
    constant Data& data [[buffer(11)]],
    texture2d<float> videoTexture [[texture(0)]],
    sampler videoSampler [[sampler(0)]]) {

    float4 color = videoTexture.sample(videoSampler, in.uv);
    color.a *= data.opacity;
    return color;
}
