#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"
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

	GlassTerrainMeshBuilder() {}
};
