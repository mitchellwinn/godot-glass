#include "glass_tiles_mesher.h"

#include "core/object/class_db.h"
#include "scene/resources/mesh.h"

GlassTilesMesher::GlassTilesMesher() {
}

// ---------------------------------------------------------------------------
// Key parsing
// ---------------------------------------------------------------------------
//
// Composite key = var_to_str(Vector3i) + ":" + str(axis) + ":" + str(layer).
// Example: "Vector3i(2, 0, -3):0:0". The Vector3i string contains commas and
// parentheses but NO colon, so colon-splitting is safe: parts[0] is the position
// token, parts[1] is axis, parts[2] is layer. We only need the position here
// (axis/layer/uv/etc. come from the per-tile Dictionary). Freeform "shape_*" keys
// (and anything that does not parse to a Vector3i) are rejected.
bool GlassTilesMesher::_key_to_pos(const String &p_key, Vector3i &r_pos) {
	int colon = p_key.find(":");
	String pos_str = (colon >= 0) ? p_key.substr(0, colon) : p_key;
	pos_str = pos_str.strip_edges();
	if (!pos_str.begins_with("Vector3i(")) {
		return false;
	}
	// Strip the "Vector3i(" prefix and trailing ")".
	String inner = pos_str.substr(9); // len("Vector3i(") == 9
	int close = inner.rfind(")");
	if (close < 0) {
		return false;
	}
	inner = inner.substr(0, close);
	Vector<String> comps = inner.split(",");
	if (comps.size() != 3) {
		return false;
	}
	r_pos = Vector3i(
			comps[0].strip_edges().to_int(),
			comps[1].strip_edges().to_int(),
			comps[2].strip_edges().to_int());
	return true;
}

// ---------------------------------------------------------------------------
// Geometry helpers (ports of mesh_builder.gd statics)
// ---------------------------------------------------------------------------

// CCW corners [0,1,2,3], origin = Vector3(grid_pos) * size. See the audit:
//   AXIS_XZ (floor/ceiling), normal UP   : flat on XZ at this Y.
//   AXIS_XY (front/back wall), normal BACK: builds bottom-to-top in Y.
//   AXIS_YZ (left/right wall), normal LEFT: builds bottom-to-top in Y.
void GlassTilesMesher::_get_quad_corners(const Vector3i &p_grid_pos, int p_axis, double p_size, Vector3 r_corners[4]) {
	const Vector3 origin = Vector3(p_grid_pos) * p_size;
	const double s = p_size;
	switch (p_axis) {
		case AXIS_XY: { // front/back wall, faces -Z
			r_corners[0] = origin;
			r_corners[1] = origin + Vector3(s, 0, 0);
			r_corners[2] = origin + Vector3(s, s, 0);
			r_corners[3] = origin + Vector3(0, s, 0);
		} break;
		case AXIS_YZ: { // left/right wall, faces -X
			r_corners[0] = origin;
			r_corners[1] = origin + Vector3(0, 0, s);
			r_corners[2] = origin + Vector3(0, s, s);
			r_corners[3] = origin + Vector3(0, s, 0);
		} break;
		case AXIS_XZ:
		default: { // floor/ceiling, faces UP
			r_corners[0] = origin;
			r_corners[1] = origin + Vector3(s, 0, 0);
			r_corners[2] = origin + Vector3(s, 0, s);
			r_corners[3] = origin + Vector3(0, 0, s);
		} break;
	}
}

Vector3 GlassTilesMesher::_get_axis_normal(int p_axis) {
	switch (p_axis) {
		case AXIS_XY:
			return Vector3(0, 0, 1); // BACK
		case AXIS_YZ:
			return Vector3(-1, 0, 0); // LEFT
		case AXIS_XZ:
		default:
			return Vector3(0, 1, 0); // UP
	}
}

// uv_size = (1/tiles_x, 1/tiles_y); origin = uv * uv_size. Shrink the cell rect
// by half a texel on every edge to stop NEAREST bleed across tile boundaries.
Rect2 GlassTilesMesher::_get_uv_rect(const Vector2i &p_uv, int p_tiles_x, int p_tiles_y, const Vector2 &p_half_texel) {
	const double sx = (p_tiles_x > 0) ? (1.0 / (double)p_tiles_x) : 0.0;
	const double sy = (p_tiles_y > 0) ? (1.0 / (double)p_tiles_y) : 0.0;
	const Vector2 uv_size(sx, sy);
	const Vector2 uv_origin = Vector2((double)p_uv.x * sx, (double)p_uv.y * sy);
	return Rect2(uv_origin + p_half_texel, uv_size - p_half_texel * 2.0);
}

// rotation==0 -> no-op; else r = rotation%4, rotated[i] = corners[(i+r)%4].
// The corner POSITIONS cycle while the _add_quad UV assignment stays fixed,
// which is exactly the GDScript "rotate UV mapping by cycling corner order".
void GlassTilesMesher::_rotate_corners(Vector3 r_corners[4], int p_rotation) {
	int r = p_rotation % 4;
	if (r < 0) {
		r += 4;
	}
	if (r == 0) {
		return;
	}
	Vector3 src[4] = { r_corners[0], r_corners[1], r_corners[2], r_corners[3] };
	for (int i = 0; i < 4; i++) {
		r_corners[i] = src[(i + r) % 4];
	}
}

// Two CCW triangles (0-1-2, 0-2-3) with UVs derived from the (possibly flipped)
// rect. Identical winding/UV idiom to GlassTerrainMeshBuilder::_add_quad.
void GlassTilesMesher::_add_quad(const Ref<SurfaceTool> &p_st, const Vector3 p_corners[4], const Rect2 &p_uv_rect) {
	const Vector2 uv0 = p_uv_rect.position;
	const Vector2 uv1 = Vector2(p_uv_rect.get_end().x, p_uv_rect.position.y);
	const Vector2 uv2 = p_uv_rect.get_end();
	const Vector2 uv3 = Vector2(p_uv_rect.position.x, p_uv_rect.get_end().y);
	p_st->set_uv(uv0); p_st->add_vertex(p_corners[0]);
	p_st->set_uv(uv1); p_st->add_vertex(p_corners[1]);
	p_st->set_uv(uv2); p_st->add_vertex(p_corners[2]);
	p_st->set_uv(uv0); p_st->add_vertex(p_corners[0]);
	p_st->set_uv(uv2); p_st->add_vertex(p_corners[2]);
	p_st->set_uv(uv3); p_st->add_vertex(p_corners[3]);
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

Ref<ArrayMesh> GlassTilesMesher::build_mesh(const Dictionary &p_tile_data, const Vector2i &p_tileset_size, int p_tile_px, double p_tile_world_size) {
	Ref<ArrayMesh> mesh;
	mesh.instantiate();

	if (p_tile_data.is_empty() || p_tile_px <= 0 || p_tileset_size.x <= 0 || p_tileset_size.y <= 0) {
		return mesh; // 0 surfaces
	}

	const double tex_w = (double)p_tileset_size.x;
	const double tex_h = (double)p_tileset_size.y;
	const int tiles_x = p_tileset_size.x / p_tile_px; // integer cols
	const int tiles_y = p_tileset_size.y / p_tile_px; // integer rows
	if (tiles_x <= 0 || tiles_y <= 0) {
		return mesh;
	}
	const Vector2 half_texel(0.5 / tex_w, 0.5 / tex_h);
	// Pixel -> world scale for optional per-tile offset.
	const double px_to_world = (p_tile_px > 0) ? (p_tile_world_size / (double)p_tile_px) : 0.0;

	Ref<SurfaceTool> st;
	st.instantiate();
	st->begin(Mesh::PRIMITIVE_TRIANGLES);

	int emitted = 0;
	const Array keys = p_tile_data.keys();
	for (int k = 0; k < keys.size(); k++) {
		const Variant key_v = keys[k];
		const String key = key_v;

		const Variant tile_v = p_tile_data[key_v];
		if (tile_v.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary tile = tile_v;

		// Freeform-vertices override is OUT OF SCOPE for this slice.
		if (tile.has("vertices")) {
			continue;
		}

		Vector3i grid_pos;
		if (!_key_to_pos(key, grid_pos)) {
			continue; // freeform / unparseable key
		}

		const int axis = tile.has("axis") ? (int)tile["axis"] : (int)AXIS_XZ;
		const int rotation = tile.has("rotation") ? (int)tile["rotation"] : 0;
		bool flip_h = tile.has("flip_h") ? (bool)tile["flip_h"] : false;
		bool flip_v = tile.has("flip_v") ? (bool)tile["flip_v"] : false;
		const int layer = tile.has("layer") ? (int)tile["layer"] : 0;

		Vector2i uv;
		if (tile.has("uv")) {
			uv = tile["uv"];
		}

		// --- Corner positions ---
		Vector3 corners[4];
		_get_quad_corners(grid_pos, axis, p_tile_world_size, corners);
		_rotate_corners(corners, rotation);

		// --- Optional pixel offset (h, v, depth) shifted per axis ---
		if (tile.has("offset")) {
			const Variant off_v = tile["offset"];
			Vector3 off;
			if (off_v.get_type() == Variant::VECTOR3) {
				off = off_v;
			} else if (off_v.get_type() == Variant::VECTOR2) {
				const Vector2 o2 = off_v;
				off = Vector3(o2.x, o2.y, 0.0);
			}
			if (off != Vector3()) {
				const double oh = off.x * px_to_world;
				const double ov = off.y * px_to_world;
				const double od = off.z * px_to_world;
				Vector3 shift;
				switch (axis) {
					case AXIS_XY:
						shift = Vector3(oh, ov, od);
						break;
					case AXIS_YZ:
						shift = Vector3(od, ov, oh);
						break;
					case AXIS_XZ:
					default:
						shift = Vector3(oh, od, ov);
						break;
				}
				for (int i = 0; i < 4; i++) {
					corners[i] += shift;
				}
			}
		}

		// --- Optional per-layer z-fight nudge along the axis normal ---
		if (layer > 0) {
			const Vector3 nudge = _get_axis_normal(axis) * 0.001 * (double)layer;
			for (int i = 0; i < 4; i++) {
				corners[i] += nudge;
			}
		}

		// --- UV slice (half-texel inset) ---
		Rect2 uv_rect = _get_uv_rect(uv, tiles_x, tiles_y, half_texel);

		// --- Flip (walls auto-correct Y-up vs UV-down by toggling flip_v) ---
		if (axis == AXIS_XY || axis == AXIS_YZ) {
			flip_v = !flip_v;
		}
		if (flip_h) {
			uv_rect.position.x += uv_rect.size.x;
			uv_rect.size.x = -uv_rect.size.x;
		}
		if (flip_v) {
			uv_rect.position.y += uv_rect.size.y;
			uv_rect.size.y = -uv_rect.size.y;
		}

		_add_quad(st, corners, uv_rect);
		emitted++;
	}

	if (emitted == 0) {
		return mesh; // 0 surfaces
	}

	st->generate_normals();
	st->generate_tangents();
	// Re-index AFTER normals/tangents. generate_normals() deindexes (and only
	// re-indexes if the surface was already indexed, which it never is here);
	// generate_tangents() also leaves it deindexed. Without this the committed
	// surface is a 6-vertex un-indexed triangle soup. index() dedups on
	// vertex+uv+normal+tangent, so each quad's two shared corners collapse,
	// yielding 4 unique verts + 6 indices per quad (what the smoke test asserts).
	st->index();
	Ref<ArrayMesh> committed = st->commit(mesh);
	return committed.is_valid() ? committed : mesh;
}

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

void GlassTilesMesher::_bind_methods() {
	ClassDB::bind_method(
			D_METHOD("build_mesh", "tile_data", "tileset_size", "tile_px", "tile_world_size"),
			&GlassTilesMesher::build_mesh);
}
