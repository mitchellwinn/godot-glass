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

// ---- Slope helpers ----

Dictionary GlassTerrainMeshBuilder::_make_default_face_settings() {
	// Mirror TerrainData.make_default_face_settings (FACE_PRESET_STRAIGHT profile).
	Dictionary d;
	PackedVector2Array straight;
	straight.push_back(Vector2(0, 0));
	straight.push_back(Vector2(1, 0));
	d["profile_curve"] = straight;
	d["profile_preset"] = "Straight";
	d["subdivisions"] = 4;
	d["face_scale"] = 1.0;
	d["rockiness"] = 0.0;
	d["rock_seed"] = 0;
	d["rock_bias"] = 0.0;
	return d;
}

int GlassTerrainMeshBuilder::_get_slope_subdivisions(const Dictionary &p_island) {
	if (p_island.has("slope_subdivisions")) {
		return CLAMP((int)p_island.get("slope_subdivisions", 4), 1, 16);
	}
	if (p_island.has("slope_steps")) {
		return CLAMP((int)p_island.get("slope_steps", 1), 1, 16);
	}
	return 4;
}

PackedVector2Array GlassTerrainMeshBuilder::_get_slope_curve(const Dictionary &p_island) {
	PackedVector2Array curve = p_island.get("slope_curve", PackedVector2Array());
	if (curve.size() < 2) {
		PackedVector2Array linear; // TerrainData default = Linear preset.
		linear.push_back(Vector2(0.0, 1.0));
		linear.push_back(Vector2(1.0, 0.0));
		return linear;
	}
	return curve.duplicate();
}

Vector2 GlassTerrainMeshBuilder::_resolve_bottom_pos(const Dictionary &p_island, int p_vert_idx, const Vector2 &p_top_pos, const Vector2 &p_outward_dir, double p_slope_depth) {
	Dictionary sbv = p_island.get("slope_bottom_verts", Dictionary());
	String key = itos(p_vert_idx);
	if (sbv.has(key)) {
		return sbv[key];
	}
	return p_top_pos + p_outward_dir * p_slope_depth;
}

double GlassTerrainMeshBuilder::_evaluate_slope_height_factor(const PackedVector2Array &p_curve, double p_t, bool p_reverse_t) {
	const double sample_t = p_reverse_t ? (1.0 - p_t) : p_t;
	return CLAMP(_evaluate_profile_curve(p_curve, sample_t), 0.0, 1.0);
}

Dictionary GlassTerrainMeshBuilder::_resolve_face_settings(const Dictionary &p_island, int p_edge_idx) {
	Dictionary defaults = p_island.get("face_settings", _make_default_face_settings());
	Dictionary overrides = p_island.get("face_overrides", Dictionary());
	String key = itos(p_edge_idx);
	if (overrides.has(key)) {
		Dictionary merged = defaults.duplicate(true);
		Dictionary over = overrides[key];
		LocalVector<Variant> keys = over.get_key_list();
		for (const Variant &k : keys) {
			merged[k] = over[k];
		}
		return merged;
	}
	return defaults;
}

// Port of _find_slope_target_elevation.
int GlassTerrainMeshBuilder::_find_slope_target_elevation(const Vector2 &p_a, const Vector2 &p_b, const Vector2 &p_outward, double p_slope_depth, int p_source_elev, const Array &p_all_islands) {
	const Vector2 mid = (p_a + p_b) * 0.5;
	const Vector2 probe = mid + p_outward * p_slope_depth * 0.5;
	int best_elev = 0;
	for (int i = 0; i < p_all_islands.size(); i++) {
		Dictionary island = p_all_islands[i];
		const int island_elev = island.get("elevation", 0);
		if (island_elev >= p_source_elev) {
			continue;
		}
		PackedVector2Array island_verts = island.get("vertices", PackedVector2Array());
		if (island_verts.size() < 3) {
			continue;
		}
		if (Geometry2D::is_point_in_polygon(probe, island_verts)) {
			if (island_elev > best_elev) {
				best_elev = island_elev;
			}
		}
	}
	return best_elev;
}

// Port of _build_slope_side_wall.
void GlassTerrainMeshBuilder::_build_slope_side_wall(const Dictionary &p_island, const Vector2 &p_top_pt, const Vector2 &p_outward_dir,
		double p_slope_depth, double p_y_top, double p_y_bottom, int p_slope_subdivisions,
		const PackedVector2Array &p_slope_curve, double p_tws, double p_level_h, bool p_flip_winding, bool p_reverse_curve_t) {
	if (Math::abs(p_y_top - p_y_bottom) < 0.001) {
		return;
	}
	const Vector2 bottom_pt = p_top_pt + p_outward_dir * p_slope_depth;
	const String rock_key = _cliff_key(p_island);
	Ref<SurfaceTool> st = _ensure_surface(rock_key);
	const double gs_cliff = MAX((double)p_island.get("grass_scale", 0.5), 0.01);
	const double cliff_uv_u = CLIFF_REPEAT_H_TILES * p_tws * gs_cliff;
	const double cliff_uv_v = CLIFF_REPEAT_V_TILES * p_tws * gs_cliff;

	Dictionary face_cfg = p_island.get("face_settings", _make_default_face_settings());
	PackedVector2Array def_profile;
	def_profile.push_back(Vector2(0, 0));
	def_profile.push_back(Vector2(1, 0));
	PackedVector2Array profile_curve = face_cfg.get("profile_curve", def_profile);
	const double face_scale = MAX((double)face_cfg.get("face_scale", 1.0), 0.0);
	const double rockiness = CLAMP((double)face_cfg.get("rockiness", 0.0), 0.0, 1.0);
	const int64_t rock_seed = (int)face_cfg.get("rock_seed", 0);
	const double rock_bias = CLAMP((double)face_cfg.get("rock_bias", 0.0), -1.0, 1.0);
	const double total_height = p_y_top - p_y_bottom;
	const int64_t wall_hash = rock_seed * 73856093 + (int64_t)(int)(p_top_pt.x * 1000) * 19349663 + (int64_t)(int)(p_top_pt.y * 1000) * 4256233;

	Vector2 wall_normal(-p_outward_dir.y, p_outward_dir.x);
	if (p_flip_winding) {
		wall_normal = -wall_normal;
	}

	const int subdivisions = CLAMP((int)face_cfg.get("subdivisions", 1), 1, 16);
	const int level_delta = (p_level_h > 0.001) ? MAX(1, (int)Math::round(total_height / p_level_h)) : 1;
	const bool effective_flip = p_flip_winding != p_reverse_curve_t;

	for (int level = 0; level < level_delta; level++) {
		const double y_row_top = p_y_top - level * p_level_h;
		const double y_row_bot = MAX(y_row_top - p_level_h, p_y_bottom);
		if (y_row_top <= p_y_bottom + 0.001) {
			break;
		}
		for (int sub = 0; sub < subdivisions; sub++) {
			const double sub_t_top = (double)sub / subdivisions;
			const double sub_t_bot = (double)(sub + 1) / subdivisions;
			const double sub_y_top = Math::lerp(y_row_top, y_row_bot, sub_t_top);
			const double sub_y_bot = Math::lerp(y_row_top, y_row_bot, sub_t_bot);

			const double global_t_top = ((double)level + sub_t_top) / (double)level_delta;
			const double global_t_bot = ((double)level + sub_t_bot) / (double)level_delta;

			double wf_top, wf_bot;
			if (p_reverse_curve_t) {
				wf_top = CLAMP((sub_y_top - p_y_bottom) / total_height, 0.0, 1.0);
				wf_bot = CLAMP((sub_y_bot - p_y_bottom) / total_height, 0.0, 1.0);
			} else {
				wf_top = CLAMP((p_y_top - sub_y_top) / total_height, 0.0, 1.0);
				wf_bot = CLAMP((p_y_top - sub_y_bot) / total_height, 0.0, 1.0);
			}
			if (wf_top < 0.001 && wf_bot < 0.001) {
				continue;
			}

			const Vector2 end_top = p_top_pt.lerp(bottom_pt, wf_top);
			const Vector2 end_bot = p_top_pt.lerp(bottom_pt, wf_bot);

			const int64_t row_top_idx = (int64_t)level * subdivisions + sub;
			const int64_t row_bot_idx = row_top_idx + 1;

			double off_b_top = _evaluate_profile_curve(profile_curve, global_t_top) * p_tws * face_scale;
			double off_b_bot = _evaluate_profile_curve(profile_curve, global_t_bot) * p_tws * face_scale;

			if (rockiness > 0.0) {
				const double rs = rockiness * p_tws * 0.3;
				double bw_top = 1.0, bw_bot = 1.0;
				if (rock_bias > 0.0) {
					bw_top = Math::lerp(1.0, 2.0, rock_bias * (1.0 - global_t_top));
					bw_bot = Math::lerp(1.0, 2.0, rock_bias * (1.0 - global_t_bot));
				} else if (rock_bias < 0.0) {
					bw_top = Math::lerp(1.0, 2.0, -rock_bias * global_t_top);
					bw_bot = Math::lerp(1.0, 2.0, -rock_bias * global_t_bot);
				}
				const double top_fade_top = Math::smoothstep(0.0, CLIFF_TOP_ROCK_FADE, global_t_top) * Math::smoothstep(0.0, CLIFF_BOT_ROCK_FADE, 1.0 - global_t_top);
				const double top_fade_bot = Math::smoothstep(0.0, CLIFF_TOP_ROCK_FADE, global_t_bot) * Math::smoothstep(0.0, CLIFF_BOT_ROCK_FADE, 1.0 - global_t_bot);
				off_b_top += _deterministic_noise(wall_hash + row_top_idx * 83492791) * rs * bw_top * top_fade_top;
				off_b_bot += _deterministic_noise(wall_hash + row_bot_idx * 83492791) * rs * bw_bot * top_fade_bot;
			}

			const Vector2 a_top_d = p_top_pt;
			const Vector2 b_top_d = end_top + wall_normal * off_b_top;
			const Vector2 a_bot_d = p_top_pt;
			const Vector2 b_bot_d = end_bot + wall_normal * off_b_bot;

			PackedVector3Array corners;
			if (effective_flip) {
				corners.push_back(Vector3(b_bot_d.x, sub_y_bot, b_bot_d.y));
				corners.push_back(Vector3(a_bot_d.x, sub_y_bot, a_bot_d.y));
				corners.push_back(Vector3(a_top_d.x, sub_y_top, a_top_d.y));
				corners.push_back(Vector3(b_top_d.x, sub_y_top, b_top_d.y));
			} else {
				corners.push_back(Vector3(a_bot_d.x, sub_y_bot, a_bot_d.y));
				corners.push_back(Vector3(b_bot_d.x, sub_y_bot, b_bot_d.y));
				corners.push_back(Vector3(b_top_d.x, sub_y_top, b_top_d.y));
				corners.push_back(Vector3(a_top_d.x, sub_y_top, a_top_d.y));
			}
			_add_quad_wall_uv(st, corners, wall_normal, cliff_uv_u, cliff_uv_v);
		}
	}
}

// Port of _build_slope_side_overhang.
void GlassTerrainMeshBuilder::_build_slope_side_overhang(const Dictionary &p_island, const Vector2 &p_top_pt, const Vector2 &p_bottom_pt,
		double p_y_top, double p_tws, bool p_flip_winding, double p_y_bottom, int p_slope_subdivisions,
		const PackedVector2Array &p_slope_curve, bool p_reverse_curve_t) {
	if ((int)p_island.get("elevation", 0) <= 0) {
		return;
	}
	if (!_island_has_grass(p_island)) {
		return;
	}
	const double grass_scale = MAX((double)p_island.get("grass_scale", 0.5), 0.01);
	const double grass_droop = (double)p_island.get("grass_droop", 0.5) * p_tws * grass_scale;
	const String ovhg_tex_key = _overhang_key(p_island);
	if (ovhg_tex_key == "") {
		return;
	}
	const double ovhg_depth = OVERHANG_DEPTH_TILES * p_tws * grass_scale;
	const double ovhg_repeat = OVERHANG_REPEAT_TILES * p_tws * grass_scale;
	Ref<SurfaceTool> st_ovhg = _ensure_surface(ovhg_tex_key);

	Vector2 depth_dir = p_bottom_pt - p_top_pt;
	const double edge_len = depth_dir.length();
	if (edge_len < 0.001) {
		return;
	}
	depth_dir = depth_dir.normalized();
	Vector2 side_outward(-depth_dir.y, depth_dir.x);
	if (p_flip_winding) {
		side_outward = -side_outward;
	}

	const bool follow_curve = p_slope_subdivisions > 0 && p_slope_curve.size() >= 2;
	const int row_count = follow_curve ? p_slope_subdivisions : MAX((int)Math::ceil(edge_len / p_tws), 1);

	double cum_len = 0.0;
	for (int row = 0; row < row_count; row++) {
		const double t0 = (double)row / row_count;
		const double t1 = (double)(row + 1) / row_count;
		const Vector2 pt0 = p_top_pt.lerp(p_bottom_pt, t0);
		const Vector2 pt1 = p_top_pt.lerp(p_bottom_pt, t1);

		double y0 = p_y_top;
		double y1 = p_y_top;
		if (follow_curve) {
			y0 = Math::lerp(p_y_bottom, p_y_top, _evaluate_slope_height_factor(p_slope_curve, t0, p_reverse_curve_t));
			y1 = Math::lerp(p_y_bottom, p_y_top, _evaluate_slope_height_factor(p_slope_curve, t1, p_reverse_curve_t));
		}

		const double seg_len = pt0.distance_to(pt1);
		const double ou0 = cum_len / ovhg_repeat;
		const double ou1 = (cum_len + seg_len) / ovhg_repeat;

		const Vector2 inner0 = pt0;
		const Vector2 inner1 = pt1;

		const double min_flat = 1.0 / (double)OVERHANG_SUBDIVISIONS;
		const double droop_delay = min_flat;

		for (int sub_i = 0; sub_i < OVERHANG_SUBDIVISIONS; sub_i++) {
			const double dt0 = (double)sub_i / OVERHANG_SUBDIVISIONS;
			const double dt1 = (double)(sub_i + 1) / OVERHANG_SUBDIVISIONS;
			const double drop_t0 = MAX(0.0, (dt0 - droop_delay) / MAX(1.0 - droop_delay, 0.0001));
			const double drop_t1 = MAX(0.0, (dt1 - droop_delay) / MAX(1.0 - droop_delay, 0.0001));
			const double droop0 = Math::pow(drop_t0, OVERHANG_DROOP_POWER) * grass_droop;
			const double droop1 = Math::pow(drop_t1, OVERHANG_DROOP_POWER) * grass_droop;
			const Vector2 strip_inner0 = inner0 + side_outward * (ovhg_depth * dt0);
			const Vector2 strip_inner1 = inner1 + side_outward * (ovhg_depth * dt0);
			const Vector2 strip_outer0 = inner0 + side_outward * (ovhg_depth * dt1);
			const Vector2 strip_outer1 = inner1 + side_outward * (ovhg_depth * dt1);
			PackedVector3Array corners;
			if (p_flip_winding) {
				corners.push_back(Vector3(strip_inner1.x, y1 - droop0, strip_inner1.y));
				corners.push_back(Vector3(strip_inner0.x, y0 - droop0, strip_inner0.y));
				corners.push_back(Vector3(strip_outer0.x, y0 - droop1, strip_outer0.y));
				corners.push_back(Vector3(strip_outer1.x, y1 - droop1, strip_outer1.y));
			} else {
				corners.push_back(Vector3(strip_inner0.x, y0 - droop0, strip_inner0.y));
				corners.push_back(Vector3(strip_inner1.x, y1 - droop0, strip_inner1.y));
				corners.push_back(Vector3(strip_outer1.x, y1 - droop1, strip_outer1.y));
				corners.push_back(Vector3(strip_outer0.x, y0 - droop1, strip_outer0.y));
			}
			_add_quad(st_ovhg, corners, Rect2(ou0, dt0, ou1 - ou0, dt1 - dt0));
		}
		cum_len += seg_len;
	}
}

// Port of _build_merged_slope.
void GlassTerrainMeshBuilder::build_merged_slope(const Dictionary &p_island, const PackedVector2Array &p_verts,
		const PackedInt32Array &p_edge_types, const PackedInt32Array &p_run, double p_y_top,
		int p_elev, double p_level_h, double p_tws, const Array &p_all_islands, const PackedVector2Array &p_vertex_perps) {
	if (p_run.is_empty()) {
		return;
	}
	const int vert_count = p_verts.size();
	const double slope_depth = (double)p_island.get("slope_depth", 1.0) * p_tws;
	if (slope_depth <= 0.001) {
		return;
	}
	const int slope_subdivisions = _get_slope_subdivisions(p_island);
	const PackedVector2Array slope_curve = _get_slope_curve(p_island);

	const PackedVector2Array top_poly = build_run_top_polyline(p_verts, p_run, vert_count);
	if (top_poly.size() < 2) {
		return;
	}
	const PackedVector2Array edge_perps = build_run_edge_perps(p_verts, p_run, vert_count);
	if (edge_perps.size() != p_run.size()) {
		return;
	}
	const PackedVector2Array outward_dirs = build_run_offset_dirs(edge_perps, false);
	if (outward_dirs.size() != top_poly.size()) {
		return;
	}
	const int mid_idx = p_run.size() / 2;
	const int mid_ei = p_run[mid_idx];
	const Vector2 mid_a = p_verts[mid_ei];
	const Vector2 mid_b = p_verts[(mid_ei + 1) % vert_count];
	const Vector2 mid_perp = edge_perps[mid_idx];
	const int target_elev = _find_slope_target_elevation(mid_a, mid_b, mid_perp, slope_depth, p_elev, p_all_islands);
	const double y_bottom = target_elev * p_level_h;
	const int level_delta = MAX(1, p_elev - target_elev);

	PackedVector2Array bottom_poly;
	for (int i = 0; i < top_poly.size(); i++) {
		int vert_idx;
		if (i < p_run.size()) {
			vert_idx = p_run[i];
		} else {
			vert_idx = (p_run[p_run.size() - 1] + 1) % vert_count;
		}
		bottom_poly.push_back(_resolve_bottom_pos(p_island, vert_idx, top_poly[i], outward_dirs[i], slope_depth));
	}

	const String tex_key = _ground_key(p_island);
	Ref<SurfaceTool> st = _ensure_surface(tex_key);

	for (int i = 0; i < top_poly.size() - 1; i++) {
		const Vector2 top_a = top_poly[i];
		const Vector2 top_b = top_poly[i + 1];
		const Vector2 bot_a = bottom_poly[i];
		const Vector2 bot_b = bottom_poly[i + 1];

		const double edge_len = top_a.distance_to(top_b);
		int segments = (int)Math::ceil(edge_len / p_tws);
		if (segments < 1) {
			segments = 1;
		}

		for (int seg_i = 0; seg_i < segments; seg_i++) {
			const double st0 = (double)seg_i / segments;
			const double st1 = (double)(seg_i + 1) / segments;
			const Vector2 seg_top_a = top_a.lerp(top_b, st0);
			const Vector2 seg_top_b = top_a.lerp(top_b, st1);
			const Vector2 seg_bot_a = bot_a.lerp(bot_b, st0);
			const Vector2 seg_bot_b = bot_a.lerp(bot_b, st1);

			for (int row = 0; row < slope_subdivisions; row++) {
				const double t_top = (double)row / slope_subdivisions;
				const double t_bot = (double)(row + 1) / slope_subdivisions;

				const double hf_top = _evaluate_slope_height_factor(slope_curve, t_top, false);
				const double hf_bot = _evaluate_slope_height_factor(slope_curve, t_bot, false);
				const double row_y_top = Math::lerp(y_bottom, p_y_top, hf_top);
				const double row_y_bot = Math::lerp(y_bottom, p_y_top, hf_bot);

				const Vector2 row_top_a = seg_top_a.lerp(seg_bot_a, t_top);
				const Vector2 row_top_b = seg_top_b.lerp(seg_bot_b, t_top);
				const Vector2 row_bot_a = seg_top_a.lerp(seg_bot_a, t_bot);
				const Vector2 row_bot_b = seg_top_b.lerp(seg_bot_b, t_bot);

				PackedVector3Array corners;
				corners.push_back(Vector3(row_bot_a.x, row_y_bot, row_bot_a.y));
				corners.push_back(Vector3(row_bot_b.x, row_y_bot, row_bot_b.y));
				corners.push_back(Vector3(row_top_b.x, row_y_top, row_top_b.y));
				corners.push_back(Vector3(row_top_a.x, row_y_top, row_top_a.y));
				_add_quad_world_uv(st, corners, p_tws);
			}
		}
	}

	double left_depth = top_poly[0].distance_to(bottom_poly[0]);
	double right_depth = top_poly[top_poly.size() - 1].distance_to(bottom_poly[bottom_poly.size() - 1]);
	Vector2 left_outward = bottom_poly[0] - top_poly[0];
	Vector2 right_outward = bottom_poly[bottom_poly.size() - 1] - top_poly[top_poly.size() - 1];
	if (left_depth > 0.001) {
		left_outward = left_outward / left_depth;
	}
	if (right_depth > 0.001) {
		right_outward = right_outward / right_depth;
	}

	_build_slope_side_wall(p_island, top_poly[0], left_outward, left_depth, p_y_top, y_bottom, slope_subdivisions, slope_curve, p_tws, p_level_h, false, false);
	_build_slope_side_wall(p_island, top_poly[top_poly.size() - 1], right_outward, right_depth, p_y_top, y_bottom, slope_subdivisions, slope_curve, p_tws, p_level_h, true, false);

	_build_slope_side_overhang(p_island, top_poly[0], bottom_poly[0], p_y_top, p_tws, true, y_bottom, slope_subdivisions, slope_curve, false);
	_build_slope_side_overhang(p_island, top_poly[top_poly.size() - 1], bottom_poly[bottom_poly.size() - 1], p_y_top, p_tws, false, y_bottom, slope_subdivisions, slope_curve, false);

	if (target_elev > 0) {
		const String rock_key = _cliff_key(p_island);
		const double gs_cliff = MAX((double)p_island.get("grass_scale", 0.5), 0.01);
		const double cliff_uv_u = CLIFF_REPEAT_H_TILES * p_tws * gs_cliff;
		const double cliff_uv_v = CLIFF_REPEAT_V_TILES * p_tws * gs_cliff;
		Ref<SurfaceTool> st_cliff = _ensure_surface(rock_key);

		for (int i = 0; i < p_run.size(); i++) {
			const int ei = p_run[i];
			const Vector2 a = p_verts[ei];
			const Vector2 b = p_verts[(ei + 1) % vert_count];
			const Vector2 edge_dir = b - a;
			const double edge_len = edge_dir.length();
			if (edge_len < 0.001) {
				continue;
			}
			const Vector2 edge_norm = edge_dir.normalized();
			const Vector2 edge_perp_local(edge_norm.y, -edge_norm.x);

			int segments = (int)Math::ceil(edge_len / p_tws);
			if (segments < 1) {
				segments = 1;
			}

			for (int seg_i = 0; seg_i < segments; seg_i++) {
				const double t0 = (double)seg_i / segments;
				const double t1 = (double)(seg_i + 1) / segments;
				const Vector2 p0 = a.lerp(b, t0);
				const Vector2 p1 = a.lerp(b, t1);

				for (int level = level_delta; level < p_elev; level++) {
					const double y_row_top = p_y_top - level * p_level_h;
					const double y_row_bot = y_row_top - p_level_h;
					PackedVector3Array corners;
					corners.push_back(Vector3(p0.x, y_row_bot, p0.y));
					corners.push_back(Vector3(p1.x, y_row_bot, p1.y));
					corners.push_back(Vector3(p1.x, y_row_top, p1.y));
					corners.push_back(Vector3(p0.x, y_row_top, p0.y));
					_add_quad_wall_uv(st_cliff, corners, edge_perp_local, cliff_uv_u, cliff_uv_v);
				}
			}
		}
	}
}

// Port of _build_merged_slope_in.
void GlassTerrainMeshBuilder::build_merged_slope_in(const Dictionary &p_island, const PackedVector2Array &p_verts,
		const PackedInt32Array &p_edge_types, const PackedInt32Array &p_run, double p_y_top,
		int p_elev, double p_level_h, double p_tws, const Array &p_all_islands, const PackedVector2Array &p_vertex_perps) {
	if (p_run.is_empty()) {
		return;
	}
	const int vert_count = p_verts.size();
	const double slope_depth = (double)p_island.get("slope_depth", 1.0) * p_tws;
	if (slope_depth <= 0.001) {
		return;
	}
	const int slope_subdivisions = _get_slope_subdivisions(p_island);
	const PackedVector2Array slope_curve = _get_slope_curve(p_island);

	const PackedVector2Array boundary_poly = build_run_top_polyline(p_verts, p_run, vert_count);
	if (boundary_poly.size() < 2) {
		return;
	}
	const PackedVector2Array edge_perps = build_run_edge_perps(p_verts, p_run, vert_count);
	if (edge_perps.size() != p_run.size()) {
		return;
	}
	PackedVector2Array inward_dirs = build_run_offset_dirs(edge_perps, true);
	if (inward_dirs.size() != boundary_poly.size()) {
		return;
	}
	const PackedVector2Array seam_dirs = compute_run_seam_dirs(p_verts, p_run, edge_perps, vert_count, true);
	if (seam_dirs.size() == 2 && inward_dirs.size() >= 2) {
		inward_dirs.write[0] = seam_dirs[0];
		inward_dirs.write[inward_dirs.size() - 1] = seam_dirs[1];
	}
	const int mid_idx = p_run.size() / 2;
	const int mid_ei = p_run[mid_idx];
	const Vector2 mid_a = p_verts[mid_ei];
	const Vector2 mid_b = p_verts[(mid_ei + 1) % vert_count];
	const Vector2 mid_perp = edge_perps[mid_idx];
	const int target_elev = _find_slope_target_elevation(mid_a, mid_b, mid_perp, slope_depth, p_elev, p_all_islands);
	const double y_bottom = target_elev * p_level_h;
	if (Math::abs(p_y_top - y_bottom) < 0.001) {
		return;
	}

	PackedVector2Array inner_poly;
	for (int i = 0; i < boundary_poly.size(); i++) {
		int vert_idx;
		if (i < p_run.size()) {
			vert_idx = p_run[i];
		} else {
			vert_idx = (p_run[p_run.size() - 1] + 1) % vert_count;
		}
		inner_poly.push_back(_resolve_bottom_pos(p_island, vert_idx, boundary_poly[i], inward_dirs[i], slope_depth));
	}

	const String tex_key = _ground_key(p_island);
	Ref<SurfaceTool> st = _ensure_surface(tex_key);

	for (int i = 0; i < boundary_poly.size() - 1; i++) {
		const Vector2 boundary_a = boundary_poly[i];
		const Vector2 boundary_b = boundary_poly[i + 1];
		const Vector2 inner_a = inner_poly[i];
		const Vector2 inner_b = inner_poly[i + 1];

		const double edge_len = boundary_a.distance_to(boundary_b);
		int segments = (int)Math::ceil(edge_len / p_tws);
		if (segments < 1) {
			segments = 1;
		}

		for (int seg_i = 0; seg_i < segments; seg_i++) {
			const double st0 = (double)seg_i / segments;
			const double st1 = (double)(seg_i + 1) / segments;
			const Vector2 seg_boundary_a = boundary_a.lerp(boundary_b, st0);
			const Vector2 seg_boundary_b = boundary_a.lerp(boundary_b, st1);
			const Vector2 seg_inner_a = inner_a.lerp(inner_b, st0);
			const Vector2 seg_inner_b = inner_a.lerp(inner_b, st1);

			for (int row = 0; row < slope_subdivisions; row++) {
				const double t_top = (double)row / slope_subdivisions;
				const double t_bot = (double)(row + 1) / slope_subdivisions;

				const double hf_top = _evaluate_slope_height_factor(slope_curve, t_top, true);
				const double hf_bot = _evaluate_slope_height_factor(slope_curve, t_bot, true);
				const double row_y_top = Math::lerp(y_bottom, p_y_top, hf_top);
				const double row_y_bot = Math::lerp(y_bottom, p_y_top, hf_bot);

				const Vector2 row_top_a = seg_boundary_a.lerp(seg_inner_a, t_top);
				const Vector2 row_top_b = seg_boundary_b.lerp(seg_inner_b, t_top);
				const Vector2 row_bot_a = seg_boundary_a.lerp(seg_inner_a, t_bot);
				const Vector2 row_bot_b = seg_boundary_b.lerp(seg_inner_b, t_bot);

				PackedVector3Array corners;
				corners.push_back(Vector3(row_top_a.x, row_y_top, row_top_a.y));
				corners.push_back(Vector3(row_top_b.x, row_y_top, row_top_b.y));
				corners.push_back(Vector3(row_bot_b.x, row_y_bot, row_bot_b.y));
				corners.push_back(Vector3(row_bot_a.x, row_y_bot, row_bot_a.y));
				_add_quad_world_uv(st, corners, p_tws);
			}
		}
	}

	if (target_elev > 0) {
		for (int i = 0; i < p_run.size(); i++) {
			const int ei = p_run[i];
			const Vector2 a = p_verts[ei];
			const Vector2 b = p_verts[(ei + 1) % vert_count];
			Dictionary face_cfg = _resolve_face_settings(p_island, ei);
			const Vector2 perp_a = (ei < p_vertex_perps.size()) ? p_vertex_perps[ei] : edge_perps[i];
			const Vector2 perp_b = ((ei + 1) % vert_count < p_vertex_perps.size()) ? p_vertex_perps[(ei + 1) % vert_count] : edge_perps[i];
			build_cliff_face(p_island, a, b, y_bottom, target_elev, p_level_h, p_tws, ei, face_cfg, vert_count, perp_a, perp_b, i == 0, i == p_run.size() - 1, false);
		}
	}

	double left_depth = boundary_poly[0].distance_to(inner_poly[0]);
	double right_depth = boundary_poly[boundary_poly.size() - 1].distance_to(inner_poly[inner_poly.size() - 1]);
	Vector2 left_inward = inner_poly[0] - boundary_poly[0];
	Vector2 right_inward = inner_poly[inner_poly.size() - 1] - boundary_poly[boundary_poly.size() - 1];
	if (left_depth > 0.001) {
		left_inward = left_inward / left_depth;
	}
	if (right_depth > 0.001) {
		right_inward = right_inward / right_depth;
	}

	_build_slope_side_wall(p_island, boundary_poly[0], left_inward, left_depth, p_y_top, y_bottom, slope_subdivisions, slope_curve, p_tws, p_level_h, true, true);
	_build_slope_side_wall(p_island, boundary_poly[boundary_poly.size() - 1], right_inward, right_depth, p_y_top, y_bottom, slope_subdivisions, slope_curve, p_tws, p_level_h, false, true);

	_build_slope_side_overhang(p_island, boundary_poly[0], inner_poly[0], p_y_top, p_tws, true, 0.0, 0, PackedVector2Array(), false);
	_build_slope_side_overhang(p_island, boundary_poly[boundary_poly.size() - 1], inner_poly[inner_poly.size() - 1], p_y_top, p_tws, false, 0.0, 0, PackedVector2Array(), false);
}

// ---- Polygon helpers ----

double GlassTerrainMeshBuilder::_polygon_area(const PackedVector2Array &p_poly) {
	double area = 0.0;
	const int n = p_poly.size();
	for (int i = 0; i < n; i++) {
		const int j = (i + 1) % n;
		area += p_poly[i].x * p_poly[j].y;
		area -= p_poly[j].x * p_poly[i].y;
	}
	return area * 0.5;
}

PackedVector2Array GlassTerrainMeshBuilder::_ensure_ccw(PackedVector2Array p_poly) {
	if (_polygon_area(p_poly) < 0) {
		p_poly.reverse();
	}
	return p_poly;
}

// Port of _build_slope_in_clip_for_run.
PackedVector2Array GlassTerrainMeshBuilder::_build_slope_in_clip_for_run(const PackedVector2Array &p_verts, const PackedInt32Array &p_run, double p_slope_depth, int p_source_elev, const Array &p_all_islands, const Dictionary &p_island) const {
	PackedVector2Array empty;
	if (p_run.is_empty()) {
		return empty;
	}
	const int vert_count = p_verts.size();
	const PackedVector2Array boundary_poly = build_run_top_polyline(p_verts, p_run, vert_count);
	if (boundary_poly.size() < 2) {
		return empty;
	}
	const PackedVector2Array edge_perps = build_run_edge_perps(p_verts, p_run, vert_count);
	if (edge_perps.size() != p_run.size()) {
		return empty;
	}
	const int mid_idx = p_run.size() / 2;
	const int mid_ei = p_run[mid_idx];
	const Vector2 mid_a = p_verts[mid_ei];
	const Vector2 mid_b = p_verts[(mid_ei + 1) % vert_count];
	const int target_elev = _find_slope_target_elevation(mid_a, mid_b, edge_perps[mid_idx], p_slope_depth, p_source_elev, p_all_islands);
	if (target_elev >= p_source_elev) {
		return empty;
	}

	PackedVector2Array inward_dirs = build_run_offset_dirs(edge_perps, true);
	if (inward_dirs.size() != boundary_poly.size()) {
		return empty;
	}
	const PackedVector2Array seam_dirs = compute_run_seam_dirs(p_verts, p_run, edge_perps, vert_count, true);
	if (seam_dirs.size() == 2 && inward_dirs.size() >= 2) {
		inward_dirs.write[0] = seam_dirs[0];
		inward_dirs.write[inward_dirs.size() - 1] = seam_dirs[1];
	}

	const bool island_empty = p_island.is_empty();
	PackedVector2Array inner_poly;
	for (int i = 0; i < boundary_poly.size(); i++) {
		int vert_idx;
		if (i < p_run.size()) {
			vert_idx = p_run[i];
		} else {
			vert_idx = (p_run[p_run.size() - 1] + 1) % vert_count;
		}
		if (!island_empty) {
			inner_poly.push_back(_resolve_bottom_pos(p_island, vert_idx, boundary_poly[i], inward_dirs[i], p_slope_depth));
		} else {
			inner_poly.push_back(boundary_poly[i] + inward_dirs[i] * p_slope_depth);
		}
	}

	PackedVector2Array clip_poly;
	for (int i = 0; i < boundary_poly.size(); i++) {
		clip_poly.push_back(boundary_poly[i]);
	}
	for (int i = inner_poly.size() - 1; i >= 0; i--) {
		clip_poly.push_back(inner_poly[i]);
	}
	if (clip_poly.size() < 3) {
		return empty;
	}
	clip_poly = _ensure_ccw(clip_poly);
	if (Geometry2D::triangulate_polygon(clip_poly).is_empty()) {
		return empty;
	}
	return clip_poly;
}

// Port of _compute_slope_in_clips.
TypedArray<PackedVector2Array> GlassTerrainMeshBuilder::_compute_slope_in_clips(const Dictionary &p_island, const PackedVector2Array &p_verts, const PackedInt32Array &p_edge_types, int p_elev, double p_tws, const Array &p_all_islands) const {
	TypedArray<PackedVector2Array> clips;
	if (p_verts.size() < 3) {
		return clips;
	}
	const double slope_depth = (double)p_island.get("slope_depth", 1.0) * p_tws;
	if (slope_depth <= 0.001) {
		return clips;
	}
	TypedArray<PackedInt32Array> runs = collect_edge_runs(p_edge_types, p_verts.size(), 3 /*EDGE_SLOPE_IN*/);
	for (int i = 0; i < runs.size(); i++) {
		PackedInt32Array run = runs[i];
		PackedVector2Array clip = _build_slope_in_clip_for_run(p_verts, run, slope_depth, p_elev, p_all_islands, p_island);
		if (clip.size() >= 3) {
			clips.push_back(clip);
		}
	}
	return clips;
}

// Port of _build_edge_geometry.
void GlassTerrainMeshBuilder::build_edge_geometry(const Dictionary &p_island, const PackedVector2Array &p_verts,
		const PackedInt32Array &p_edge_types, int p_elev, double p_level_h, double p_tws,
		const Array &p_all_islands, int p_island_idx) {
	const double y_top = p_elev * p_level_h;
	const int vert_count = p_verts.size();
	const int et_size = p_edge_types.size();

	// Per-vertex averaged outward perpendiculars (EDGE_CLIFF == 0).
	PackedVector2Array vertex_perps;
	for (int vi = 0; vi < vert_count; vi++) {
		const int prev_ei = (vi - 1 + vert_count) % vert_count;
		const int prev_type = (prev_ei < et_size) ? p_edge_types[prev_ei] : 0;
		const int curr_type = (vi < et_size) ? p_edge_types[vi] : 0;
		const Vector2 prev_dir = (p_verts[(prev_ei + 1) % vert_count] - p_verts[prev_ei]).normalized();
		const Vector2 prev_perp(prev_dir.y, -prev_dir.x);
		const Vector2 curr_dir = (p_verts[(vi + 1) % vert_count] - p_verts[vi]).normalized();
		const Vector2 curr_perp(curr_dir.y, -curr_dir.x);
		if (prev_type == 0 && curr_type == 0) {
			vertex_perps.push_back((prev_perp + curr_perp).normalized());
		} else if (prev_type == 0) {
			vertex_perps.push_back(prev_perp);
		} else if (curr_type == 0) {
			vertex_perps.push_back(curr_perp);
		} else {
			vertex_perps.push_back((prev_perp + curr_perp).normalized());
		}
	}

	// Cliff faces.
	for (int ei = 0; ei < vert_count; ei++) {
		const int edge_type = (ei < et_size) ? p_edge_types[ei] : 0;
		if (edge_type != 0) {
			continue;
		}
		const Vector2 a = p_verts[ei];
		const Vector2 b = p_verts[(ei + 1) % vert_count];
		Dictionary face_cfg = _resolve_face_settings(p_island, ei);
		const Vector2 perp_a = vertex_perps[ei];
		const Vector2 perp_b = vertex_perps[(ei + 1) % vert_count];
		const int prev_ei = (ei - 1 + vert_count) % vert_count;
		const int next_ei = (ei + 1) % vert_count;
		const int prev_type = (prev_ei < et_size) ? p_edge_types[prev_ei] : 0;
		const int next_type = (next_ei < et_size) ? p_edge_types[next_ei] : 0;
		const bool flush_a = prev_type != 0;
		const bool flush_b = next_type != 0;
		build_cliff_face(p_island, a, b, y_top, p_elev, p_level_h, p_tws, ei, face_cfg, vert_count, perp_a, perp_b, flush_a, flush_b, true);
	}

	// Merged outward slope runs.
	TypedArray<PackedInt32Array> slope_runs = collect_edge_runs(p_edge_types, vert_count, 1 /*EDGE_SLOPE*/);
	for (int i = 0; i < slope_runs.size(); i++) {
		PackedInt32Array run = slope_runs[i];
		build_merged_slope(p_island, p_verts, p_edge_types, run, y_top, p_elev, p_level_h, p_tws, p_all_islands, vertex_perps);
	}

	// Merged inward slope runs.
	TypedArray<PackedInt32Array> slope_in_runs = collect_edge_runs(p_edge_types, vert_count, 3 /*EDGE_SLOPE_IN*/);
	for (int i = 0; i < slope_in_runs.size(); i++) {
		PackedInt32Array run = slope_in_runs[i];
		build_merged_slope_in(p_island, p_verts, p_edge_types, run, y_top, p_elev, p_level_h, p_tws, p_all_islands, vertex_perps);
	}
}

// Port of _build_cut_geometry.
void GlassTerrainMeshBuilder::build_cut_geometry(const Dictionary &p_island, const PackedVector2Array &p_cut_verts,
		int p_parent_elev, int p_depth, double p_level_h, double p_tws) {
	const double y_top = p_parent_elev * p_level_h;
	const double y_bottom = (p_parent_elev - p_depth) * p_level_h;

	const String ground_key = _ground_key(p_island);
	double cut_uv_scale = 0.0;
	if (!_island_has_grass(p_island)) {
		const double grass_scale_cliff = MAX((double)p_island.get("grass_scale", 0.5), 0.01);
		cut_uv_scale = CLIFF_REPEAT_H_TILES * p_tws * grass_scale_cliff;
	}
	build_ground_surface(p_cut_verts, y_bottom, p_tws, ground_key, cut_uv_scale);

	const int vert_count = p_cut_verts.size();
	for (int ei = 0; ei < vert_count; ei++) {
		const Vector2 a = p_cut_verts[ei];
		const Vector2 b = p_cut_verts[(ei + 1) % vert_count];
		const Vector2 edge_dir = b - a;
		const double edge_len = edge_dir.length();
		if (edge_len < 0.001) {
			continue;
		}
		const Vector2 edge_norm = edge_dir.normalized();
		const Vector2 edge_perp(-edge_norm.y, edge_norm.x); // inward normal for cuts

		int segments = (int)Math::ceil(edge_len / p_tws);
		if (segments < 1) {
			segments = 1;
		}

		const String rock_key = _cliff_key(p_island);
		const double gs_cliff = MAX((double)p_island.get("grass_scale", 0.5), 0.01);
		const double cut_uv_u = CLIFF_REPEAT_H_TILES * p_tws * gs_cliff;
		const double cut_uv_v = CLIFF_REPEAT_V_TILES * p_tws * gs_cliff;

		for (int seg_i = 0; seg_i < segments; seg_i++) {
			const double t0 = (double)seg_i / segments;
			const double t1 = (double)(seg_i + 1) / segments;
			const Vector2 p0 = a.lerp(b, t0);
			const Vector2 p1 = a.lerp(b, t1);

			Ref<SurfaceTool> st_rock = _ensure_surface(rock_key);
			for (int level = 0; level < p_depth; level++) {
				const double y_row_top = y_top - level * p_level_h;
				const double y_row_bottom = y_row_top - p_level_h;
				PackedVector3Array corners;
				corners.push_back(Vector3(p1.x, y_row_top, p1.y));
				corners.push_back(Vector3(p0.x, y_row_top, p0.y));
				corners.push_back(Vector3(p0.x, y_row_bottom, p0.y));
				corners.push_back(Vector3(p1.x, y_row_bottom, p1.y));
				_add_quad_wall_uv(st_rock, corners, edge_perp, cut_uv_u, cut_uv_v);
			}
		}
	}
}

// Port of build_single_island_surfaces.
Dictionary GlassTerrainMeshBuilder::build_island_surfaces(const Dictionary &p_island, const Array &p_cuts, int p_island_idx,
		const Array &p_all_islands, double p_tws, double p_level_h) {
	PackedVector2Array verts = p_island.get("vertices", PackedVector2Array());
	const int elev = p_island.get("elevation", 0);
	PackedInt32Array edge_types = p_island.get("edge_types", PackedInt32Array());
	const double y_height = elev * p_level_h;

	if (verts.size() < 3) {
		return Dictionary();
	}

	begin();

	// Boolean cuts + slope_in footprints clipped from the ground polygon set.
	Vector<PackedVector2Array> polygons;
	polygons.push_back(verts);
	TypedArray<PackedVector2Array> slope_in_clips = _compute_slope_in_clips(p_island, verts, edge_types, elev, p_tws, p_all_islands);

	for (int ci = 0; ci < p_cuts.size(); ci++) {
		Dictionary cut = p_cuts[ci];
		if ((int)cut.get("parent_island", -1) != p_island_idx) {
			continue;
		}
		PackedVector2Array cut_verts = cut.get("vertices", PackedVector2Array());
		if (cut_verts.size() < 3) {
			continue;
		}
		Vector<PackedVector2Array> new_polygons;
		for (const PackedVector2Array &poly : polygons) {
			Vector<Vector<Point2>> clipped = Geometry2D::clip_polygons(poly, cut_verts);
			for (const Vector<Point2> &c : clipped) {
				new_polygons.push_back(c);
			}
		}
		polygons = new_polygons;
	}

	for (int si = 0; si < slope_in_clips.size(); si++) {
		PackedVector2Array clip_poly = slope_in_clips[si];
		if (clip_poly.size() < 3) {
			continue;
		}
		Vector<PackedVector2Array> clipped_polygons;
		for (const PackedVector2Array &poly : polygons) {
			Vector<Vector<Point2>> clipped = Geometry2D::clip_polygons(poly, clip_poly);
			for (const Vector<Point2> &c : clipped) {
				clipped_polygons.push_back(c);
			}
		}
		polygons = clipped_polygons;
	}

	// Ground surfaces.
	const String ground_key = _ground_key(p_island);
	double ground_uv_scale = 0.0;
	if (!_island_has_grass(p_island)) {
		const double grass_scale_cliff = MAX((double)p_island.get("grass_scale", 0.5), 0.01);
		ground_uv_scale = CLIFF_REPEAT_H_TILES * p_tws * grass_scale_cliff;
	}
	for (const PackedVector2Array &poly : polygons) {
		build_ground_surface(poly, y_height, p_tws, ground_key, ground_uv_scale);
	}

	// Cliff/slope edges.
	build_edge_geometry(p_island, verts, edge_types, elev, p_level_h, p_tws, p_all_islands, p_island_idx);

	// Cut depressions.
	for (int ci = 0; ci < p_cuts.size(); ci++) {
		Dictionary cut = p_cuts[ci];
		if ((int)cut.get("parent_island", -1) != p_island_idx) {
			continue;
		}
		PackedVector2Array cut_verts = cut.get("vertices", PackedVector2Array());
		const int cut_depth = cut.get("depth", 1);
		if (cut_verts.size() < 3) {
			continue;
		}
		build_cut_geometry(p_island, cut_verts, elev, cut_depth, p_level_h, p_tws);
	}

	return commit();
}

// Port of _append_surface_arrays.
void GlassTerrainMeshBuilder::_append_surface_arrays(Array &r_dst, const Array &p_src) {
	int vert_count = 0;
	if (r_dst[Mesh::ARRAY_VERTEX].get_type() == Variant::PACKED_VECTOR3_ARRAY) {
		vert_count = ((PackedVector3Array)r_dst[Mesh::ARRAY_VERTEX]).size();
	}
	const int count = MIN(r_dst.size(), p_src.size());
	for (int i = 0; i < count; i++) {
		if (p_src[i].get_type() == Variant::NIL) {
			continue;
		}
		if (r_dst[i].get_type() == Variant::NIL) {
			r_dst[i] = p_src[i];
			continue;
		}
		switch (r_dst[i].get_type()) {
			case Variant::PACKED_VECTOR3_ARRAY: {
				PackedVector3Array d = r_dst[i];
				d.append_array(p_src[i]);
				r_dst[i] = d;
			} break;
			case Variant::PACKED_VECTOR2_ARRAY: {
				PackedVector2Array d = r_dst[i];
				d.append_array(p_src[i]);
				r_dst[i] = d;
			} break;
			case Variant::PACKED_FLOAT32_ARRAY: {
				PackedFloat32Array d = r_dst[i];
				d.append_array(p_src[i]);
				r_dst[i] = d;
			} break;
			case Variant::PACKED_INT32_ARRAY: {
				if (i == Mesh::ARRAY_INDEX) {
					PackedInt32Array d = r_dst[i];
					PackedInt32Array s = p_src[i];
					PackedInt32Array offset;
					offset.resize(s.size());
					for (int j = 0; j < s.size(); j++) {
						offset.write[j] = s[j] + vert_count;
					}
					d.append_array(offset);
					r_dst[i] = d;
				} else {
					PackedInt32Array d = r_dst[i];
					d.append_array(p_src[i]);
					r_dst[i] = d;
				}
			} break;
			case Variant::PACKED_COLOR_ARRAY: {
				PackedColorArray d = r_dst[i];
				d.append_array(p_src[i]);
				r_dst[i] = d;
			} break;
			default:
				break;
		}
	}
}

// Port of merge_surface_caches.
Dictionary GlassTerrainMeshBuilder::merge_caches(const Array &p_island_caches) {
	// Collect texture keys in insertion order.
	Vector<String> all_keys;
	HashMap<String, bool> key_set;
	for (int ci = 0; ci < p_island_caches.size(); ci++) {
		Dictionary cache = p_island_caches[ci];
		LocalVector<Variant> keys = cache.get_key_list();
		for (const Variant &k : keys) {
			String ks = k;
			if (!key_set.has(ks)) {
				key_set.insert(ks, true);
				all_keys.push_back(ks);
			}
		}
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	PackedStringArray surface_textures;
	for (const String &tex_key : all_keys) {
		Array merged;
		for (int ci = 0; ci < p_island_caches.size(); ci++) {
			Dictionary cache = p_island_caches[ci];
			if (!cache.has(tex_key)) {
				continue;
			}
			Array arrays = cache[tex_key];
			if (merged.is_empty()) {
				merged = arrays.duplicate(true);
			} else {
				_append_surface_arrays(merged, arrays);
			}
		}
		if (!merged.is_empty()) {
			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, merged);
			surface_textures.push_back(tex_key);
		}
	}

	Dictionary result;
	result["mesh"] = mesh;
	result["surface_textures"] = surface_textures;
	return result;
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
	ClassDB::bind_method(D_METHOD("build_merged_slope", "island", "verts", "edge_types", "run", "y_top", "elev", "level_h", "tws", "all_islands", "vertex_perps"),
			&GlassTerrainMeshBuilder::build_merged_slope);
	ClassDB::bind_method(D_METHOD("build_merged_slope_in", "island", "verts", "edge_types", "run", "y_top", "elev", "level_h", "tws", "all_islands", "vertex_perps"),
			&GlassTerrainMeshBuilder::build_merged_slope_in);
	ClassDB::bind_method(D_METHOD("build_edge_geometry", "island", "verts", "edge_types", "elev", "level_h", "tws", "all_islands", "island_idx"),
			&GlassTerrainMeshBuilder::build_edge_geometry);
	ClassDB::bind_method(D_METHOD("build_cut_geometry", "island", "cut_verts", "parent_elev", "depth", "level_h", "tws"),
			&GlassTerrainMeshBuilder::build_cut_geometry);
	ClassDB::bind_method(D_METHOD("build_island_surfaces", "island", "cuts", "island_idx", "all_islands", "tws", "level_h"),
			&GlassTerrainMeshBuilder::build_island_surfaces);
	ClassDB::bind_method(D_METHOD("merge_caches", "island_caches"), &GlassTerrainMeshBuilder::merge_caches);
}
