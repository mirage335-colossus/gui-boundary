#version 430 core

// GPU polyline triangulation in 3D.
//
// This is FastLines (the 2D screen-space triangulator) with one thing changed:
// instead of an affine world->pixel transform, each point is projected through
// the full camera (viewProj * world * model). Everything downstream -- the
// miter joins, the corner fan, the offsets -- runs in PIXEL space exactly as in
// the 2D version, so the stroke stays a constant screen-space width regardless
// of depth or zoom, and all the join math is reused verbatim.
//
// The only addition is depth: each segment endpoint's NDC depth is forwarded to
// the fragment shader, which reconstructs a perspective-correct per-fragment
// depth (window-space depth is linear in screen space, so a plain mix by the
// SDF's own segment parameter is exact) and writes gl_FragDepth -- so a thick
// 3D line occludes and is occluded by meshes in the same View3D, per pixel,
// including across the rounded joins.
//
// One instance per segment: glDrawArraysInstanced(GL_TRIANGLES, 0, 15, N - 1).
//   verts 0-5   : the segment body quad (trapezoid), built in pixel space.
//   verts 6-14  : a 3-triangle corner fan over the join (rounded + AA'd in FS).

layout(std140, binding = 1) uniform Data {
    vec4  uColor;
    float uStrokeWidth;
    float uSmoothing;
    float uPointCount;
    float uOpacity;
    vec2  uViewport;     // logical pixels; pixel space matches gl_FragCoord under ortho
    float uUseVertexColor;  // 0 => uniform uColor, 1 => per-point colour TBO
    float _pad;
};

// Bound by View3D once per frame (binding 2), shared with Mesh3d / Lines3d.
layout(std140, binding = 2) uniform Camera {
    mat4 uViewProj;
    vec4 uLightDir;
    vec4 uEyePos;
    vec4 uLightDir2;
};

// Per-actor transform stack (binding 3): viewProj x world x model.
layout(std140, binding = 3) uniform Model {
    mat4 uWorld;
    mat4 uModel;
};

// One interleaved buffer: 4 RG32F texels per point -- position then colour.
layout(binding = 0) uniform samplerBuffer uPoints;

out vec2 v_pos;
flat out vec4 v_col0;      // per-point colour at p0 (mixed by the FS along the segment)
flat out vec4 v_col1;      // per-point colour at p1
flat out vec2 v_p0;
flat out vec2 v_p1;
flat out vec2 v_prev;     // previous point (for the corner-aware fan SDF)
flat out float v_half;
flat out float v_aa;
flat out float v_round;   // 1 => corner fan, 0 => segment body
flat out float v_ndcz0;   // NDC depth at p0 (for per-fragment depth)
flat out float v_ndcz1;   // NDC depth at p1

const float PI = 3.14159265358979;

// Point i in world space: texels 4i, 4i+1 -- (x, y) then (z, _).
vec3 fetchWorld(int i) {
    vec2 a = texelFetch(uPoints, 4 * i).xy;
    vec2 b = texelFetch(uPoints, 4 * i + 1).xy;
    return vec3(a.x, a.y, b.x);
}

// Colour for point i: texels 4i+2, 4i+3 -- (r, g) then (b, a).
vec4 fetchColor(int i) {
    vec2 a = texelFetch(uPoints, 4 * i + 2).xy;
    vec2 b = texelFetch(uPoints, 4 * i + 3).xy;
    return vec4(a.x, a.y, b.x, b.y);
}

// Clip -> pixel. All geometry is built here, so stroke width stays in pixels.
vec2 clipToPixel(vec4 clip) {
    vec2 ndc = clip.xy / clip.w;
    return (ndc * 0.5 + 0.5) * uViewport;
}

// Bisector miter offset, scaled so the offset edge sits `ext` from the
// centreline. Falls back to a butt offset for a near-hairpin.
vec2 miterOffset(vec2 a, vec2 b, vec2 nrm, float ext) {
    vec2 sum = a + b;
    if (dot(sum, sum) < 1e-4) { return nrm * ext; }
    vec2 t = normalize(sum);
    vec2 m = vec2(-t.y, t.x);
    float d = dot(m, nrm);
    if (abs(d) < 1e-3) { return nrm * ext; }
    return m * (ext / d);
}

// Clamp an offset's component along `dir` to +/- limit.
vec2 clampAlong(vec2 o, vec2 dir, float limit) {
    float a = dot(o, dir);
    return o + dir * (clamp(a, -limit, limit) - a);
}

void main() {

    int seg = gl_InstanceID;
    int N   = int(uPointCount + 0.5);

    int i0 = seg;
    int i1 = seg + 1;

    bool hasPrev = i0 > 0;
    bool hasNext = i1 < (N - 1);

    // Project the (up to) four relevant points through the camera.
    mat4 mvp = uViewProj * uWorld * uModel;

    vec4 clip0    = mvp * vec4(fetchWorld(i0), 1.0);
    vec4 clip1    = mvp * vec4(fetchWorld(i1), 1.0);
    vec4 clipPrev = hasPrev ? mvp * vec4(fetchWorld(i0 - 1), 1.0) : clip0;
    vec4 clipNext = hasNext ? mvp * vec4(fetchWorld(i1 + 1), 1.0) : clip1;

    // ... then expand the stroke in pixel space (the 2D algorithm, unchanged).
    vec2 p0   = clipToPixel(clip0);
    vec2 p1   = clipToPixel(clip1);
    vec2 prev = clipToPixel(clipPrev);
    vec2 next = clipToPixel(clipNext);

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

    // Verts on the p1 end of the body quad take p1's depth; everything else
    // (the p0 end and the whole fan) takes p0's. The fragment shader refines
    // this to a smooth per-pixel depth, but the anchor keeps gl_Position's
    // depth/clip sane.
    bool atP1 = (v == 2 || v == 4 || v == 5);
    vec4 anchorClip = atP1 ? clip1 : clip0;

    v_pos    = pos;
    v_p0     = p0;
    v_p1     = p1;
    v_prev   = prev;
    v_half   = hw;
    v_aa     = aa;
    v_round  = round;
    v_ndcz0  = clip0.z / clip0.w;
    v_ndcz1  = clip1.z / clip1.w;

    // Per-point colour (fetched even when unused; the FS gates on uUseVertexColor).
    // The corner fan sits at p0, so it inherits p0's colour, which is acceptable.
    v_col0   = fetchColor(i0);
    v_col1   = fetchColor(i1);

    // Map the pixel-space vertex back to clip, carrying the anchor's depth/w so
    // the rasterizer's perspective divide reproduces our pixel position.
    vec2 ndcOut = pos / uViewport * 2.0 - 1.0;
    gl_Position = vec4(ndcOut * anchorClip.w, anchorClip.z, anchorClip.w);
}
