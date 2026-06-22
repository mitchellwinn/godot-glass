#include "glass_terrain.h"

#include "glass_height_sampler.h"
#include "glass_terrain_mesh_builder.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

// Sentinel tex_key: surfaces with this key are left untextured (no material).
static const char *GLASS_TERRAIN_DEFAULT_TEXTURE = "__default__";
// Terrain collision sits on layer 1 plus a dedicated terrain bit (1 << 19).
static const uint32_t GLASS_TERRAIN_COLLISION_LAYER = 1u | (1u << 19);

// ---------------------------------------------------------------------------
// Config setters/getters
// ---------------------------------------------------------------------------

void GlassTerrain::set_tile_size(int p_v) {
	tile_size = p_v;
	_request_rebuild();
}

int GlassTerrain::get_tile_size() const {
	return tile_size;
}

void GlassTerrain::set_tile_world_size(double p_v) {
	tile_world_size = p_v;
	_request_rebuild();
}

double GlassTerrain::get_tile_world_size() const {
	return tile_world_size;
}

void GlassTerrain::set_level_height(double p_v) {
	level_height = p_v;
	_request_rebuild();
	update_gizmos();
}

double GlassTerrain::get_level_height() const {
	return level_height;
}

void GlassTerrain::set_slope_y_threshold(double p_v) {
	slope_y_threshold = p_v;
	_request_rebuild();
}

double GlassTerrain::get_slope_y_threshold() const {
	return slope_y_threshold;
}

void GlassTerrain::set_build_collision(bool p_v) {
	build_collision = p_v;
	_request_rebuild();
}

bool GlassTerrain::get_build_collision() const {
	return build_collision;
}

void GlassTerrain::set_auto_rebuild(bool p_v) {
	auto_rebuild = p_v;
}

bool GlassTerrain::get_auto_rebuild() const {
	return auto_rebuild;
}

void GlassTerrain::set_islands(const TypedArray<Dictionary> &p_islands) {
	islands = p_islands;
	_request_rebuild();
	update_gizmos();
}

TypedArray<Dictionary> GlassTerrain::get_islands() const {
	return islands;
}

void GlassTerrain::set_cuts(const TypedArray<Dictionary> &p_cuts) {
	cuts = p_cuts;
	_request_rebuild();
}

TypedArray<Dictionary> GlassTerrain::get_cuts() const {
	return cuts;
}

// ---------------------------------------------------------------------------
// Internal child management
// ---------------------------------------------------------------------------

void GlassTerrain::_ensure_mesh_instance() {
	if (mesh_instance) {
		return;
	}
	mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name("TerrainMesh");
	// Terrain self-shadows via shader; the mesh itself casts no shadow.
	mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	add_child(mesh_instance, false, INTERNAL_MODE_FRONT);
}

void GlassTerrain::_clear_collision() {
	if (collision_body) {
		remove_child(collision_body);
		collision_body->queue_free();
		collision_body = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Material policy
// ---------------------------------------------------------------------------
//
// Generic (no ygbf-specific shader paths): a tex_key is either the sentinel
// DEFAULT_TEXTURE (skipped by the caller) or a res:// texture path. We resolve
// it via ResourceLoader and wrap it in a StandardMaterial3D albedo. If the path
// fails to load, we return an UNCACHED unshaded-white fallback so a later
// rebuild (after the texture is available) upgrades it instead of caching white.
Ref<BaseMaterial3D> GlassTerrain::_get_or_create_material(const String &p_tex_key) {
	if (material_cache.has(p_tex_key)) {
		return material_cache[p_tex_key];
	}

	Ref<Texture2D> tex;
	if (ResourceLoader::exists(p_tex_key)) {
		Ref<Resource> res = ResourceLoader::load(p_tex_key);
		tex = res;
	}

	Ref<StandardMaterial3D> mat;
	mat.instantiate();

	if (tex.is_null()) {
		// Unresolved: unshaded white, NOT cached (upgradeable on next rebuild).
		mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		mat->set_albedo(Color(1, 1, 1));
		return mat;
	}

	mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
	material_cache[p_tex_key] = mat;
	return mat;
}

// ---------------------------------------------------------------------------
// Rebuild
// ---------------------------------------------------------------------------

void GlassTerrain::rebuild() {
	_rebuild_queued = false;
	_ensure_mesh_instance();

	// --- 1. Per-island surface build ---
	Ref<GlassTerrainMeshBuilder> builder;
	builder.instantiate();

	Array island_caches;
	Array all_islands;
	for (int i = 0; i < islands.size(); i++) {
		all_islands.push_back(islands[i]);
	}
	Array cut_array;
	for (int i = 0; i < cuts.size(); i++) {
		cut_array.push_back(cuts[i]);
	}

	for (int i = 0; i < islands.size(); i++) {
		Dictionary island = islands[i];
		Dictionary island_cache = builder->build_island_surfaces(
				island, cut_array, i, all_islands, tile_world_size, level_height);
		island_caches.push_back(island_cache);
	}

	// Empty terrain: clear everything and bail.
	if (island_caches.is_empty()) {
		mesh_instance->set_mesh(Ref<Mesh>());
		_clear_collision();
		height_sampler.unref();
		return;
	}

	// --- 2. Merge into one ArrayMesh ---
	Dictionary merge_result = builder->merge_caches(island_caches);
	Ref<ArrayMesh> mesh = merge_result["mesh"];
	PackedStringArray surface_textures = merge_result["surface_textures"];

	mesh_instance->set_mesh(mesh);

	// --- 3. Per-surface materials ---
	if (mesh.is_valid()) {
		int surf_count = mesh->get_surface_count();
		for (int s = 0; s < surf_count; s++) {
			if (s >= surface_textures.size()) {
				break;
			}
			const String tex_key = surface_textures[s];
			if (tex_key == GLASS_TERRAIN_DEFAULT_TEXTURE) {
				continue; // untextured surface, leave material-less
			}
			Ref<BaseMaterial3D> mat = _get_or_create_material(tex_key);
			if (mat.is_valid()) {
				mesh->surface_set_material(s, mat);
			}
		}
	}

	// --- 4. Collision + height sampler ---
	_clear_collision();
	height_sampler.unref();

	if (!build_collision) {
		return;
	}

	PackedVector3Array all_faces;
	for (int i = 0; i < island_caches.size(); i++) {
		Dictionary island = islands[i];
		// Per-island opt-out (contributes no collision shapes).
		if (island.has("no_collision") && bool(island["no_collision"])) {
			continue;
		}
		Dictionary island_cache = island_caches[i];
		PackedVector3Array tris = builder->extract_walkable_triangles(island_cache, slope_y_threshold);
		all_faces.append_array(tris);
	}

	if (all_faces.is_empty()) {
		return;
	}

	// Single ConcavePolygonShape3D on a StaticBody3D + CollisionShape3D child.
	// (Per-island body splitting is a later slice; this slice builds one body.)
	collision_body = memnew(StaticBody3D);
	collision_body->set_name("CollisionBody_Combined");
	collision_body->set_collision_layer(GLASS_TERRAIN_COLLISION_LAYER);
	add_child(collision_body, false, INTERNAL_MODE_FRONT);

	Ref<ConcavePolygonShape3D> shape;
	shape.instantiate();
	shape->set_backface_collision_enabled(true);
	shape->set_faces(all_faces);

	CollisionShape3D *cs = memnew(CollisionShape3D);
	cs->set_name("CombinedFloor");
	cs->set_shape(shape);
	collision_body->add_child(cs, false, INTERNAL_MODE_FRONT);

	// Height sampler over the collision faces (top-surface barycentric max-Y).
	height_sampler.instantiate();
	height_sampler->build(all_faces, tile_world_size * 2.0);
}

double GlassTerrain::sample_height(const Vector2 &p_xz) const {
	if (height_sampler.is_null()) {
		return GlassHeightSampler::NO_HIT;
	}
	return height_sampler->sample(p_xz);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void GlassTerrain::_request_rebuild() {
	if (!auto_rebuild) {
		return;
	}
	if (!is_inside_tree() || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (_rebuild_queued) {
		return;
	}
	_rebuild_queued = true;
	callable_mp(this, &GlassTerrain::rebuild).call_deferred();
}

void GlassTerrain::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY && !Engine::get_singleton()->is_editor_hint()) {
		rebuild();
	}
}

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

void GlassTerrain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_tile_size", "tile_size"), &GlassTerrain::set_tile_size);
	ClassDB::bind_method(D_METHOD("get_tile_size"), &GlassTerrain::get_tile_size);
	ClassDB::bind_method(D_METHOD("set_tile_world_size", "tile_world_size"), &GlassTerrain::set_tile_world_size);
	ClassDB::bind_method(D_METHOD("get_tile_world_size"), &GlassTerrain::get_tile_world_size);
	ClassDB::bind_method(D_METHOD("set_level_height", "level_height"), &GlassTerrain::set_level_height);
	ClassDB::bind_method(D_METHOD("get_level_height"), &GlassTerrain::get_level_height);
	ClassDB::bind_method(D_METHOD("set_slope_y_threshold", "slope_y_threshold"), &GlassTerrain::set_slope_y_threshold);
	ClassDB::bind_method(D_METHOD("get_slope_y_threshold"), &GlassTerrain::get_slope_y_threshold);
	ClassDB::bind_method(D_METHOD("set_build_collision", "build_collision"), &GlassTerrain::set_build_collision);
	ClassDB::bind_method(D_METHOD("get_build_collision"), &GlassTerrain::get_build_collision);
	ClassDB::bind_method(D_METHOD("set_auto_rebuild", "auto_rebuild"), &GlassTerrain::set_auto_rebuild);
	ClassDB::bind_method(D_METHOD("get_auto_rebuild"), &GlassTerrain::get_auto_rebuild);
	ClassDB::bind_method(D_METHOD("set_islands", "islands"), &GlassTerrain::set_islands);
	ClassDB::bind_method(D_METHOD("get_islands"), &GlassTerrain::get_islands);
	ClassDB::bind_method(D_METHOD("set_cuts", "cuts"), &GlassTerrain::set_cuts);
	ClassDB::bind_method(D_METHOD("get_cuts"), &GlassTerrain::get_cuts);

	ClassDB::bind_method(D_METHOD("rebuild"), &GlassTerrain::rebuild);
	ClassDB::bind_method(D_METHOD("sample_height", "xz"), &GlassTerrain::sample_height);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "tile_size"), "set_tile_size", "get_tile_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tile_world_size", PROPERTY_HINT_NONE, "suffix:m"), "set_tile_world_size", "get_tile_world_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "level_height", PROPERTY_HINT_NONE, "suffix:m"), "set_level_height", "get_level_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slope_y_threshold"), "set_slope_y_threshold", "get_slope_y_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "build_collision"), "set_build_collision", "get_build_collision");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_rebuild"), "set_auto_rebuild", "get_auto_rebuild");

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "islands", PROPERTY_HINT_ARRAY_TYPE, "Dictionary"), "set_islands", "get_islands");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "cuts", PROPERTY_HINT_ARRAY_TYPE, "Dictionary"), "set_cuts", "get_cuts");
}

GlassTerrain::GlassTerrain() {
}
