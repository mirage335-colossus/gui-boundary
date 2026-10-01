// FastLines3d -- Metal port pending.
//
// Placeholder, mirroring Lines3d.metal: the active backend in this project is
// OpenGL, so this file is never compiled. The GPU polyline triangulation +
// per-fragment depth lives in FastLines3d.vert / FastLines3d.frag. When the
// Metal backend is brought up, port those two stages here (instanced segment
// expansion in pixel space, depth written via [[depth(any)]] from the
// interpolated NDC depth). Also port the optional per-point colour path: the
// colour rides the SAME interleaved point buffer (4 texels/point: position then
// RGBA), fetched for p0/p1 and mixed by the segment parameter h in the fragment
// stage when uUseVertexColor is set -- otherwise the uniform uColor is used.
