#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

// GlassHeightSampler
//
// Native port of the mahou_terrain height-sampling hot loop
// (autotile_mesh_builder.gd: _build_face_grid / _build_face_grid_bounded /
// _sample_height PRIMARY path). This is the bottleneck the autotile/road/fence
// bakes hammer millions of times.
//
// Usage from GDScript (drop-in for the GDScript face-grid + _sample_height):
//   var s = GlassHeightSampler.new()
//   s.build(terrain_mesh.get_faces(), grid_cs)         # or build_bounded(...)
//   var h = s.sample(Vector2(x, z))                     # -1e18 == no triangle hit
//
// On a miss sample() returns NO_HIT (-1e18); the caller falls back to the
// GDScript polygon lookup exactly as before (it checks `h > -1e17`).
class GlassHeightSampler : public RefCounted {
	GDCLASS(GlassHeightSampler, RefCounted);

	PackedVector3Array faces;
	// Cell key (packed cx,cz) -> list of face start indices (multiples of 3).
	HashMap<int64_t, Vector<int>> grid;
	double grid_cs = 1.0;

	static _FORCE_INLINE_ int64_t _cell_key(int p_cx, int p_cz) {
		return (static_cast<int64_t>(p_cx) << 32) ^ (static_cast<uint32_t>(p_cz));
	}

protected:
	static void _bind_methods();

public:
	static const double NO_HIT;

	void build(const PackedVector3Array &p_faces, double p_cell_size);
	void build_bounded(const PackedVector3Array &p_faces, double p_cell_size, const Rect2 &p_bounds);
	double sample(const Vector2 &p_xz) const;
	PackedFloat64Array sample_many(const PackedVector2Array &p_points) const;
	int face_count() const { return faces.size() / 3; }

	GlassHeightSampler() {}
};
