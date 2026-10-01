#include <metal_stdlib>
using namespace metal;
struct Transform { float4x4 uProjection; };
struct Data { float4 color; float2 pos; float depth,opacity; };
struct VertexIn { float4 rect [[attribute(0)]]; float4 uv [[attribute(1)]]; };
struct VertexOut { float4 position [[position]]; float2 uv; };
vertex VertexOut vertex_main(VertexIn in [[stage_in]],constant Transform& transform [[buffer(10)]],uint vid [[vertex_id]]) {
    const float2 corners[6]={float2(0,0),float2(1,0),float2(1,1),float2(0,0),float2(1,1),float2(0,1)};
    const float2 corner=corners[vid%6];
    return {transform.uProjection*float4(in.rect.xy+in.rect.zw*corner,0.0,1.0),mix(in.uv.xy,in.uv.zw,corner)};
}
fragment float4 fragment_main(VertexOut in [[stage_in]],texture2d<float> tex [[texture(0)]],sampler texSampler [[sampler(0)]],constant Data& data [[buffer(11)]]) {
    return float4(data.color.rgb,tex.sample(texSampler,in.uv).r*data.color.a*data.opacity);
}
