#include <metal_stdlib>

DEFINITIONS

using namespace metal;

struct Transform {
    float4x4 uProjection;
};

struct Data {
    float x, y, w, h;                           // Rect
    float r, g, b, a;                           // Fill color
    float tl, tr, bl, br;                       // Corner radii
    float b_l, b_r, b_t, b_b;                   // Border widths
    float4 l_color, r_color, t_color, b_color;  // Border colors
    float shadowX, shadowY, shadowSize, shadowBlur;
    float4 shadowColor;
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 localPos;        // position in local rect space
};

vertex VertexOut vertex_main(
    uint vid [[vertex_id]],
    constant Transform& transform [[buffer(10)]],
    constant Data& data [[buffer(11)]]
) {
    const float2 corners[4] = {
        float2(0.0, 0.0),
        float2(1.0, 0.0),
        float2(1.0, 1.0),
        float2(0.0, 1.0)
    };
    const ushort indices[6] = { 0, 1, 2, 0, 2, 3 };

    float2 cornerOffset = corners[ indices[vid] ];

    // --- Expanded rect (for rasterization) ---
    float shadowExtent = data.shadowSize + data.shadowBlur;
    float2 expandedOrigin = float2(data.x, data.y) - float2(shadowExtent);
    float2 expandedSize   = float2(data.w, data.h) + float2(shadowExtent * 2.0);
    float2 expandedPos    = expandedOrigin + cornerOffset * expandedSize;

    // Apply shadow offset globally
    expandedPos += float2(data.shadowX, data.shadowY);

    // --- Original rect center (for local coordinates) ---
    float2 rectCenter = float2(data.x + data.w * 0.5, data.y + data.h * 0.5);

    VertexOut out;
    out.position = transform.uProjection * float4(expandedPos, 0.0, 1.0);

    // Preserve the same local coordinate system as before
    out.localPos = expandedPos - rectCenter;

    out.color = float4(data.r, data.g, data.b, data.a);
    return out;
}


inline float roundedBoxSDF(float2 p, float2 halfSize, float radius) {
    float2 q = abs(p) - halfSize + float2(radius);
    return length(max(q, float2(0.0))) - radius;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              constant Data& data [[buffer(11)]])
{
    float2 halfSize = float2(data.w, data.h) * 0.5;

    // Branchless corner selection
    float isLeft   = 1.0 - step(0.0, in.localPos.x);
    float isTop    = 1.0 - step(0.0, in.localPos.y);
    float isRight  = 1.0 - isLeft;
    float isBottom = 1.0 - isTop;

    float w_tl = isLeft  * isTop;
    float w_tr = isRight * isTop;
    float w_bl = isLeft  * isBottom;
    float w_br = isRight * isBottom;

    float cornerRadius =
          w_tl * data.tl +
          w_tr * data.tr +
          w_bl * data.bl +
          w_br * data.br;

    cornerRadius = clamp(cornerRadius, 0.0, min(halfSize.x, halfSize.y));

    // Local border width
    float localBorderW =
          isLeft   * data.b_l +
          isRight  * data.b_r +
          isTop    * data.b_t +
          isBottom * data.b_b;

    // Outer and inner distances
    float distOuter = roundedBoxSDF(in.localPos, halfSize, cornerRadius);
    float2 innerHalfSize = max(halfSize - float2(localBorderW * 0.5), float2(0.0));
    float distInner = roundedBoxSDF(in.localPos, innerHalfSize,
                                    max(cornerRadius - localBorderW * 0.5, 0.0));

    float smoothingBase = 0.5 * fwidth(distOuter);

    float2 edgeDist = abs(in.localPos) - (halfSize - float2(cornerRadius));
    float fade = cornerRadius;
    float cornerFactor =
        smoothstep(-fade, 0.0, edgeDist.x) *
        smoothstep(-fade, 0.0, edgeDist.y);
    float smoothing = mix(0.0, smoothingBase, cornerFactor);

    float outerAlpha = 1.0 - smoothstep(-smoothing, smoothing, distOuter);
    float innerMask  = 1.0 - smoothstep(-smoothing, smoothing, distInner);
    float borderMask = clamp(outerAlpha - innerMask, 0.0, 1.0);

#ifdef STENCIL
    if (distInner > -smoothing) discard_fragment();
    return float4(0.0);
#else
    float4 fillColor = float4(data.r, data.g, data.b, data.a);

    float sideX = smoothstep(-halfSize.x, halfSize.x, in.localPos.x);
    float sideY = smoothstep(-halfSize.y, halfSize.y, in.localPos.y);
    float4 horizColor = mix(data.l_color, data.r_color, sideX);
    float4 vertColor  = mix(data.t_color, data.b_color, sideY);
    float4 borderColor = mix(horizColor, vertColor, 0.5);

    float4 shapeColor = mix(fillColor, borderColor, borderMask);
    float shapeAlpha = max(outerAlpha, borderMask);

    // --- Shadow ---
    float2 shadowPos = in.localPos - float2(data.shadowX, data.shadowY);
    float shadowDist = roundedBoxSDF(
        shadowPos,
        halfSize + float2(data.shadowSize),
        cornerRadius + data.shadowSize
    );

    float rawShadowAlpha = 1.0 - smoothstep(0.0, data.shadowBlur, shadowDist);
    float blurAtt = 1.0 / (1.0 + data.shadowBlur * 0.05);
    blurAtt = mix(1.0, blurAtt, clamp(data.shadowBlur / 50.0, 0.0, 1.0));

    float shadowAlpha = rawShadowAlpha * blurAtt * (1.0 - shapeAlpha);

    float finalAlpha = clamp(shapeAlpha + shadowAlpha, 0.0, 1.0);
    float shadowWeight = shadowAlpha / max(finalAlpha, 1e-5);
    float shapeWeight  = 1.0 - shadowWeight;

    float4 finalRGBA = data.shadowColor * shadowWeight + shapeColor * shapeWeight;
    return float4(finalRGBA.rgb, finalRGBA.a * finalAlpha);
#endif
}
