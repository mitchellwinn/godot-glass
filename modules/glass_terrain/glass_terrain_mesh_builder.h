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

	// Texture-key + curve + noise helpers (ports of the GDScript statics).
	static String _derive_grass_path(const String &p_tile_path, const String &p_suffix);
	static bool _island_has_grass(const Dictionary &p_island);
	static String _grass_key(const Dictionary &p_island);
	static String _cliff_key(const Dictionary &p_island);
	static String _overhang_key(const Dictionary &p_island);
	static String _ground_key(const Dictionary &p_island);
	static double _evaluate_profile_curve(const PackedVector2Array &p_curve, double p_t);
	static double _deterministic_noise(int64_t p_hash);

	// Quad emitters (ports of _add_quad / _add_quad_wall_uv / _add_quad_world_uv).
	static void _add_quad(const Ref<SurfaceTool> &p_st, const PackedVector3Array &p_corners, const Rect2 &p_uv_rect);
	static void _add_quad_wall_uv(const Ref<SurfaceTool> &p_st, const PackedVector3Array &p_corners, const Vector2 &p_face_normal_xz, double p_uv_repeat_u, double p_uv_repeat_v);
	static void _add_quad_world_uv(const Ref<SurfaceTool> &p_st, const PackedVector3Array &p_corners, double p_tws);

	// Slope helpers (ports of the GDScript statics).
	static Dictionary _make_default_face_settings();
	static int _get_slope_subdivisions(const Dictionary &p_island);
	static PackedVector2Array _get_slope_curve(const Dictionary &p_island);
	static Vector2 _resolve_bottom_pos(const Dictionary &p_island, int p_vert_idx, const Vector2 &p_top_pos, const Vector2 &p_outward_dir, double p_slope_depth);
	static double _evaluate_slope_height_factor(const PackedVector2Array &p_curve, double p_t, bool p_reverse_t);
	static Dictionary _resolve_face_settings(const Dictionary &p_island, int p_edge_idx);
	static int _find_slope_target_elevation(const Vector2 &p_a, const Vector2 &p_b, const Vector2 &p_outward, double p_slope_depth, int p_source_elev, const Array &p_all_islands);

	// Polygon helpers (ports of PolygonOps).
	static double _polygon_area(const PackedVector2Array &p_poly);
	static PackedVector2Array _ensure_ccw(PackedVector2Array p_poly);
	// slope_in ground cut-out footprints.
	TypedArray<PackedVector2Array> _compute_slope_in_clips(const Dictionary &p_island, const PackedVector2Array &p_verts, const PackedInt32Array &p_edge_types, int p_elev, double p_tws, const Array &p_all_islands) const;
	PackedVector2Array _build_slope_in_clip_for_run(const PackedVector2Array &p_verts, const PackedInt32Array &p_run, double p_slope_depth, int p_source_elev, const Array &p_all_islands, const Dictionary &p_island) const;
	// Append src surface arrays onto dst (port of _append_surface_arrays).
	static void _append_surface_arrays(Array &r_dst, const Array &p_src);

	// Slope-side leaf builders (live callees of the merged-slope builders).
	void _build_slope_side_wall(const Dictionary &p_island, const Vector2 &p_top_pt, const Vector2 &p_outward_dir,
			double p_slope_depth, double p_y_top, double p_y_bottom, int p_slope_subdivisions,
			const PackedVector2Array &p_slope_curve, double p_tws, double p_level_h, bool p_flip_winding, bool p_reverse_curve_t);
	void _build_slope_side_overhang(const Dictionary &p_island, const Vector2 &p_top_pt, const Vector2 &p_bottom_pt,
			double p_y_top, double p_tws, bool p_flip_winding, double p_y_bottom, int p_slope_subdivisions,
			const PackedVector2Array &p_slope_curve, bool p_reverse_curve_t);

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

	// Port of _build_cliff_face: subdivided cliff wall (profile curve + rockiness
	// noise + rock fade) plus the grass overhang collar at the top.
	void build_cliff_face(const Dictionary &p_island, const Vector2 &p_a, const Vector2 &p_b,
			double p_y_top, int p_elev, double p_level_h, double p_tws, int p_edge_idx,
			const Dictionary &p_face_cfg, int p_vert_count, const Vector2 &p_perp_at_a,
			const Vector2 &p_perp_at_b, bool p_flush_at_a, bool p_flush_at_b, bool p_build_overhang);

	// Ports of _build_merged_slope / _build_merged_slope_in: the ramp surface plus
	// its side walls, side overhangs, and the cliff below the slope bottom.
	void build_merged_slope(const Dictionary &p_island, const PackedVector2Array &p_verts,
			const PackedInt32Array &p_edge_types, const PackedInt32Array &p_run, double p_y_top,
			int p_elev, double p_level_h, double p_tws, const Array &p_all_islands, const PackedVector2Array &p_vertex_perps);
	void build_merged_slope_in(const Dictionary &p_island, const PackedVector2Array &p_verts,
			const PackedInt32Array &p_edge_types, const PackedInt32Array &p_run, double p_y_top,
			int p_elev, double p_level_h, double p_tws, const Array &p_all_islands, const PackedVector2Array &p_vertex_perps);

	// Port of _build_edge_geometry: cliff faces + merged slope/slope_in runs.
	void build_edge_geometry(const Dictionary &p_island, const PackedVector2Array &p_verts,
			const PackedInt32Array &p_edge_types, int p_elev, double p_level_h, double p_tws,
			const Array &p_all_islands, int p_island_idx);

	// Port of _build_cut_geometry: lowered ground + inward cliff walls of a cut.
	void build_cut_geometry(const Dictionary &p_island, const PackedVector2Array &p_cut_verts,
			int p_parent_elev, int p_depth, double p_level_h, double p_tws);

	// Port of build_single_island_surfaces: clips cuts + slope_in footprints,
	// builds ground/edge/cut geometry, returns { tex_key: arrays }.
	Dictionary build_island_surfaces(const Dictionary &p_island, const Array &p_cuts, int p_island_idx,
			const Array &p_all_islands, double p_tws, double p_level_h);

	// Port of merge_surface_caches: { "mesh": ArrayMesh, "surface_textures": PackedStringArray }.
	Dictionary merge_caches(const Array &p_island_caches);

	// Port of _extract_walkable_triangles_for_island (collision kernel): from a
	// per-island surface cache { tex_key: arrays }, keep triangles whose face
	// normal |n.y| > threshold (walkable), skipping "_overhang" surfaces. Returns
	// a triangle-soup PackedVector3Array for ConcavePolygonShape3D::set_faces.
	PackedVector3Array extract_walkable_triangles(const Dictionary &p_island_cache, double p_slope_y_threshold) const;

	GlassTerrainMeshBuilder() {}
};
