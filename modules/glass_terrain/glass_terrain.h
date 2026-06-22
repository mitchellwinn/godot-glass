#pragma once

#include "core/templates/hash_map.h"
#include "core/variant/typed_array.h"
#include "scene/3d/node_3d.h"

class MeshInstance3D;
class StaticBody3D;
class GlassHeightSampler;
class BaseMaterial3D;

// GlassTerrain
//
// Native runtime node for the Mahou/Glass procedural terrain. It owns the data
// model (islands, cuts, world-scale config) and, on rebuild(), drives the native
// GlassTerrainMeshBuilder kernel to produce one merged ArrayMesh, hangs it on an
// internal "TerrainMesh" MeshInstance3D, assigns per-surface materials from the
// builder's surface_textures map, and (optionally) builds per-island
// ConcavePolygonShape3D collision from the walkable triangles. A GlassHeightSampler
// is built from the collision faces so sample_height(Vector2) returns the top
// surface Y.
//
// SCOPE: runtime node only (no editor plugin in this slice). The generated
// children are added with INTERNAL_MODE_FRONT so they never serialize into the
// scene — mirroring FlowRegion's internal-child convention.
class GlassTerrain : public Node3D {
	GDCLASS(GlassTerrain, Node3D);

	// ---- Config ----
	int tile_size = 16;
	double tile_world_size = 0.5;
	double level_height = 0.5;
	double slope_y_threshold = 0.15;
	bool build_collision = true;
	bool auto_rebuild = true;

	// ---- Data model ----
	TypedArray<Dictionary> islands;
	TypedArray<Dictionary> cuts;

	// ---- Generated children (internal, never serialized) ----
	MeshInstance3D *mesh_instance = nullptr;
	StaticBody3D *collision_body = nullptr;
	Ref<GlassHeightSampler> height_sampler;

	// tex_key -> material (memoized across rebuilds).
	HashMap<String, Ref<BaseMaterial3D>> material_cache;

	void _ensure_mesh_instance();
	void _clear_collision();
	Ref<BaseMaterial3D> _get_or_create_material(const String &p_tex_key);

	bool _rebuild_queued = false;
	void _request_rebuild();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_tile_size(int p_v);
	int get_tile_size() const;

	void set_tile_world_size(double p_v);
	double get_tile_world_size() const;

	void set_level_height(double p_v);
	double get_level_height() const;

	void set_slope_y_threshold(double p_v);
	double get_slope_y_threshold() const;

	void set_build_collision(bool p_v);
	bool get_build_collision() const;

	void set_auto_rebuild(bool p_v);
	bool get_auto_rebuild() const;

	void set_islands(const TypedArray<Dictionary> &p_islands);
	TypedArray<Dictionary> get_islands() const;

	void set_cuts(const TypedArray<Dictionary> &p_cuts);
	TypedArray<Dictionary> get_cuts() const;

	// Rebuild the whole terrain: mesh, materials, collision, height sampler.
	void rebuild();

	// Top-surface height at an XZ position, or a large negative sentinel on miss
	// (matches GlassHeightSampler::NO_HIT, which the caller checks with h > -1e17).
	double sample_height(const Vector2 &p_xz) const;

	GlassTerrain();
};
