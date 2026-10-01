#version 430 core

// Antialiased 3D stroke + per-fragment depth.
//
//   body fragments : distance to this segment (round caps fall out of the math).
//   fan fragments  : corner-aware distance = the nearer of this segment and the
//                    previous segment, so the join rounds and AAs smoothly.
//
// Depth: window-space depth is linear in screen space, so the true per-fragment
// NDC depth is just the endpoints' NDC depths interpolated by the segment
// parameter h (the same projection the SDF already computes). We write that to
// gl_FragDepth so the line occludes correctly against meshes per pixel -- at the
// cost of early-Z (deliberate; the joins/fringe need true depth to look right).

layout(std140, binding = 1) uniform Data {
    vec4  uColor;
    float uStrokeWidth;
    float uSmoothing;
    float uPointCount;
    float uOpacity;
    vec2  uViewport;
    float uUseVertexColor;  // 0 => uniform uColor, 1 => per-point colour
    float _pad;
};

in vec2 v_pos;
flat in vec2 v_p0;
flat in vec2 v_p1;
flat in vec2 v_prev;
flat in float v_half;
flat in float v_aa;
flat in float v_round;
flat in float v_ndcz0;
flat in float v_ndcz1;
flat in vec4 v_col0;
flat in vec4 v_col1;

out vec4 FragColor;

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {

    float dist = (v_round > 0.5)
        ? min(sdSegment(v_pos, v_p0, v_p1), sdSegment(v_pos, v_prev, v_p0))
        : sdSegment(v_pos, v_p0, v_p1);

    // Center the antialiasing band on the true stroke edge (v_half).
    float d = dist - v_half;
    float alpha = 1.0 - smoothstep(-0.5 * v_aa, 0.5 * v_aa, d);

    if (alpha <= 0.0) { discard; }

    // Per-fragment depth: linear in screen space, so interpolate the endpoints'
    // NDC depths by this fragment's parameter along the primary segment. Fan
    // pixels sit near p0, so h ~ 0 gives them p0's depth -- which is right.
    vec2 ba = v_p1 - v_p0;
    float h = clamp(dot(v_pos - v_p0, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    float ndcz = mix(v_ndcz0, v_ndcz1, h);
    gl_FragDepth = ndcz * 0.5 + 0.5;

    // Colour: uniform by default; per-point gradient (mixed along the segment by h)
    // when enabled. h is the same parameter the depth interpolation uses.
    vec4 col = (uUseVertexColor > 0.5) ? mix(v_col0, v_col1, h) : uColor;

    FragColor = vec4(col.rgb, col.a * alpha);

    FragColor.a *= uOpacity; // Opacity
}
