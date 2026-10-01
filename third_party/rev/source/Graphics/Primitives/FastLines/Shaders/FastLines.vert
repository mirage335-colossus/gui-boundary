#version 430 core

// GPU polyline triangulation.
//
// One instance per segment: glDrawArraysInstanced(GL_TRIANGLES, 0, 15, N - 1).
//   verts 0-5   : the segment body quad (trapezoid). Inner/concave side is an
//                 unclamped* miter shared with the neighbour (full width, tiles,
//                 no overdraw); outer/convex side is a butt corner.
//   verts 6-14  : a 3-triangle corner fan over the join, pivoting at the inner
//                 intersection M and fanning across an arc of outer points. The
//                 fragment shades it with a corner-aware SDF (union of the two
//                 segment capsules) so it rounds smoothly and antialiases.
//
// *The inner miter is clamped so it cannot shoot past either segment's far end
//  (otherwise hyper-acute angles squirt a jagged offshoot out of the corner).

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

layout(std140, binding = 1) uniform Data {
    vec4  uColor;
    float uStrokeWidth;
    float uSmoothing;
    float uPointCount;
    float uOpacity;
};

layout(binding = 0) uniform samplerBuffer uPoints;

// World -> pixel transform, applied to the points before geometry generation.
//   pixel = world * uXform.xy + uXform.zw
layout(std140, binding = 2) uniform Xform {
    vec4 uXform;
};

out vec2 v_pos;
flat out vec2 v_p0;
flat out vec2 v_p1;
flat out vec2 v_prev;    // previous point (for the corner-aware fan SDF)
flat out float v_half;
flat out float v_aa;
flat out float v_round;  // 1 => corner fan, 0 => segment body

const float PI = 3.14159265358979;

// Fetch a point and map it world -> pixel. All downstream geometry (offsets,
// miters, fan) is therefore built in pixel space, so the stroke stays a constant
// pixel width no matter the zoom.
vec2 fetch(int i) {
    vec2 w = texelFetch(uPoints, i).xy;
    return w * uXform.xy + uXform.zw;
}

// Bisector miter offset, scaled so the offset edge sits `ext` from the
// centreline (perpendicular component == ext, so joins never thin). Falls back
// to a butt offset for a near-hairpin where the bisector is undefined.
vec2 miterOffset(vec2 a, vec2 b, vec2 nrm, float ext) {
    vec2 sum = a + b;
    if (dot(sum, sum) < 1e-4) { return nrm * ext; }
    vec2 t = normalize(sum);
    vec2 m = vec2(-t.y, t.x);
    float d = dot(m, nrm);
    if (abs(d) < 1e-3) { return nrm * ext; }
    return m * (ext / d);
}

// Clamp an offset's component along `dir` to +/- limit, so the mitered vertex
// cannot extend past the segment end that produced it.
vec2 clampAlong(vec2 o, vec2 dir, float limit) {
    float a = dot(o, dir);
    return o + dir * (clamp(a, -limit, limit) - a);
}

void main() {

    int seg = gl_InstanceID;
    int N   = int(uPointCount + 0.5);

    int i0 = seg;
    int i1 = seg + 1;

    vec2 p0 = fetch(i0);
    vec2 p1 = fetch(i1);

    bool hasPrev = i0 > 0;
    bool hasNext = i1 < (N - 1);

    vec2 prev = hasPrev ? fetch(i0 - 1) : p0;
    vec2 next = hasNext ? fetch(i1 + 1) : p1;

    vec2 segVec = p1 - p0;
    float segLen = length(segVec);
    vec2 segDir = (segLen > 1e-6) ? segVec / segLen : vec2(1.0, 0.0);
    vec2 nrm = vec2(-segDir.y, segDir.x);

    float prevLen = hasPrev ? length(p0 - prev) : 0.0;
    float nextLen = hasNext ? length(next - p1) : 0.0;

    float aa  = uSmoothing + 1.0;
    float hw  = 0.5 * uStrokeWidth;
    float ext = hw + aa;

    vec2 dirIn  = hasPrev ? normalize(p0 - prev) : segDir;
    vec2 dirOut = hasNext ? normalize(next - p1) : segDir;

    // --- body quad corners (c?p on +nrm side, c?m on -nrm side) ---

    vec2 c0p, c0m;
    if (!hasPrev) {
        c0p = p0 + nrm * ext - segDir * ext;
        c0m = p0 - nrm * ext - segDir * ext;
    } else {
        float cross0 = dirIn.x * segDir.y - dirIn.y * segDir.x;
        vec2 o = miterOffset(dirIn, segDir, nrm, ext);
        o = clampAlong(o, segDir, min(segLen, prevLen));
        if (cross0 > 0.0) { c0p = p0 + o;         c0m = p0 - nrm * ext; }
        else              { c0p = p0 + nrm * ext; c0m = p0 - o; }
    }

    vec2 c1p, c1m;
    if (!hasNext) {
        c1p = p1 + nrm * ext + segDir * ext;
        c1m = p1 - nrm * ext + segDir * ext;
    } else {
        float cross1 = segDir.x * dirOut.y - segDir.y * dirOut.x;
        vec2 o = miterOffset(segDir, dirOut, nrm, ext);
        o = clampAlong(o, segDir, min(segLen, nextLen));
        if (cross1 > 0.0) { c1p = p1 + o;         c1m = p1 - nrm * ext; }
        else              { c1p = p1 + nrm * ext; c1m = p1 - o; }
    }

    // --- corner fan at p0 (3 triangles over an arc; owned by this segment) ---

    vec2 pivot = p0;
    vec2 a0 = p0, a1 = p0, a2 = p0, a3 = p0;   // outer arc points
    if (hasPrev) {
        float cross0 = dirIn.x * segDir.y - dirIn.y * segDir.x;
        vec2 o = miterOffset(dirIn, segDir, nrm, ext);
        o = clampAlong(o, segDir, min(segLen, prevLen));
        pivot = (cross0 > 0.0) ? (p0 + o) : (p0 - o);   // inner intersection M

        float os = (cross0 >= 0.0) ? -1.0 : 1.0;        // outer side
        vec2 nrmPrev = vec2(-dirIn.y, dirIn.x);
        vec2 thisOuter = p0 + nrm * (ext * os);
        vec2 prevOuter = p0 + nrmPrev * (ext * os);

        // Sweep the short arc (radius ext, centred on p0) from this segment's
        // outer butt to the previous segment's outer butt.
        float ang0 = atan(thisOuter.y - p0.y, thisOuter.x - p0.x);
        float ang1 = atan(prevOuter.y - p0.y, prevOuter.x - p0.x);
        float da = ang1 - ang0;
        if (da >  PI) { da -= 2.0 * PI; }
        if (da < -PI) { da += 2.0 * PI; }

        a0 = thisOuter;
        a1 = p0 + ext * vec2(cos(ang0 + da * (1.0 / 3.0)), sin(ang0 + da * (1.0 / 3.0)));
        a2 = p0 + ext * vec2(cos(ang0 + da * (2.0 / 3.0)), sin(ang0 + da * (2.0 / 3.0)));
        a3 = prevOuter;
    }

    int v = gl_VertexID;
    vec2 pos = p0;
    float round = 0.0;

    if      (v == 0) { pos = c0p; }
    else if (v == 1) { pos = c0m; }
    else if (v == 2) { pos = c1m; }
    else if (v == 3) { pos = c0p; }
    else if (v == 4) { pos = c1m; }
    else if (v == 5) { pos = c1p; }
    else if (v == 6)  { pos = pivot; round = 1.0; }
    else if (v == 7)  { pos = a0;    round = 1.0; }
    else if (v == 8)  { pos = a1;    round = 1.0; }
    else if (v == 9)  { pos = pivot; round = 1.0; }
    else if (v == 10) { pos = a1;    round = 1.0; }
    else if (v == 11) { pos = a2;    round = 1.0; }
    else if (v == 12) { pos = pivot; round = 1.0; }
    else if (v == 13) { pos = a2;    round = 1.0; }
    else              { pos = a3;    round = 1.0; }

    v_pos   = pos;
    v_p0    = p0;
    v_p1    = p1;
    v_prev  = prev;
    v_half  = hw;
    v_aa    = aa;
    v_round = round;

    gl_Position = uProjection * vec4(pos, 0.0, 1.0);
}
