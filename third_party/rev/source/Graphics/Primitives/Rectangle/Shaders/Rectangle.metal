#include <metal_stdlib>

DEFINITIONS

using namespace metal;

struct Transform {
    float4x4 uProjection;
};

struct Data {
    float x, y, w, h;                                   // Rect
    float4 fillColor;                                    // Fill color
    float tl, tr, bl, br;                               // Corner radii
    float l_width, r_width, t_width, b_width;           // Border widths
    float4 l_color, r_color, t_color, b_color;            // Border colors
    float shadowX, shadowY, shadowSize, shadowBlur;
    float4 shadowColor;
};

struct VertexOut {
    float4 position [[position]];
    float2 localPos;

    float4 color;

    float4 cornerMask;
    float4 sideMask;
};

vertex VertexOut vertex_main(
    uint vid [[vertex_id]],
    constant Transform& transform [[buffer(10)]],
    constant Data& data [[buffer(11)]]
) {
    // ---------------------------------------------
    // GLSL lookup tables (Metal version)
    // ---------------------------------------------

    const float2 offsets[4] = {
        float2(0.0, 0.0),
        float2(1.0, 0.0),
        float2(1.0, 1.0),
        float2(0.0, 1.0)
    };

    const float4 sideMasks[4] = {
        float4(1,0,1,0),  // left + top
        float4(0,1,1,0),  // right + top
        float4(0,1,0,1),  // right + bottom
        float4(1,0,0,1)   // left + bottom
    };

    const float4 cornerMasks[4] = {
        float4(1,0,0,0),  // TL
        float4(0,1,0,0),  // TR
        float4(0,0,1,0),  // BL
        float4(0,0,0,1)   // BR
    };

    // ---------------------------------------------
    // Matching GLSL: vertex index is vid % 4
    // ---------------------------------------------

    const uint indices[6] = { 0, 1, 2, 0, 2, 3 };
    uint idx = indices[vid];
    

    float2 cornerOffset = offsets[idx];

    VertexOut out;
    out.cornerMask = cornerMasks[idx];
    out.sideMask   = sideMasks[idx];
    out.color      = data.fillColor;

    // ---------------------------------------------
    // Expanded rect for shadow + rasterization
    // ---------------------------------------------
    float shadowExtent = max(data.shadowSize + data.shadowBlur, 0.0f);

    float2 expandedOrigin =
        float2(data.x, data.y) - float2(shadowExtent);

    float2 expandedSize =
        float2(data.w, data.h) + float2(shadowExtent * 2.0f);

    float2 expandedPos =
        float2(data.shadowX, data.shadowY)
        + expandedOrigin
        + cornerOffset * expandedSize;

    // ---------------------------------------------
    // Preserve local rect-space coordinates
    // (same definition as GLSL: fragLocalPos = expandedPos)
    // ---------------------------------------------
    out.localPos = expandedPos;

    // ---------------------------------------------
    // Final projection
    // ---------------------------------------------
    out.position = transform.uProjection * float4(expandedPos, 0.0, 1.0);

    return out;
}


// ------------------------------------------------------------
// IDENTICAL softMax()
// ------------------------------------------------------------
inline float4 softMax(float4 v, float sharpness)
{
    float M = max(max(v.x, v.y), max(v.z, v.w));
    float4 e = exp((v - float4(M)) * sharpness);
    float s = e.x + e.y + e.z + e.w;
    return e / max(s, 1e-6);
}

// ------------------------------------------------------------
// IDENTICAL roundedBoxSDF()
// ------------------------------------------------------------
inline float roundedBoxSDF(float2 p, float2 halfSize, float radius)
{
    float2 q = abs(p) - halfSize + float2(radius);
    return length(max(q, float2(0.0))) - radius;
}

// ------------------------------------------------------------
// FRAGMENT SHADER — EXACT GLSL LOGIC
// ------------------------------------------------------------
fragment float4 fragment_main(VertexOut in              [[stage_in]],
                              constant Data& data       [[buffer(11)]])
{
    // --------------------------------------------------------
    // Compute basic dimensions
    // --------------------------------------------------------
    float2 rectCenter = float2(data.x + data.w * 0.5, data.y + data.h * 0.5);
    float2 localPos = in.localPos - rectCenter;

    // --------------------------------------------------------
    // Side & corner masking
    // --------------------------------------------------------
    float4 mCorner = softMax(in.cornerMask, 1.0f);

    float maxSide = max(max(in.sideMask.x, in.sideMask.y),
                        max(in.sideMask.z, in.sideMask.w));
    float4 mSide = step(maxSide - 0.0001f, in.sideMask);

    // --------------------------------------------------------
    // Choose corner radius, border width, color
    // --------------------------------------------------------
    float cornerRadius =
          mCorner.x * data.tl +
          mCorner.y * data.tr +
          mCorner.z * data.bl +
          mCorner.w * data.br;

    float borderWidth =
          mSide.x * data.l_width +
          mSide.y * data.r_width +
          mSide.z * data.t_width +
          mSide.w * data.b_width;

    // --------------------------------------------------------
    // Outer/inner rect sizes
    // --------------------------------------------------------
    float2 outerHalfSize = float2(data.w, data.h) * 0.5;
    float2 innerHalfSize = max(outerHalfSize - float2(borderWidth), float2(0.0));

    float outerRadius = clamp(cornerRadius, 0.0f,
                              min(outerHalfSize.x, outerHalfSize.y));
    float innerRadius = max(outerRadius - borderWidth, 0.0f);

    // --------------------------------------------------------
    // SDF distances
    // --------------------------------------------------------
    float distInner = roundedBoxSDF(localPos, innerHalfSize, innerRadius);
    float distOuter = roundedBoxSDF(localPos, outerHalfSize, outerRadius);

    // --------------------------------------------------------
    // Unified smoothing
    // --------------------------------------------------------
    float baseSmooth = 0.5 * fwidth(distOuter);

    float2 cornerProbe = abs(localPos) - (outerHalfSize - float2(outerRadius));
    float cornerFade = smoothstep(-outerRadius, 0.0f, max(cornerProbe.x, cornerProbe.y));

    float smoothing = mix(0.0f, baseSmooth, cornerFade);

    float outerMask = 1.0 - smoothstep(-smoothing,  smoothing, distOuter);
    float innerMask = 1.0 - smoothstep(-smoothing,  smoothing, distInner);

    float borderMask = outerMask * (1.0 - innerMask);
    float fillMask   = innerMask;

    // --------------------------------------------------------
    // STENCIL MODE (identical behavior)
    // --------------------------------------------------------
    #ifdef STENCIL
        if (innerMask == 0.0) discard_fragment();
        return float4(0,0,0,0);
    #else

    // --------------------------------------------------------
    // SHADOW
    // --------------------------------------------------------
    float shadowEnabled = step(0.001f, data.shadowColor.a);

    float2 shadowPos = localPos - float2(data.shadowX, data.shadowY);
    float2 shadowHalfSize = outerHalfSize + float2(data.shadowSize);
    float shadowRadius = cornerRadius + data.shadowSize;

    float shadowDist = roundedBoxSDF(shadowPos, shadowHalfSize, shadowRadius);

    float rawShadowAlpha = 1.0 - smoothstep(0.0, data.shadowBlur, shadowDist);

    float blurAtt = 1.0 / (1.0 + data.shadowBlur * 0.05f);
    blurAtt = mix(1.0, blurAtt, clamp(data.shadowBlur / 50.0f, 0.0f, 1.0f));

    // --------------------------------------------------------
    // COLOR COMPOSITION
    // --------------------------------------------------------
    float4 borderColor =
        mSide.x * data.l_color +
        mSide.y * data.r_color +
        mSide.z * data.t_color +
        mSide.w * data.b_color;

    // Premultiplied
    float fillAlpha = fillMask * data.fillColor.a;
    float3 fillRGB  = data.fillColor.rgb * fillAlpha;

    float borderAlpha = borderMask * borderColor.a;
    float3 borderRGB  = borderColor.rgb * borderAlpha;

    float shapeAlpha = fillAlpha + borderAlpha;
    float3 shapeRGB  = fillRGB + borderRGB;

    // Shadow
    float shadowAlpha = rawShadowAlpha *
                        blurAtt *
                        (1.0 - outerMask) *
                        data.shadowColor.a;

    float3 shadowRGB = data.shadowColor.rgb * shadowAlpha;

    // --------------------------------------------------------
    // Final premultiplied → straight alpha
    // --------------------------------------------------------
    float finalAlpha = shapeAlpha + shadowAlpha;
    float3 finalRGB  = shapeRGB  + shadowRGB;

    return float4(finalRGB / max(finalAlpha, 1e-5f), finalAlpha);

    #endif
}