#version 430 core

// Antialiased stroke.
//   body fragments : distance to this segment (round caps fall out of the math).
//   fan fragments  : corner-aware distance = the nearer of this segment and the
//                    previous segment. Their capsules' union is exactly the join
//                    coverage, so the corner rounds (around the shared point p0)
//                    and antialiases smoothly instead of filling a crunchy bevel.

layout(std140, binding = 1) uniform Data {
    vec4  uColor;
    float uStrokeWidth;
    float uSmoothing;
    float uPointCount;
    float uOpacity;
};

in vec2 v_pos;
flat in vec2 v_p0;
flat in vec2 v_p1;
flat in vec2 v_prev;
flat in float v_half;
flat in float v_aa;
flat in float v_round;

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

    // Center the antialiasing band on the true stroke edge (v_half) rather than
    // adding the whole feather outside it. Otherwise a thin stroke blooms by ~v_aa
    // on each side and a 1px line reads as 2-3px. Now strokeWidth is the real width.
    float d = dist - v_half;
    float alpha = 1.0 - smoothstep(-0.5 * v_aa, 0.5 * v_aa, d);

    if (alpha <= 0.0) { discard; }

    FragColor = vec4(uColor.rgb, uColor.a * alpha);

    FragColor.a *= uOpacity; // Opacity
}
