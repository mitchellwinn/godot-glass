#include "glass_terrain_mesh_builder.h"

#include "core/math/geometry_2d.h"
#include "core/math/math_funcs.h"
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

// EDGE_CLIFF fallback for out-of-range edge_types (TerrainData.EDGE_CLIFF == 0).
static const int GLASS_EDGE_CLIFF = 0;

// Port of _collect_edge_runs.
TypedArray<PackedInt32Array> GlassTerrainMeshBuilder::collect_edge_runs(const PackedInt32Array &p_edge_types, int p_vert_count, int p_target_type) const {
	TypedArray<PackedInt32Array> runs;
	PackedInt32Array current_run;
	const int et_size = p_edge_types.size();
	const int *et = p_edge_types.ptr();
	for (int ei = 0; ei < p_vert_count; ei++) {
		const int edge_type = (ei < et_size) ? et[ei] : GLASS_EDGE_CLIFF;
		if (edge_type == p_target_type) {
			current_run.push_back(ei);
		} else if (current_run.size() > 0) {
			runs.push_back(current_run);
			current_run = PackedInt32Array();
		}
	}
	// Flush last run with wraparound merge.
	if (current_run.size() > 0) {
		if (runs.size() > 0) {
			PackedInt32Array first = runs[0];
			if (first.size() > 0 && first[0] == 0) {
				PackedInt32Array merged = current_run;
				merged.append_array(first);
				runs[0] = merged;
			} else {
				runs.push_back(current_run);
			}
		} else {
			runs.push_back(current_run);
		}
	}
	return runs;
}

// Port of _build_run_top_polyline.
PackedVector2Array GlassTerrainMeshBuilder::build_run_top_polyline(const PackedVector2Array &p_verts, const PackedInt32Array &p_run, int p_vert_count) const {
	PackedVector2Array top_poly;
	const int rsize = p_run.size();
	for (int i = 0; i < rsize; i++) {
		top_poly.push_back(p_verts[p_run[i]]);
	}
	if (rsize > 0) {
		const int last_edge = p_run[rsize - 1];
		top_poly.push_back(p_verts[(last_edge + 1) % p_vert_count]);
	}
	return top_poly;
}

// Port of _build_run_edge_perps.
PackedVector2Array GlassTerrainMeshBuilder::build_run_edge_perps(const PackedVector2Array &p_verts, const PackedInt32Array &p_run, int p_vert_count) const {
	PackedVector2Array edge_perps;
	const int rsize = p_run.size();
	for (int i = 0; i < rsize; i++) {
		const int ei = p_run[i];
		const Vector2 a = p_verts[ei];
		const Vector2 b = p_verts[(ei + 1) % p_vert_count];
		const Vector2 edge_norm = (b - a).normalized();
		edge_perps.push_back(Vector2(edge_norm.y, -edge_norm.x));
	}
	return edge_perps;
}

// Port of _edge_perp_at.
Vector2 GlassTerrainMeshBuilder::edge_perp_at(const PackedVector2Array &p_verts, int p_edge_idx, int p_vert_count) const {
	if (p_vert_count <= 0) {
		return Vector2(1, 0);
	}
	const int idx = (p_edge_idx + p_vert_count) % p_vert_count;
	const Vector2 a = p_verts[idx];
	const Vector2 b = p_verts[(idx + 1) % p_vert_count];
	const Vector2 edge_dir = b - a;
	if (edge_dir.length() < 0.0001) {
		return Vector2(1, 0);
	}
	const Vector2 edge_norm = edge_dir.normalized();
	return Vector2(edge_norm.y, -edge_norm.x);
}

// Port of _compute_run_seam_dirs.
PackedVector2Array GlassTerrainMeshBuilder::compute_run_seam_dirs(const PackedVector2Array &p_verts, const PackedInt32Array &p_run, const PackedVector2Array &p_edge_perps, int p_vert_count, bool p_inward) const {
	PackedVector2Array seam_dirs;
	if (p_run.is_empty() || p_edge_perps.is_empty() || p_vert_count < 3) {
		return seam_dirs;
	}
	const int first_edge = p_run[0];
	const int last_edge = p_run[p_run.size() - 1];
	const int prev_edge = (first_edge - 1 + p_vert_count) % p_vert_count;
	const int next_edge = (last_edge + 1) % p_vert_count;

	const Vector2 prev_perp = edge_perp_at(p_verts, prev_edge, p_vert_count);
	const Vector2 first_perp = p_edge_perps[0];
	const Vector2 last_perp = p_edge_perps[p_edge_perps.size() - 1];
	const Vector2 next_perp = edge_perp_at(p_verts, next_edge, p_vert_count);

	Vector2 left_dir = prev_perp + first_perp;
	if (left_dir.length() < 0.0001) {
		left_dir = first_perp;
	}
	left_dir = left_dir.normalized();

	Vector2 right_dir = last_perp + next_perp;
	if (right_dir.length() < 0.0001) {
		right_dir = last_perp;
	}
	right_dir = right_dir.normalized();

	if (p_inward) {
		left_dir = -left_dir;
		right_dir = -right_dir;
	}
	seam_dirs.push_back(left_dir);
	seam_dirs.push_back(right_dir);
	return seam_dirs;
}

// Port of _build_run_offset_dirs.
PackedVector2Array GlassTerrainMeshBuilder::build_run_offset_dirs(const PackedVector2Array &p_edge_perps, bool p_inward) const {
	PackedVector2Array dirs;
	if (p_edge_perps.is_empty()) {
		return dirs;
	}
	const int point_count = p_edge_perps.size() + 1;
	for (int i = 0; i < point_count; i++) {
		Vector2 dir;
		if (i == 0) {
			dir = p_edge_perps[0].normalized();
		} else if (i == point_count - 1) {
			dir = p_edge_perps[p_edge_perps.size() - 1].normalized();
		} else {
			dir = (p_edge_perps[i - 1] + p_edge_perps[i]).normalized();
		}
		if (p_inward) {
			dir = -dir;
		}
		dirs.push_back(dir);
	}
	return dirs;
}

// Port of _densify_run_polyline. Returns [dense, params].
Array GlassTerrainMeshBuilder::densify_run_polyline(const PackedVector2Array &p_sparse_poly, double p_tws) const {
	PackedVector2Array dense;
	PackedFloat32Array params;
	Array out;
	if (p_sparse_poly.size() < 2) {
		out.push_back(dense);
		out.push_back(params);
		return out;
	}
	const int ssize = p_sparse_poly.size();
	PackedFloat32Array cum_lens;
	cum_lens.push_back(0.0);
	for (int i = 1; i < ssize; i++) {
		cum_lens.push_back(cum_lens[i - 1] + p_sparse_poly[i].distance_to(p_sparse_poly[i - 1]));
	}
	const double total_len = cum_lens[cum_lens.size() - 1];
	if (total_len < 0.0001) {
		out.push_back(dense);
		out.push_back(params);
		return out;
	}
	for (int i = 0; i < ssize - 1; i++) {
		const Vector2 a = p_sparse_poly[i];
		const Vector2 b = p_sparse_poly[i + 1];
		const double seg_len = a.distance_to(b);
		const int segments = MAX(1, (int)Math::ceil(seg_len / p_tws));
		for (int s = 0; s < segments; s++) {
			const double t_local = (double)s / (double)segments;
			dense.push_back(a.lerp(b, t_local));
			params.push_back((cum_lens[i] + seg_len * t_local) / total_len);
		}
	}
	dense.push_back(p_sparse_poly[ssize - 1]);
	params.push_back(1.0);
	out.push_back(dense);
	out.push_back(params);
	return out;
}

// Port of _bow_run_polyline.
PackedVector2Array GlassTerrainMeshBuilder::bow_run_polyline(const PackedVector2Array &p_dense_top, const PackedFloat32Array &p_params, double p_slope_depth, bool p_inward) const {
	PackedVector2Array bowed;
	const int dsize = p_dense_top.size();
	if (dsize < 2 || p_params.size() != dsize) {
		return bowed;
	}
	for (int i = 0; i < dsize; i++) {
		Vector2 tangent;
		if (i == 0) {
			tangent = (p_dense_top[1] - p_dense_top[0]).normalized();
		} else if (i == dsize - 1) {
			tangent = (p_dense_top[i] - p_dense_top[i - 1]).normalized();
		} else {
			tangent = (p_dense_top[i + 1] - p_dense_top[i - 1]).normalized();
		}
		Vector2 perp(tangent.y, -tangent.x);
		if (p_inward) {
			perp = -perp;
		}
		const double bow_amt = Math::sin(p_params[i] * Math::PI);
		bowed.push_back(p_dense_top[i] + perp * p_slope_depth * bow_amt);
	}
	return bowed;
}

void GlassTerrainMeshBuilder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("begin"), &GlassTerrainMeshBuilder::begin);
	ClassDB::bind_method(D_METHOD("build_ground_surface", "poly", "y_height", "tws", "tex_key", "uv_scale"),
			&GlassTerrainMeshBuilder::build_ground_surface);
	ClassDB::bind_method(D_METHOD("commit"), &GlassTerrainMeshBuilder::commit);
	ClassDB::bind_method(D_METHOD("collect_edge_runs", "edge_types", "vert_count", "target_type"), &GlassTerrainMeshBuilder::collect_edge_runs);
	ClassDB::bind_method(D_METHOD("build_run_top_polyline", "verts", "run", "vert_count"), &GlassTerrainMeshBuilder::build_run_top_polyline);
	ClassDB::bind_method(D_METHOD("build_run_edge_perps", "verts", "run", "vert_count"), &GlassTerrainMeshBuilder::build_run_edge_perps);
	ClassDB::bind_method(D_METHOD("edge_perp_at", "verts", "edge_idx", "vert_count"), &GlassTerrainMeshBuilder::edge_perp_at);
	ClassDB::bind_method(D_METHOD("compute_run_seam_dirs", "verts", "run", "edge_perps", "vert_count", "inward"), &GlassTerrainMeshBuilder::compute_run_seam_dirs);
	ClassDB::bind_method(D_METHOD("build_run_offset_dirs", "edge_perps", "inward"), &GlassTerrainMeshBuilder::build_run_offset_dirs);
	ClassDB::bind_method(D_METHOD("densify_run_polyline", "sparse_poly", "tws"), &GlassTerrainMeshBuilder::densify_run_polyline);
	ClassDB::bind_method(D_METHOD("bow_run_polyline", "dense_top", "params", "slope_depth", "inward"), &GlassTerrainMeshBuilder::bow_run_polyline);
}
