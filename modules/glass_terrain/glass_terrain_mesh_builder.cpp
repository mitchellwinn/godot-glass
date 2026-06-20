#include "glass_terrain_mesh_builder.h"

#include "core/math/geometry_2d.h"
#include "core/object/class_db.h"

Ref<SurfaceTool> GlassTerrainMeshBuilder::_ensure_surface(const String &p_tex_key) {
	HashMap<String, Ref<SurfaceTool>>::Iterator it = surfaces.find(p_tex_key);
	if (it) {
		return it->value;
	}
	Ref<SurfaceTool> st;
	st.instantiate();
	st->begin(Mesh::PRIMITIVE_TRIANGLES);
	surfaces.insert(p_tex_key, st);
	surface_order.push_back(p_tex_key);
	return st;
}

void GlassTerrainMeshBuilder::begin() {
	surfaces.clear();
	surface_order.clear();
}

// Port of _build_ground_surface.
void GlassTerrainMeshBuilder::build_ground_surface(const PackedVector2Array &p_poly, double p_y_height,
		double p_tws, const String &p_tex_key, double p_uv_scale) {
	if (p_poly.size() < 3) {
		return;
	}

	Vector<int> indices = Geometry2D::triangulate_polygon(p_poly);
	if (indices.is_empty()) {
		return;
	}

	double uv_scale = p_uv_scale;
	if (uv_scale <= 0.0) {
		uv_scale = p_tws;
	}

	Ref<SurfaceTool> st = _ensure_surface(p_tex_key);

	const Vector2 *pr = p_poly.ptr();
	const int *ir = indices.ptr();
	const int count = indices.size();
	for (int i = 0; i + 2 < count; i += 3) {
		for (int j = 0; j < 3; j++) {
			const Vector2 &v2 = pr[ir[i + j]];
			st->set_uv(Vector2(v2.x / uv_scale, v2.y / uv_scale));
			st->add_vertex(Vector3(v2.x, p_y_height, v2.y));
		}
	}
}

// Port of the commit loop in build_single_island_surfaces.
Dictionary GlassTerrainMeshBuilder::commit() {
	Dictionary result;
	for (const String &tex_key : surface_order) {
		Ref<SurfaceTool> st = surfaces[tex_key];
		st->generate_normals();
		st->generate_tangents();
		Array arrays = st->commit_to_arrays();
		if (arrays.is_empty()) {
			continue;
		}
		Variant v = arrays[Mesh::ARRAY_VERTEX];
		if (v.get_type() == Variant::PACKED_VECTOR3_ARRAY) {
			PackedVector3Array verts = v;
			if (verts.size() > 0) {
				result[tex_key] = arrays;
			}
		}
	}
	return result;
}

void GlassTerrainMeshBuilder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("begin"), &GlassTerrainMeshBuilder::begin);
	ClassDB::bind_method(D_METHOD("build_ground_surface", "poly", "y_height", "tws", "tex_key", "uv_scale"),
			&GlassTerrainMeshBuilder::build_ground_surface);
	ClassDB::bind_method(D_METHOD("commit"), &GlassTerrainMeshBuilder::commit);
}
