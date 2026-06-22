#include "glass_tiles_map.h"

#include "glass_tiles_mesher.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/texture.h"

// ---------------------------------------------------------------------------
// Config setters/getters
// ---------------------------------------------------------------------------

void GlassTilesMap::set_tile_size(int p_v) {
	tile_size = p_v;
	_request_rebuild();
}

int GlassTilesMap::get_tile_size() const {
	return tile_size;
}

void GlassTilesMap::set_pixels_per_unit(double p_v) {
	pixels_per_unit = p_v;
	// Keep the derived world size consistent when ppu changes.
	if (pixels_per_unit > 0.0) {
		tile_world_size = (double)tile_size / pixels_per_unit;
	}
	_request_rebuild();
}

double GlassTilesMap::get_pixels_per_unit() const {
	return pixels_per_unit;
}

void GlassTilesMap::set_tile_world_size(double p_v) {
	tile_world_size = p_v;
	_request_rebuild();
}

double GlassTilesMap::get_tile_world_size() const {
	return tile_world_size;
}

void GlassTilesMap::set_tileset_texture_path(const String &p_path) {
	tileset_texture_path = p_path;
	_request_rebuild();
}

String GlassTilesMap::get_tileset_texture_path() const {
	return tileset_texture_path;
}

void GlassTilesMap::set_tile_data(const Dictionary &p_tile_data) {
	tile_data = p_tile_data;
	_request_rebuild();
}

Dictionary GlassTilesMap::get_tile_data() const {
	return tile_data;
}

void GlassTilesMap::set_auto_rebuild(bool p_v) {
	auto_rebuild = p_v;
}

bool GlassTilesMap::get_auto_rebuild() const {
	return auto_rebuild;
}

// ---------------------------------------------------------------------------
// Internal child management
// ---------------------------------------------------------------------------

void GlassTilesMap::_ensure_mesh_instance() {
	if (mesh_instance) {
		return;
	}
	mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name("TiledMesh");
	// Pixel-art tiles are unshaded; they neither cast nor need shadows.
	mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	add_child(mesh_instance, false, INTERNAL_MODE_FRONT);
}

// ---------------------------------------------------------------------------
// Material policy
// ---------------------------------------------------------------------------
//
// One shared StandardMaterial3D per tileset texture, with the pixel-art flag set:
//   CULL_DISABLED          double-sided (floor/wall quads are single-sided).
//   TEXTURE_FILTER_NEAREST crisp pixels (this is WHY the mesher half-texel-insets).
//   SHADING_MODE_UNSHADED  flat, lighting-independent.
//   TRANSPARENCY_ALPHA_SCISSOR @ 0.5  hard 1-bit cutout (no blend / no sort).
// If the texture path fails to resolve, we return an UNCACHED unshaded-white
// fallback so a later rebuild (after the texture is available) upgrades it.
Ref<BaseMaterial3D> GlassTilesMap::_get_or_create_material() {
	// Cache hit only when the path is unchanged AND was successfully resolved.
	if (tileset_material.is_valid() && cached_material_path == tileset_texture_path) {
		return tileset_material;
	}

	Ref<Texture2D> tex;
	if (!tileset_texture_path.is_empty() && ResourceLoader::exists(tileset_texture_path)) {
		Ref<Resource> res = ResourceLoader::load(tileset_texture_path);
		tex = res;
	}

	Ref<StandardMaterial3D> mat;
	mat.instantiate();
	mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	mat->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_NEAREST);
	mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
	mat->set_alpha_scissor_threshold(0.5);

	if (tex.is_null()) {
		// Unresolved: unshaded white, NOT cached (upgradeable on next rebuild).
		mat->set_albedo(Color(1, 1, 1));
		return mat;
	}

	mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
	tileset_material = mat;
	cached_material_path = tileset_texture_path;
	return mat;
}

// ---------------------------------------------------------------------------
// Rebuild
// ---------------------------------------------------------------------------

void GlassTilesMap::rebuild() {
	_rebuild_queued = false;
	_ensure_mesh_instance();

	// Empty data: clear the mesh and bail.
	if (tile_data.is_empty()) {
		mesh_instance->set_mesh(Ref<Mesh>());
		return;
	}

	// Resolve the tileset texture dimensions (pixels) for the mesher's UV slice.
	Vector2i tileset_size;
	Ref<Texture2D> tex;
	if (!tileset_texture_path.is_empty() && ResourceLoader::exists(tileset_texture_path)) {
		Ref<Resource> res = ResourceLoader::load(tileset_texture_path);
		tex = res;
	}
	if (tex.is_valid()) {
		tileset_size = Vector2i(tex->get_width(), tex->get_height());
	}

	// --- Mesh build ---
	Ref<GlassTilesMesher> mesher;
	mesher.instantiate();
	Ref<ArrayMesh> mesh = mesher->build_mesh(tile_data, tileset_size, tile_size, tile_world_size);

	if (mesh.is_null() || mesh->get_surface_count() == 0) {
		mesh_instance->set_mesh(Ref<Mesh>());
		return;
	}

	mesh_instance->set_mesh(mesh);

	// --- Single shared tileset material on the one surface ---
	Ref<BaseMaterial3D> mat = _get_or_create_material();
	if (mat.is_valid()) {
		mesh->surface_set_material(0, mat);
	}
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void GlassTilesMap::_request_rebuild() {
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
	callable_mp(this, &GlassTilesMap::rebuild).call_deferred();
}

void GlassTilesMap::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY && !Engine::get_singleton()->is_editor_hint()) {
		rebuild();
	}
}

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

void GlassTilesMap::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_tile_size", "tile_size"), &GlassTilesMap::set_tile_size);
	ClassDB::bind_method(D_METHOD("get_tile_size"), &GlassTilesMap::get_tile_size);
	ClassDB::bind_method(D_METHOD("set_pixels_per_unit", "pixels_per_unit"), &GlassTilesMap::set_pixels_per_unit);
	ClassDB::bind_method(D_METHOD("get_pixels_per_unit"), &GlassTilesMap::get_pixels_per_unit);
	ClassDB::bind_method(D_METHOD("set_tile_world_size", "tile_world_size"), &GlassTilesMap::set_tile_world_size);
	ClassDB::bind_method(D_METHOD("get_tile_world_size"), &GlassTilesMap::get_tile_world_size);
	ClassDB::bind_method(D_METHOD("set_tileset_texture_path", "path"), &GlassTilesMap::set_tileset_texture_path);
	ClassDB::bind_method(D_METHOD("get_tileset_texture_path"), &GlassTilesMap::get_tileset_texture_path);
	ClassDB::bind_method(D_METHOD("set_tile_data", "tile_data"), &GlassTilesMap::set_tile_data);
	ClassDB::bind_method(D_METHOD("get_tile_data"), &GlassTilesMap::get_tile_data);
	ClassDB::bind_method(D_METHOD("set_auto_rebuild", "auto_rebuild"), &GlassTilesMap::set_auto_rebuild);
	ClassDB::bind_method(D_METHOD("get_auto_rebuild"), &GlassTilesMap::get_auto_rebuild);

	ClassDB::bind_method(D_METHOD("rebuild"), &GlassTilesMap::rebuild);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "tile_size"), "set_tile_size", "get_tile_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pixels_per_unit"), "set_pixels_per_unit", "get_pixels_per_unit");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tile_world_size", PROPERTY_HINT_NONE, "suffix:m"), "set_tile_world_size", "get_tile_world_size");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "tileset_texture_path", PROPERTY_HINT_FILE, "*.png,*.webp,*.tres,*.res"), "set_tileset_texture_path", "get_tileset_texture_path");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "tile_data"), "set_tile_data", "get_tile_data");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_rebuild"), "set_auto_rebuild", "get_auto_rebuild");
}

GlassTilesMap::GlassTilesMap() {
}
