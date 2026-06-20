#include "glass_terrain_mesh_builder.h"

#include "core/math/geometry_2d.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"

// Constants ported 1:1 from terrain_mesh_builder.gd.
static const String GLASS_DEFAULT_TEXTURE = "__default__";
static const double OVERHANG_DEPTH_TILES = 1.0;
static const double OVERHANG_REPEAT_TILES = 3.0;
static const int OVERHANG_SUBDIVISIONS = 5;
static const double OVERHANG_DROOP_POWER = 3.0;
static const double CLIFF_REPEAT_H_TILES = 3.0;
static const double CLIFF_REPEAT_V_TILES = 2.0;
static const double CLIFF_TOP_ROCK_FADE = 0.1;
static const double CLIFF_BOT_ROCK_FADE = 0.1;

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

// ---- Texture-key + curve + noise helpers ----

String GlassTerrainMeshBuilder::_derive_grass_path(const String &p_tile_path, const String &p_suffix) {
	return p_tile_path.replace("_tile.", p_suffix + ".");
}

bool GlassTerrainMeshBuilder::_island_has_grass(const Dictionary &p_island) {
	return String(p_island.get("grass_tileset", "")) != "";
}

String GlassTerrainMeshBuilder::_grass_key(const Dictionary &p_island) {
	String grass_path = p_island.get("grass_tileset", "");
	if (grass_path == "") {
		return GLASS_DEFAULT_TEXTURE;
	}
	return grass_path;
}

String GlassTerrainMeshBuilder::_cliff_key(const Dictionary &p_island) {
	String cliff_path = p_island.get("cliff_tileset", "");
	if (cliff_path == "") {
		return GLASS_DEFAULT_TEXTURE;
	}
	return cliff_path;
}

String GlassTerrainMeshBuilder::_overhang_key(const Dictionary &p_island) {
	String grass_path = p_island.get("grass_tileset", "");
	if (grass_path == "") {
		return "";
	}
	return _derive_grass_path(grass_path, "_overhang");
}

String GlassTerrainMeshBuilder::_ground_key(const Dictionary &p_island) {
	if (_island_has_grass(p_island)) {
		return _grass_key(p_island);
	}
	return _cliff_key(p_island);
}

// Port of TerrainData.evaluate_profile_curve.
double GlassTerrainMeshBuilder::_evaluate_profile_curve(const PackedVector2Array &p_curve, double p_t) {
	const int n = p_curve.size();
	if (n == 0) {
		return 0.0;
	}
	if (n == 1) {
		return p_curve[0].y;
	}
	double t = CLAMP(p_t, 0.0, 1.0);
	for (int i = 0; i < n - 1; i++) {
		if (t >= p_curve[i].x && t <= p_curve[i + 1].x) {
			const double seg_len = p_curve[i + 1].x - p_curve[i].x;
			if (seg_len < 0.0001) {
				return p_curve[i].y;
			}
			const double local_t = (t - p_curve[i].x) / seg_len;
			return Math::lerp((double)p_curve[i].y, (double)p_curve[i + 1].y, local_t);
		}
	}
	return p_curve[n - 1].y;
}

// Port of _deterministic_noise (Jenkins 32-bit on 64-bit ints, matching GDScript
// int semantics). Returns float in -1..1.
double GlassTerrainMeshBuilder::_deterministic_noise(int64_t p_hash) {
	int64_t h = p_hash;
	h = ((h >> 16) ^ h) * 0x45d9f3b;
	h = ((h >> 16) ^ h) * 0x45d9f3b;
	h = (h >> 16) ^ h;
	return (double)(h & 0xFFFF) / 32767.5 - 1.0;
}

// ---- Quad emitters ----

void GlassTerrainMeshBuilder::_add_quad(const Ref<SurfaceTool> &p_st, const PackedVector3Array &p_corners, const Rect2 &p_uv_rect) {
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

void GlassTerrainMeshBuilder::_add_quad_wall_uv(const Ref<SurfaceTool> &p_st, const PackedVector3Array &p_corners, const Vector2 &p_face_normal_xz, double p_uv_repeat_u, double p_uv_repeat_v) {
	const Vector2 tangent(-p_face_normal_xz.y, p_face_normal_xz.x);
	Vector2 uvs[4];
	for (int i = 0; i < 4; i++) {
		const Vector3 &c = p_corners[i];
		const double u = (tangent.x * c.x + tangent.y * c.z) / p_uv_repeat_u;
		const double v = c.y / p_uv_repeat_v;
		uvs[i] = Vector2(u, v);
	}
	p_st->set_uv(uvs[0]); p_st->add_vertex(p_corners[0]);
	p_st->set_uv(uvs[1]); p_st->add_vertex(p_corners[1]);
	p_st->set_uv(uvs[2]); p_st->add_vertex(p_corners[2]);
	p_st->set_uv(uvs[0]); p_st->add_vertex(p_corners[0]);
	p_st->set_uv(uvs[2]); p_st->add_vertex(p_corners[2]);
	p_st->set_uv(uvs[3]); p_st->add_vertex(p_corners[3]);
}

void GlassTerrainMeshBuilder::_add_quad_world_uv(const Ref<SurfaceTool> &p_st, const PackedVector3Array &p_corners, double p_tws) {
	Vector2 uvs[4];
	for (int i = 0; i < 4; i++) {
		uvs[i] = Vector2(p_corners[i].x / p_tws, p_corners[i].z / p_tws);
	}
	p_st->set_uv(uvs[0]); p_st->add_vertex(p_corners[0]);
	p_st->set_uv(uvs[1]); p_st->add_vertex(p_corners[1]);
	p_st->set_uv(uvs[2]); p_st->add_vertex(p_corners[2]);
	p_st->set_uv(uvs[0]); p_st->add_vertex(p_corners[0]);
	p_st->set_uv(uvs[2]); p_st->add_vertex(p_corners[2]);
	p_st->set_uv(uvs[3]); p_st->add_vertex(p_corners[3]);
}

// Port of _build_cliff_face.
void GlassTerrainMeshBuilder::build_cliff_face(const Dictionary &p_island, const Vector2 &p_a, const Vector2 &p_b,
		double p_y_top, int p_elev, double p_level_h, double p_tws, int p_edge_idx,
		const Dictionary &p_face_cfg, int p_vert_count, const Vector2 &p_perp_at_a,
		const Vector2 &p_perp_at_b, bool p_flush_at_a, bool p_flush_at_b, bool p_build_overhang) {
	const Vector2 edge_dir = p_b - p_a;
	const double edge_len = edge_dir.length();
	if (edge_len < 0.001) {
		return;
	}
	const Vector2 edge_norm = edge_dir.normalized();
	const Vector2 edge_perp(edge_norm.y, -edge_norm.x); // outward normal (CW polygon)

	int segments = (int)Math::ceil(edge_len / p_tws);
	if (segments < 1) {
		segments = 1;
	}

	const String rock_key = _cliff_key(p_island);
	const double grass_scale_cliff = MAX((double)p_island.get("grass_scale", 0.5), 0.01);
	const double cliff_uv_u = CLIFF_REPEAT_H_TILES * p_tws * grass_scale_cliff;
	const double cliff_uv_v = CLIFF_REPEAT_V_TILES * p_tws * grass_scale_cliff;

	PackedVector2Array def_profile;
	def_profile.push_back(Vector2(0, 0));
	def_profile.push_back(Vector2(1, 0));
	PackedVector2Array profile_curve = p_face_cfg.get("profile_curve", def_profile);
	int subdivisions = CLAMP((int)p_face_cfg.get("subdivisions", 1), 1, 16);
	const double face_scale = MAX((double)p_face_cfg.get("face_scale", 1.0), 0.0);
	const double rockiness = CLAMP((double)p_face_cfg.get("rockiness", 0.0), 0.0, 1.0);
	const int64_t rock_seed = (int)p_face_cfg.get("rock_seed", 0);
	const double rock_bias = CLAMP((double)p_face_cfg.get("rock_bias", 0.0), -1.0, 1.0);

	// Adaptive subdivision when rockiness is 0.
	if (rockiness <= 0.0 && subdivisions > 1) {
		double max_disp = 0.0;
		for (int pi = 0; pi < profile_curve.size(); pi++) {
			max_disp = MAX(max_disp, Math::abs((double)profile_curve[pi].y));
		}
		if (max_disp < 0.01) {
			subdivisions = 1;
		} else {
			subdivisions = MIN(subdivisions, 2);
		}
	}

	const bool has_vertex_perps = p_vert_count > 0;

	for (int seg_i = 0; seg_i < segments; seg_i++) {
		const double t0 = (double)seg_i / segments;
		const double t1 = (double)(seg_i + 1) / segments;
		const Vector2 p0 = p_a.lerp(p_b, t0);
		const Vector2 p1 = p_a.lerp(p_b, t1);

		Vector2 perp_left, perp_right;
		if (has_vertex_perps) {
			perp_left = (p_perp_at_a * (1.0 - t0) + p_perp_at_b * t0).normalized();
			perp_right = (p_perp_at_a * (1.0 - t1) + p_perp_at_b * t1).normalized();
		} else {
			perp_left = edge_perp;
			perp_right = edge_perp;
		}

		int64_t hash_left, hash_right;
		if (has_vertex_perps && seg_i == 0) {
			hash_left = rock_seed * 73856093 + (int64_t)p_edge_idx * 19349663;
		} else {
			hash_left = rock_seed * 73856093 + (int64_t)p_edge_idx * 4256233 + (int64_t)seg_i * 19349663;
		}
		if (has_vertex_perps && seg_i + 1 == segments) {
			hash_right = rock_seed * 73856093 + (int64_t)((p_edge_idx + 1) % p_vert_count) * 19349663;
		} else {
			hash_right = rock_seed * 73856093 + (int64_t)p_edge_idx * 4256233 + (int64_t)(seg_i + 1) * 19349663;
		}

		Ref<SurfaceTool> st_rock = _ensure_surface(rock_key);
		for (int level = 0; level < p_elev; level++) {
			const double y_row_top = p_y_top - level * p_level_h;
			const double y_row_bottom = y_row_top - p_level_h;

			for (int sub = 0; sub < subdivisions; sub++) {
				const double sub_t_top = (double)sub / subdivisions;
				const double sub_t_bot = (double)(sub + 1) / subdivisions;
				const double sub_y_top = Math::lerp(y_row_top, y_row_bottom, sub_t_top);
				const double sub_y_bot = Math::lerp(y_row_top, y_row_bottom, sub_t_bot);

				const double global_t_top = ((double)level + sub_t_top) / (double)p_elev;
				const double global_t_bot = ((double)level + sub_t_bot) / (double)p_elev;

				const double profile_top = _evaluate_profile_curve(profile_curve, global_t_top) * p_tws * face_scale;
				const double profile_bot = _evaluate_profile_curve(profile_curve, global_t_bot) * p_tws * face_scale;

				double offset_p0_top = profile_top;
				double offset_p1_top = profile_top;
				double offset_p0_bot = profile_bot;
				double offset_p1_bot = profile_bot;

				if (rockiness > 0.0) {
					double bias_weight_top = 1.0;
					double bias_weight_bot = 1.0;
					if (rock_bias > 0.0) {
						bias_weight_top = Math::lerp(1.0, 2.0, rock_bias * (1.0 - global_t_top));
						bias_weight_bot = Math::lerp(1.0, 2.0, rock_bias * (1.0 - global_t_bot));
					} else if (rock_bias < 0.0) {
						bias_weight_top = Math::lerp(1.0, 2.0, -rock_bias * global_t_top);
						bias_weight_bot = Math::lerp(1.0, 2.0, -rock_bias * global_t_bot);
					}

					const double top_fade_top = Math::smoothstep(0.0, CLIFF_TOP_ROCK_FADE, global_t_top) * Math::smoothstep(0.0, CLIFF_BOT_ROCK_FADE, 1.0 - global_t_top);
					const double top_fade_bot = Math::smoothstep(0.0, CLIFF_TOP_ROCK_FADE, global_t_bot) * Math::smoothstep(0.0, CLIFF_BOT_ROCK_FADE, 1.0 - global_t_bot);

					const int64_t row_top_idx = (int64_t)level * subdivisions + sub;
					const int64_t row_bot_idx = row_top_idx + 1;
					const double rock_scale = rockiness * p_tws * 0.3;
					offset_p0_top += _deterministic_noise(hash_left + row_top_idx * 83492791) * rock_scale * bias_weight_top * top_fade_top;
					offset_p1_top += _deterministic_noise(hash_right + row_top_idx * 83492791) * rock_scale * bias_weight_top * top_fade_top;
					offset_p0_bot += _deterministic_noise(hash_left + row_bot_idx * 83492791) * rock_scale * bias_weight_bot * top_fade_bot;
					offset_p1_bot += _deterministic_noise(hash_right + row_bot_idx * 83492791) * rock_scale * bias_weight_bot * top_fade_bot;
				}

				if (p_flush_at_a && seg_i == 0) {
					offset_p0_top = 0.0;
					offset_p0_bot = 0.0;
				}
				if (p_flush_at_b && seg_i + 1 == segments) {
					offset_p1_top = 0.0;
					offset_p1_bot = 0.0;
				}

				const Vector2 p0_top = p0 + perp_left * offset_p0_top;
				const Vector2 p1_top = p1 + perp_right * offset_p1_top;
				const Vector2 p0_bot = p0 + perp_left * offset_p0_bot;
				const Vector2 p1_bot = p1 + perp_right * offset_p1_bot;

				PackedVector3Array corners;
				corners.push_back(Vector3(p0_bot.x, sub_y_bot, p0_bot.y));
				corners.push_back(Vector3(p1_bot.x, sub_y_bot, p1_bot.y));
				corners.push_back(Vector3(p1_top.x, sub_y_top, p1_top.y));
				corners.push_back(Vector3(p0_top.x, sub_y_top, p0_top.y));
				_add_quad_wall_uv(st_rock, corners, edge_perp, cliff_uv_u, cliff_uv_v);
			}
		}
	}

	// --- Overhang plane at cliff top ---
	if (p_build_overhang && p_elev > 0 && _island_has_grass(p_island)) {
		const double grass_scale = MAX((double)p_island.get("grass_scale", 0.5), 0.01);
		const double grass_droop = (double)p_island.get("grass_droop", 0.5) * p_tws * grass_scale;
		const String ovhg_tex_key = _overhang_key(p_island);
		if (ovhg_tex_key != "") {
			const double ovhg_depth = OVERHANG_DEPTH_TILES * p_tws * grass_scale;
			const double ovhg_repeat = OVERHANG_REPEAT_TILES * p_tws * grass_scale;
			const double profile_offset_top = _evaluate_profile_curve(profile_curve, 0.0) * p_tws * face_scale;
			Ref<SurfaceTool> st_ovhg = _ensure_surface(ovhg_tex_key);

			for (int seg_i = 0; seg_i < segments; seg_i++) {
				const double st0 = (double)seg_i / segments;
				const double st1 = (double)(seg_i + 1) / segments;
				const Vector2 sp0 = p_a.lerp(p_b, st0);
				const Vector2 sp1 = p_a.lerp(p_b, st1);

				Vector2 ov_perp0, ov_perp1;
				if (has_vertex_perps) {
					ov_perp0 = (p_perp_at_a * (1.0 - st0) + p_perp_at_b * st0).normalized();
					ov_perp1 = (p_perp_at_a * (1.0 - st1) + p_perp_at_b * st1).normalized();
				} else {
					ov_perp0 = edge_perp;
					ov_perp1 = edge_perp;
				}

				const Vector2 inner0 = sp0;
				const Vector2 inner1 = sp1;

				const double ou0 = (st0 * edge_len) / ovhg_repeat;
				const double ou1 = (st1 * edge_len) / ovhg_repeat;

				const double min_flat = 1.0 / (double)OVERHANG_SUBDIVISIONS;
				const double profile_cover = profile_offset_top / MAX(ovhg_depth, 0.0001) + 0.05;
				const double droop_delay = CLAMP(MAX(profile_cover, min_flat), 0.0, 0.85);

				for (int sub_i = 0; sub_i < OVERHANG_SUBDIVISIONS; sub_i++) {
					const double dt0 = (double)sub_i / OVERHANG_SUBDIVISIONS;
					const double dt1 = (double)(sub_i + 1) / OVERHANG_SUBDIVISIONS;
					const double drop_t0 = MAX(0.0, (dt0 - droop_delay) / MAX(1.0 - droop_delay, 0.0001));
					const double drop_t1 = MAX(0.0, (dt1 - droop_delay) / MAX(1.0 - droop_delay, 0.0001));
					const double droop0 = Math::pow(drop_t0, OVERHANG_DROOP_POWER) * grass_droop;
					const double droop1 = Math::pow(drop_t1, OVERHANG_DROOP_POWER) * grass_droop;
					const Vector2 strip_inner0 = inner0 + ov_perp0 * (ovhg_depth * dt0);
					const Vector2 strip_inner1 = inner1 + ov_perp1 * (ovhg_depth * dt0);
					const Vector2 strip_outer0 = inner0 + ov_perp0 * (ovhg_depth * dt1);
					const Vector2 strip_outer1 = inner1 + ov_perp1 * (ovhg_depth * dt1);
					PackedVector3Array corners;
					corners.push_back(Vector3(strip_inner0.x, p_y_top - droop0, strip_inner0.y));
					corners.push_back(Vector3(strip_inner1.x, p_y_top - droop0, strip_inner1.y));
					corners.push_back(Vector3(strip_outer1.x, p_y_top - droop1, strip_outer1.y));
					corners.push_back(Vector3(strip_outer0.x, p_y_top - droop1, strip_outer0.y));
					_add_quad(st_ovhg, corners, Rect2(ou0, dt0, ou1 - ou0, dt1 - dt0));
				}
			}
		}
	}
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
	ClassDB::bind_method(D_METHOD("build_cliff_face", "island", "a", "b", "y_top", "elev", "level_h", "tws", "edge_idx", "face_cfg", "vert_count", "perp_at_a", "perp_at_b", "flush_at_a", "flush_at_b", "build_overhang"),
			&GlassTerrainMeshBuilder::build_cliff_face);
}
