#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "scene/resources/surface_tool.h"

// GlassTerrainMeshBuilder
//
// Native port of addons/mahou_terrain/core/terrain_mesh_builder.gd. Builds an
// island's textured surfaces (ground top, cliff/slope edges, cut depressions)
// into per-texture SurfaceTools, then commits them to mesh arrays.
//
// Design: the builder is fed island geometry/config explicitly (it does NOT
// reach back into the terrain Node), so it is pure and unit-testable. GDScript
// keeps data marshalling, texture loading and scene assembly; this owns the
// hot geometry compute.
//
// Ported incrementally, parity-tested per slice against the GDScript original:
//   Slice 2  (this): scaffold + ground surface (triangulate + UV) + commit/merge
//   later: cliff faces, merged slopes/slope-in, overhangs, cut geometry
class GlassTerrainMeshBuilder : public RefCounted {
	GDCLASS(GlassTerrainMeshBuilder, RefCounted);

	// tex_key -> SurfaceTool (insertion order preserved, mirroring the GDScript
	// Dictionary so committed surface order matches).
	HashMap<String, Ref<SurfaceTool>> surfaces;
	List<String> surface_order;

	Ref<SurfaceTool> _ensure_surface(const String &p_tex_key);

protected:
	static void _bind_methods();

public:
	// Reset accumulated surfaces (call before building an island).
	void begin();

	// Port of _build_ground_surface: triangulate the polygon, world-space UVs
	// (uv = world_pos / uv_scale; uv_scale<=0 falls back to tws).
	void build_ground_surface(const PackedVector2Array &p_poly, double p_y_height,
			double p_tws, const String &p_tex_key, double p_uv_scale);

	// Port of the commit loop in build_single_island_surfaces: generate
	// normals+tangents, commit each surface to arrays, drop empties.
	// Returns { tex_key: arrays }.
	Dictionary commit();

	// ---- Edge-run geometry helpers (pure; ported 1:1 from the GDScript) ----
	// EDGE_CLIFF=0, EDGE_SLOPE=1, EDGE_FLAT=2, EDGE_SLOPE_IN=3 (TerrainData).
	// Grouped consecutive edges of target_type into runs (with wraparound merge).
	TypedArray<PackedInt32Array> collect_edge_runs(const PackedInt32Array &p_edge_types, int p_vert_count, int p_target_type) const;
	PackedVector2Array build_run_top_polyline(const PackedVector2Array &p_verts, const PackedInt32Array &p_run, int p_vert_count) const;
	PackedVector2Array build_run_edge_perps(const PackedVector2Array &p_verts, const PackedInt32Array &p_run, int p_vert_count) const;
	Vector2 edge_perp_at(const PackedVector2Array &p_verts, int p_edge_idx, int p_vert_count) const;
	PackedVector2Array compute_run_seam_dirs(const PackedVector2Array &p_verts, const PackedInt32Array &p_run, const PackedVector2Array &p_edge_perps, int p_vert_count, bool p_inward) const;
	PackedVector2Array build_run_offset_dirs(const PackedVector2Array &p_edge_perps, bool p_inward) const;
	// Returns [dense_poly: PackedVector2Array, params: PackedFloat32Array].
	Array densify_run_polyline(const PackedVector2Array &p_sparse_poly, double p_tws) const;
	PackedVector2Array bow_run_polyline(const PackedVector2Array &p_dense_top, const PackedFloat32Array &p_params, double p_slope_depth, bool p_inward) const;

	GlassTerrainMeshBuilder() {}
};
