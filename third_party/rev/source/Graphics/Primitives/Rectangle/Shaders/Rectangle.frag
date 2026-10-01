#version 430 core

DEFINITIONS

in vec2 fragLocalPos;
in vec4 cornerMask;
in vec4 sideMask;

out vec4 FragColor;

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;                                   // Rect
    vec4 fillColor;                                     // Fill color
    float tl, tr, bl, br;                               // Corner radii
    float l_width, r_width, t_width, b_width;           // Border widths
    vec4 l_color, r_color, t_color, b_color;            // Border colors
    float shadowX, shadowY, shadowSize, shadowBlur;
    vec4 shadowColor;
    float opacity;
};

vec4 softMax(vec4 v, float sharpness) {

    // Stabilize by subtracting the maximum value
    float M = max(max(v.x, v.y), max(v.z, v.w));

    // Apply exponent with sharpness
    vec4 e = exp((v - vec4(M)) * sharpness);

    // Sum of all exponentiated values
    float s = e.x + e.y + e.z + e.w;

    // Normalize
    return e / max(s, 1e-6);
}

vec4 hardMax4(vec4 v) {
    float M = max(max(v.x, v.y), max(v.z, v.w));
    return vec4(
        v.x == M ? 1.0 : 0.0,
        v.y == M ? 1.0 : 0.0,
        v.z == M ? 1.0 : 0.0,
        v.w == M ? 1.0 : 0.0
    );
}

float radiusSharpness(float radius) {
    return mix(24.0, 4.0, clamp(radius / 24.0, 0.0, 1.0));
}

float roundedBoxSDF(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + vec2(radius);
    return length(max(q, 0.0)) - radius;
}

void main() {

    // Compute basic dimensions
    vec2 rectCenter = vec2(x + w * 0.5, y + h * 0.5);
    vec2 localPos = fragLocalPos - rectCenter;

    // Choose side and corner
    //--------------------------------------------------

    vec4 mCorner = softMax(cornerMask, 1.0f);
    vec4 mSide   = softMax(sideMask, 1024.0f);

    // Choose corner radius, border width, and color
    float cornerRadius = mCorner.x * tl + mCorner.y * tr + mCorner.z * bl + mCorner.w * br;
    float borderWidth = mSide.x * l_width + mSide.y * r_width + mSide.z * t_width + mSide.w * b_width;
    
    // Calculate outer and inner half-size (accounting for border width)
    vec2 outerHalfSize = vec2(w, h) * 0.5;
    vec2 innerHalfSize = max(outerHalfSize - vec2(borderWidth), vec2(0.0));

    // Choose corner radius, calc inner and outer based on size and border width
    float outerRadius = clamp(cornerRadius, 0.0, min(outerHalfSize.x, outerHalfSize.y));
    float innerRadius = max(outerRadius - borderWidth, 0.0);

    // Compute box SDF with borders
    //--------------------------------------------------

    // Signed distance to inner (inside border) and outer (on border) boxes
    float distInner = roundedBoxSDF(localPos, innerHalfSize, innerRadius);
    float distOuter = roundedBoxSDF(localPos, outerHalfSize, outerRadius);

    //--------------------------------------------------
    // Unified smoothing
    //--------------------------------------------------

    // Base smoothing from SDF derivatives
    float baseSmooth = fwidth(distOuter) * 0.5;

    // Distance to nearest corner region
    vec2 cornerProbe = abs(localPos) - (outerHalfSize - vec2(outerRadius));

    // We only fade when actually inside the rounded corner region
    float cornerFade = smoothstep(-outerRadius, 0.0, max(cornerProbe.x, cornerProbe.y));

    // Final smoothing: 0 on straight sides, full on corners
    float smoothing = mix(0.0, baseSmooth, cornerFade);

    // Hard-edge masks
    float outerMask = 1.0 - smoothstep(-smoothing, smoothing, distOuter);
    float innerMask = 1.0 - smoothstep(-smoothing, smoothing, distInner);

    // Border = outer region minus inner region
    float borderMask = outerMask * (1.0 - innerMask);
    float fillMask   = innerMask;

    // If stencil mode is activated, we immediately discard if we're inside the inner mask
    #ifdef STENCIL

        if (innerMask == 0.0) { discard; }
        FragColor = vec4(0,0,0,0);

        return;
    #else

    // Shadow
    //--------------------------------------------------

    float shadowEnabled = step(0.001, shadowColor.a);

    vec2 shadowPos = localPos - vec2(shadowX, shadowY);
    vec2 shadowHalfSize = outerHalfSize + vec2(shadowSize);
    float shadowRadius = cornerRadius + shadowSize;

    // Signed distance field for the shadow's shape (expanded)
    float shadowDist = roundedBoxSDF(shadowPos, shadowHalfSize, shadowRadius);

    // Raw blur falloff
    float rawShadowAlpha = 1.0 - smoothstep(0.0, shadowBlur, shadowDist);

    // Scale shadow opacity inversely with blur radius
    // Ensures that large blurs appear softer, not darker
    float blurAttenuation = 1.0 / (1.0 + shadowBlur * 0.05);

    // Optionally, make attenuation weaker for small shadows (more perceptual)
    blurAttenuation = mix(1.0, blurAttenuation, clamp(shadowBlur / 50.0, 0.0, 1.0));

    // Combine
    //--------------------------------------------------

    vec4 borderColor = mSide.x * l_color + mSide.y * r_color + mSide.z * t_color + mSide.w * b_color;

    // Premultiplied fill color
    float fillAlpha = fillMask * fillColor.a;
    vec3  fillRGB      = fillColor.rgb * fillAlpha;

    // Premultiplied border color
    float borderAlpha = borderMask * borderColor.a;
    vec3  borderRGB      = borderColor.rgb * borderAlpha;

    // Premultiplied shape color
    float shapeAlpha = fillAlpha + borderAlpha;
    vec3  shapeRGB   = fillRGB + borderRGB;

    // Shadow
    float shadowAlpha    = rawShadowAlpha * blurAttenuation * (1.0 - outerMask) * shadowColor.a;
    vec3  shadowRGB      = shadowColor.rgb * shadowAlpha;

    // Final color
    float finalAlpha = shapeAlpha + shadowAlpha;
    vec3  finalRGB   = shapeRGB + shadowRGB;

    // Convert premultiplied → straight alpha for output
    FragColor = vec4(finalRGB / max(finalAlpha, 1e-5), finalAlpha);

    FragColor.a *= opacity; // Opacity

    #endif
}
