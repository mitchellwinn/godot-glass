#pragma once

#include "core/variant/dictionary.h"
#include "scene/3d/node_3d.h"

class MeshInstance3D;
class BaseMaterial3D;

// GlassTilesMap
//
// Native runtime node for the Mahou/Glass tiled-quad maps. It owns the tile data
// model (a Dictionary keyed by "Vector3i(...):axis:layer") plus the shared
// tileset config, and on rebuild() drives the GlassTilesMesher kernel to produce
// one ArrayMesh, hangs it on an internal "TiledMesh" MeshInstance3D, and assigns
// a single unshaded / NEAREST / alpha-scissor StandardMaterial3D built from the
// tileset texture.
//
// SCOPE: the textured-quad path (one tileset texture -> one surface). No
// freeform/LOD/collision in this slice. The generated MeshInstance3D child is
// added with INTERNAL_MODE_FRONT so it never serializes into the scene —
// mirroring GlassTerrain's internal-child convention.
class GlassTilesMap : public Node3D {
	GDCLASS(GlassTilesMap, Node3D);

	// ---- Config ----
	int tile_size = 16; // pixel size of one tile cell
	double pixels_per_unit = 32.0;
	double tile_world_size = 0.5; // world units per tile cell edge
	String tileset_texture_path; // res:// path to the shared tileset texture
	bool auto_rebuild = true;

	// ---- Data model ----
	Dictionary tile_data; // "Vector3i(...):axis:layer" -> per-tile Dictionary

	// ---- Generated children (internal, never serialized) ----
	MeshInstance3D *mesh_instance = nullptr;

	// Cached tileset material, rebuilt when the texture path changes.
	Ref<BaseMaterial3D> tileset_material;
	String cached_material_path;

	void _ensure_mesh_instance();
	Ref<BaseMaterial3D> _get_or_create_material();

	bool _rebuild_queued = false;
	void _request_rebuild();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_tile_size(int p_v);
	int get_tile_size() const;

	void set_pixels_per_unit(double p_v);
	double get_pixels_per_unit() const;

	void set_tile_world_size(double p_v);
	double get_tile_world_size() const;

	void set_tileset_texture_path(const String &p_path);
	String get_tileset_texture_path() const;

	void set_tile_data(const Dictionary &p_tile_data);
	Dictionary get_tile_data() const;

	void set_auto_rebuild(bool p_v);
	bool get_auto_rebuild() const;

	// Rebuild the whole tile mesh: mesher -> ArrayMesh -> internal mesh + material.
	void rebuild();

	GlassTilesMap();
};
