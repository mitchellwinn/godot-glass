#pragma once

#include "core/object/ref_counted.h"
#include "core/variant/array.h"

// GlassMeshDecimator
//
// Native port of addons/mahou_terrain/core/mesh_decimator.gd — a two-pass mesh
// decimator (planar coplanar-merge + Garland-Heckbert QEM edge collapse) over
// committed surface arrays (triangle soup). This is the prime compute-bound
// terrain baker: the QEM kernel (per-edge quadric eval + 3x3 Cramer solve +
// flip test in a min-heap loop) is exactly the tight FPU loop that goes fast in
// native code, and the whole thing is a pure arrays-in/arrays-out kernel with
// zero game-node coupling.
//
// Faithful port: Godot's HashMap preserves insertion order, matching the
// GDScript Dictionary iteration order the heap/group passes depend on, and the
// binary heap + flood-fill are replicated operation-for-operation so the
// collapse order — and therefore the output mesh — matches the GDScript.
class GlassMeshDecimator : public RefCounted {
	GDCLASS(GlassMeshDecimator, RefCounted);

protected:
	static void _bind_methods();

public:
	// Port of decimate_surface. arrays = surface arrays (ARRAY_VERTEX/TEX_UV/
	// NORMAL). Returns decimated surface arrays (triangle soup).
	Array decimate_surface(const Array &p_arrays, bool p_use_qem, double p_error_threshold);

	GlassMeshDecimator() {}
};
