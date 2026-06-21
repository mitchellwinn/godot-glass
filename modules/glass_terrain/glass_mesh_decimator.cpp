#include "glass_mesh_decimator.h"

#include "core/math/geometry_2d.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "scene/resources/mesh.h"

// Constants (1:1 with mesh_decimator.gd).
static const double POS_SNAP = 0.001;
static const double UV_SNAP = 0.0001;
static const double PLANAR_NORMAL_THRESHOLD = 0.99939; // cos(2 deg)
static const double PLANAR_DIST_THRESHOLD = 0.001;
static const double UV_GRADIENT_THRESHOLD = 0.01;
static const double BOUNDARY_COST = 1e6;

namespace {

struct Quad {
	double q[10];
};

static Quad quad_zero() {
	Quad z;
	for (int i = 0; i < 10; i++) {
		z.q[i] = 0.0;
	}
	return z;
}

struct UVGrad {
	bool valid = false;
	Vector2 du;
	Vector2 dv;
};

struct HeapEntry {
	double cost;
	Vector2i edge;
	int gen;
};

struct Indexed {
	PackedVector3Array verts;
	PackedVector2Array uvs;
	Vector<Vector3i> faces;
};

// ── Shared infrastructure ──

static String weld_key(const Vector3 &pos, const Vector2 &uv) {
	return vformat("%d,%d,%d,%d,%d",
			(int)Math::round(pos.x / POS_SNAP),
			(int)Math::round(pos.y / POS_SNAP),
			(int)Math::round(pos.z / POS_SNAP),
			(int)Math::round(uv.x / UV_SNAP),
			(int)Math::round(uv.y / UV_SNAP));
}

static Vector3i pos_key(const Vector3 &p) {
	return Vector3i(
			(int32_t)Math::round(p.x / POS_SNAP),
			(int32_t)Math::round(p.y / POS_SNAP),
			(int32_t)Math::round(p.z / POS_SNAP));
}

static Indexed build_indexed(const PackedVector3Array &verts, const PackedVector2Array &uvs) {
	HashMap<String, int> weld_map;
	Indexed out;
	const int n = verts.size();
	PackedInt32Array remap;
	remap.resize(n);
	for (int i = 0; i < n; i++) {
		String key = weld_key(verts[i], uvs[i]);
		HashMap<String, int>::Iterator it = weld_map.find(key);
		if (it) {
			remap.write[i] = it->value;
		} else {
			int idx = out.verts.size();
			weld_map.insert(key, idx);
			out.verts.push_back(verts[i]);
			out.uvs.push_back(uvs[i]);
			remap.write[i] = idx;
		}
	}
	const int tri_count = n / 3;
	for (int t = 0; t < tri_count; t++) {
		int i0 = remap[t * 3];
		int i1 = remap[t * 3 + 1];
		int i2 = remap[t * 3 + 2];
		if (i0 != i1 && i1 != i2 && i0 != i2) {
			out.faces.push_back(Vector3i(i0, i1, i2));
		}
	}
	return out;
}

static void face_edges(const Vector3i &f, Vector2i r_out[3]) {
	r_out[0] = Vector2i(MIN(f.x, f.y), MAX(f.x, f.y));
	r_out[1] = Vector2i(MIN(f.y, f.z), MAX(f.y, f.z));
	r_out[2] = Vector2i(MIN(f.x, f.z), MAX(f.x, f.z));
}

static HashMap<Vector2i, Vector<int>> build_adjacency(const Vector<Vector3i> &faces) {
	HashMap<Vector2i, Vector<int>> adj;
	for (int fi = 0; fi < faces.size(); fi++) {
		Vector2i edges[3];
		face_edges(faces[fi], edges);
		for (int e = 0; e < 3; e++) {
			adj[edges[e]].push_back(fi);
		}
	}
	return adj;
}

static PackedVector3Array compute_face_normals(const Vector<Vector3i> &faces, const PackedVector3Array &verts) {
	PackedVector3Array normals;
	normals.resize(faces.size());
	for (int i = 0; i < faces.size(); i++) {
		const Vector3i &f = faces[i];
		Vector3 e1 = verts[f.y] - verts[f.x];
		Vector3 e2 = verts[f.z] - verts[f.x];
		normals.write[i] = e1.cross(e2).normalized();
	}
	return normals;
}

static Vector3 pick_normal(const Vector3 &p, const HashMap<Vector3i, Vector3> &norm_by_pos, const Vector3 &fallback) {
	if (!norm_by_pos.is_empty()) {
		Vector3i k = pos_key(p);
		HashMap<Vector3i, Vector3>::ConstIterator it = norm_by_pos.find(k);
		if (it) {
			Vector3 nv = it->value;
			if (nv.length() > 1e-6) {
				return nv.normalized();
			}
		}
	}
	return fallback;
}

static Array rebuild_soup(const Vector<Vector3i> &faces, const PackedVector3Array &w_verts, const PackedVector2Array &w_uvs, const HashMap<Vector3i, Vector3> &norm_by_pos) {
	int vert_count = faces.size() * 3;
	PackedVector3Array out_verts;
	PackedVector3Array out_normals;
	PackedVector2Array out_uvs;
	out_verts.resize(vert_count);
	out_normals.resize(vert_count);
	out_uvs.resize(vert_count);

	for (int i = 0; i < faces.size(); i++) {
		const Vector3i &f = faces[i];
		Vector3 v0 = w_verts[f.x];
		Vector3 v1 = w_verts[f.y];
		Vector3 v2 = w_verts[f.z];
		Vector3 fallback = (v1 - v0).cross(v2 - v0).normalized();
		int base = i * 3;
		out_verts.write[base] = v0;
		out_verts.write[base + 1] = v1;
		out_verts.write[base + 2] = v2;
		out_normals.write[base] = pick_normal(v0, norm_by_pos, fallback);
		out_normals.write[base + 1] = pick_normal(v1, norm_by_pos, fallback);
		out_normals.write[base + 2] = pick_normal(v2, norm_by_pos, fallback);
		out_uvs.write[base] = w_uvs[f.x];
		out_uvs.write[base + 1] = w_uvs[f.y];
		out_uvs.write[base + 2] = w_uvs[f.z];
	}

	Array result;
	result.resize(Mesh::ARRAY_MAX);
	result[Mesh::ARRAY_VERTEX] = out_verts;
	result[Mesh::ARRAY_NORMAL] = out_normals;
	result[Mesh::ARRAY_TEX_UV] = out_uvs;
	return result;
}

// ── Pass 1: planar decimation ──

static UVGrad compute_uv_gradient(const Vector3i &face, const PackedVector3Array &verts, const PackedVector2Array &uvs) {
	Vector3 v0 = verts[face.x];
	Vector3 v1 = verts[face.y];
	Vector3 v2 = verts[face.z];
	Vector2 uv0 = uvs[face.x];
	Vector2 uv1 = uvs[face.y];
	Vector2 uv2 = uvs[face.z];

	Vector3 e1 = v1 - v0;
	Vector3 e2 = v2 - v0;
	Vector2 duv1 = uv1 - uv0;
	Vector2 duv2 = uv2 - uv0;

	UVGrad g;
	double det = e1.x * e2.z - e2.x * e1.z;
	if (Math::abs(det) < 1e-8) {
		det = e1.y * e2.z - e2.y * e1.z;
		if (Math::abs(det) < 1e-8) {
			g.valid = false;
			return g;
		}
		double inv_det = 1.0 / det;
		g.valid = true;
		g.du = Vector2(e2.z * duv1.x - e1.z * duv2.x, e2.z * duv1.y - e1.z * duv2.y) * inv_det;
		g.dv = Vector2(-e2.y * duv1.x + e1.y * duv2.x, -e2.y * duv1.y + e1.y * duv2.y) * inv_det;
		return g;
	}
	double inv_det = 1.0 / det;
	g.valid = true;
	g.du = Vector2(e2.z * duv1.x - e1.z * duv2.x, e2.z * duv1.y - e1.z * duv2.y) * inv_det;
	g.dv = Vector2(-e2.x * duv1.x + e1.x * duv2.x, -e2.x * duv1.y + e1.x * duv2.y) * inv_det;
	return g;
}

static bool uv_gradients_match(const UVGrad &a, const UVGrad &b) {
	return (a.du - b.du).length() < UV_GRADIENT_THRESHOLD && (a.dv - b.dv).length() < UV_GRADIENT_THRESHOLD;
}

// BFS/DFS flood fill into coplanar, UV-compatible groups.
static Vector<PackedInt32Array> find_coplanar_groups(const Vector<Vector3i> &faces, const PackedVector3Array &verts, const PackedVector2Array &uvs, const PackedVector3Array &normals, const HashMap<Vector2i, Vector<int>> &adj) {
	HashMap<int, bool> visited;
	Vector<PackedInt32Array> groups;

	for (int fi = 0; fi < faces.size(); fi++) {
		if (visited.has(fi)) {
			continue;
		}
		PackedInt32Array group;
		Vector<int> queue;
		queue.push_back(fi);
		visited.insert(fi, true);
		Vector3 ref_normal = normals[fi];
		Vector3 ref_point = verts[faces[fi].x];
		UVGrad ref_grad = compute_uv_gradient(faces[fi], verts, uvs);

		while (!queue.is_empty()) {
			int current = queue[queue.size() - 1];
			queue.remove_at(queue.size() - 1);
			group.push_back(current);
			const Vector3i &f = faces[current];

			Vector2i edges[3];
			face_edges(f, edges);
			for (int e = 0; e < 3; e++) {
				HashMap<Vector2i, Vector<int>>::ConstIterator it = adj.find(edges[e]);
				if (!it) {
					continue;
				}
				const Vector<int> &nbrs = it->value;
				for (int ni = 0; ni < nbrs.size(); ni++) {
					int neighbor_fi = nbrs[ni];
					if (visited.has(neighbor_fi)) {
						continue;
					}
					if (ref_normal.dot(normals[neighbor_fi]) < PLANAR_NORMAL_THRESHOLD) {
						continue;
					}
					Vector3 neighbor_point = verts[faces[neighbor_fi].x];
					if (Math::abs((neighbor_point - ref_point).dot(ref_normal)) > PLANAR_DIST_THRESHOLD) {
						continue;
					}
					if (ref_grad.valid) {
						UVGrad n_grad = compute_uv_gradient(faces[neighbor_fi], verts, uvs);
						if (!n_grad.valid || !uv_gradients_match(ref_grad, n_grad)) {
							continue;
						}
					}
					visited.insert(neighbor_fi, true);
					queue.push_back(neighbor_fi);
				}
			}
		}
		groups.push_back(group);
	}
	return groups;
}

static int get_drop_axis(const Vector3 &normal) {
	Vector3 abs_n = normal.abs();
	if (abs_n.x >= abs_n.y && abs_n.x >= abs_n.z) {
		return 0;
	} else if (abs_n.y >= abs_n.z) {
		return 1;
	}
	return 2;
}

static Vector2 project_to_2d(const Vector3 &v, int drop_axis) {
	switch (drop_axis) {
		case 0:
			return Vector2(v.y, v.z);
		case 1:
			return Vector2(v.x, v.z);
		default:
			return Vector2(v.x, v.y);
	}
}

static double loop_area_2d(const PackedInt32Array &loop, const PackedVector3Array &verts, int drop_axis) {
	double area = 0.0;
	int n = loop.size();
	for (int i = 0; i < n; i++) {
		Vector2 a = project_to_2d(verts[loop[i]], drop_axis);
		Vector2 b = project_to_2d(verts[loop[(i + 1) % n]], drop_axis);
		area += a.x * b.y - b.x * a.y;
	}
	return area * 0.5;
}

static Vector<PackedInt32Array> build_boundary_loops(const Vector<Vector2i> &edges) {
	HashMap<int, int> next_map;
	for (int i = 0; i < edges.size(); i++) {
		next_map[edges[i].x] = edges[i].y;
	}
	HashMap<int, bool> visited;
	Vector<PackedInt32Array> loops;
	for (int i = 0; i < edges.size(); i++) {
		int start = edges[i].x;
		if (visited.has(start)) {
			continue;
		}
		PackedInt32Array loop;
		int current = start;
		int safety = 0;
		while (true) {
			if (visited.has(current)) {
				break;
			}
			visited.insert(current, true);
			loop.push_back(current);
			HashMap<int, int>::ConstIterator it = next_map.find(current);
			if (!it) {
				break;
			}
			current = it->value;
			safety += 1;
			if (current == start || safety > 10000) {
				break;
			}
		}
		if (loop.size() >= 3) {
			loops.push_back(loop);
		}
	}
	return loops;
}

static PackedInt32Array bridge_holes(const PackedInt32Array &outer, const Vector<PackedInt32Array> &holes, const PackedVector3Array &verts, int drop_axis) {
	PackedInt32Array result = outer;
	for (int h = 0; h < holes.size(); h++) {
		const PackedInt32Array &hole_loop = holes[h];
		double best_dist = INFINITY;
		int best_outer_idx = 0;
		int best_hole_idx = 0;
		for (int hi = 0; hi < hole_loop.size(); hi++) {
			Vector2 hp = project_to_2d(verts[hole_loop[hi]], drop_axis);
			for (int oi = 0; oi < result.size(); oi++) {
				Vector2 op = project_to_2d(verts[result[oi]], drop_axis);
				double d = hp.distance_squared_to(op);
				if (d < best_dist) {
					best_dist = d;
					best_outer_idx = oi;
					best_hole_idx = hi;
				}
			}
		}
		PackedInt32Array bridged;
		for (int i = 0; i < best_outer_idx + 1; i++) {
			bridged.push_back(result[i]);
		}
		for (int i = 0; i < hole_loop.size() + 1; i++) {
			bridged.push_back(hole_loop[(best_hole_idx + i) % hole_loop.size()]);
		}
		bridged.push_back(result[best_outer_idx]);
		for (int i = best_outer_idx + 1; i < result.size(); i++) {
			bridged.push_back(result[i]);
		}
		result = bridged;
	}
	return result;
}

static Vector<Vector3i> retriangulate_group(const PackedInt32Array &group, const Vector<Vector3i> &faces, const PackedVector3Array &verts, const PackedVector3Array &normals, const HashMap<Vector2i, Vector<int>> &adjacency) {
	Vector<Vector3i> empty;
	Vector3 group_normal = normals[group[0]];

	HashMap<int, bool> group_set;
	for (int i = 0; i < group.size(); i++) {
		group_set.insert(group[i], true);
	}

	Vector<Vector2i> boundary_edges;
	for (int gi = 0; gi < group.size(); gi++) {
		int fi = group[gi];
		const Vector3i &f = faces[fi];
		int fv[3] = { f.x, f.y, f.z };
		for (int ei = 0; ei < 3; ei++) {
			int a = fv[ei];
			int b = fv[(ei + 1) % 3];
			Vector2i sorted_edge(MIN(a, b), MAX(a, b));
			bool is_boundary = true;
			HashMap<Vector2i, Vector<int>>::ConstIterator it = adjacency.find(sorted_edge);
			if (it) {
				const Vector<int> &nbrs = it->value;
				for (int ni = 0; ni < nbrs.size(); ni++) {
					int neighbor_fi = nbrs[ni];
					if (neighbor_fi != fi && group_set.has(neighbor_fi)) {
						is_boundary = false;
						break;
					}
				}
			}
			if (is_boundary) {
				boundary_edges.push_back(Vector2i(a, b));
			}
		}
	}

	if (boundary_edges.is_empty()) {
		return empty;
	}
	Vector<PackedInt32Array> loops = build_boundary_loops(boundary_edges);
	if (loops.is_empty()) {
		return empty;
	}

	int drop_axis = get_drop_axis(group_normal);
	int outer_idx = 0;
	double outer_area = 0.0;
	for (int li = 0; li < loops.size(); li++) {
		double area = loop_area_2d(loops[li], verts, drop_axis);
		if (Math::abs(area) > Math::abs(outer_area)) {
			outer_area = area;
			outer_idx = li;
		}
	}

	PackedInt32Array outer_loop = loops[outer_idx];
	if (outer_area < 0.0) {
		outer_loop.reverse();
	}

	Vector<PackedInt32Array> holes;
	for (int li = 0; li < loops.size(); li++) {
		if (li == outer_idx) {
			continue;
		}
		PackedInt32Array hole = loops[li];
		double area = loop_area_2d(hole, verts, drop_axis);
		if (area > 0.0) {
			hole.reverse();
		}
		holes.push_back(hole);
	}

	PackedInt32Array polygon_verts = outer_loop;
	if (!holes.is_empty()) {
		polygon_verts = bridge_holes(outer_loop, holes, verts, drop_axis);
	}

	PackedVector2Array pts_2d;
	pts_2d.resize(polygon_verts.size());
	for (int i = 0; i < polygon_verts.size(); i++) {
		pts_2d.write[i] = project_to_2d(verts[polygon_verts[i]], drop_axis);
	}

	Vector<int> tri_indices = Geometry2D::triangulate_polygon(pts_2d);
	if (tri_indices.is_empty()) {
		return empty;
	}

	Vector<Vector3i> result;
	int tri_count = tri_indices.size() / 3;
	for (int t = 0; t < tri_count; t++) {
		int i0 = polygon_verts[tri_indices[t * 3]];
		int i1 = polygon_verts[tri_indices[t * 3 + 1]];
		int i2 = polygon_verts[tri_indices[t * 3 + 2]];
		if (i0 != i1 && i1 != i2 && i0 != i2) {
			result.push_back(Vector3i(i0, i1, i2));
		}
	}

	if (result.size() >= group.size()) {
		return empty;
	}
	return result;
}

static Vector<Vector3i> planar_decimate(const Vector<Vector3i> &faces, const PackedVector3Array &verts, const PackedVector2Array &uvs, const HashMap<Vector2i, Vector<int>> &adjacency, const PackedVector3Array &normals) {
	Vector<PackedInt32Array> groups = find_coplanar_groups(faces, verts, uvs, normals, adjacency);

	HashMap<int, bool> in_group;
	Vector<Vector3i> new_faces;

	for (int gi = 0; gi < groups.size(); gi++) {
		const PackedInt32Array &group = groups[gi];
		if (group.size() < 2) {
			continue;
		}
		for (int i = 0; i < group.size(); i++) {
			in_group.insert(group[i], true);
		}
		Vector<Vector3i> re_tris = retriangulate_group(group, faces, verts, normals, adjacency);
		if (re_tris.is_empty()) {
			for (int i = 0; i < group.size(); i++) {
				in_group.erase(group[i]);
			}
		} else {
			for (int i = 0; i < re_tris.size(); i++) {
				new_faces.push_back(re_tris[i]);
			}
		}
	}

	for (int fi = 0; fi < faces.size(); fi++) {
		if (!in_group.has(fi)) {
			new_faces.push_back(faces[fi]);
		}
	}
	return new_faces;
}

// ── Pass 2: QEM edge collapse ──

static Quad make_plane_quadric(const Vector3 &normal, const Vector3 &point) {
	double a = normal.x;
	double b = normal.y;
	double c = normal.z;
	double d = -normal.dot(point);
	Quad q;
	q.q[0] = a * a;
	q.q[1] = a * b;
	q.q[2] = a * c;
	q.q[3] = a * d;
	q.q[4] = b * b;
	q.q[5] = b * c;
	q.q[6] = b * d;
	q.q[7] = c * c;
	q.q[8] = c * d;
	q.q[9] = d * d;
	return q;
}

static Quad add_quadrics(const Quad &a, const Quad &b) {
	Quad q;
	for (int i = 0; i < 10; i++) {
		q.q[i] = a.q[i] + b.q[i];
	}
	return q;
}

static double eval_quadric(const Quad &q, const Vector3 &pos) {
	double x = (double)pos.x;
	double y = (double)pos.y;
	double z = (double)pos.z;
	return (q.q[0] * x * x + 2.0 * q.q[1] * x * y + 2.0 * q.q[2] * x * z + 2.0 * q.q[3] * x + q.q[4] * y * y + 2.0 * q.q[5] * y * z + 2.0 * q.q[6] * y + q.q[7] * z * z + 2.0 * q.q[8] * z + q.q[9]);
}

static bool optimal_pos(const Quad &q, Vector3 &r_out) {
	double a00 = q.q[0], a01 = q.q[1], a02 = q.q[2];
	double a11 = q.q[4], a12 = q.q[5];
	double a22 = q.q[7];
	double b0 = -q.q[3], b1 = -q.q[6], b2 = -q.q[8];

	double det = (a00 * (a11 * a22 - a12 * a12) - a01 * (a01 * a22 - a12 * a02) + a02 * (a01 * a12 - a11 * a02));
	if (Math::abs(det) < 1e-10) {
		return false;
	}
	double inv_det = 1.0 / det;
	double x = ((a11 * a22 - a12 * a12) * b0 + (a02 * a12 - a01 * a22) * b1 + (a01 * a12 - a02 * a11) * b2) * inv_det;
	double y = ((a02 * a12 - a01 * a22) * b0 + (a00 * a22 - a02 * a02) * b1 + (a01 * a02 - a00 * a12) * b2) * inv_det;
	double z = ((a01 * a12 - a02 * a11) * b0 + (a01 * a02 - a00 * a12) * b1 + (a00 * a11 - a01 * a01) * b2) * inv_det;
	r_out = Vector3(x, y, z);
	return true;
}

static double compute_collapse_cost(const Vector2i &edge, const PackedVector3Array &verts, const Vector<Quad> &quadrics, const HashMap<int, bool> &boundary_verts) {
	if (boundary_verts.has(edge.x) && boundary_verts.has(edge.y)) {
		return BOUNDARY_COST;
	}
	Quad q_merged = add_quadrics(quadrics[edge.x], quadrics[edge.y]);
	Vector3 pos;
	if (!optimal_pos(q_merged, pos)) {
		pos = (verts[edge.x] + verts[edge.y]) * 0.5;
	}
	double cost = eval_quadric(q_merged, pos);
	if (boundary_verts.has(edge.x) || boundary_verts.has(edge.y)) {
		cost += BOUNDARY_COST * 0.1;
	}
	return cost;
}

static bool collapse_would_flip(int v1, int v2, const Vector3 &new_pos, const PackedVector3Array &verts, const Vector<Vector3i> &face_list, const Vector<Vector<int>> &vert_faces) {
	Vector<int> check_faces;
	const Vector<int> &v1_faces = vert_faces[v1];
	const Vector<int> &v2_faces = vert_faces[v2];
	for (int i = 0; i < v1_faces.size(); i++) {
		check_faces.push_back(v1_faces[i]);
	}
	for (int i = 0; i < v2_faces.size(); i++) {
		check_faces.push_back(v2_faces[i]);
	}
	for (int ci = 0; ci < check_faces.size(); ci++) {
		int fi = check_faces[ci];
		const Vector3i &f = face_list[fi];
		if (f.x < 0) {
			continue; // deleted
		}
		int fx = f.x, fy = f.y, fz = f.z;
		bool touches_v1 = (fx == v1 || fy == v1 || fz == v1);
		bool touches_v2 = (fx == v2 || fy == v2 || fz == v2);
		if (touches_v1 && touches_v2) {
			continue;
		}
		Vector3 a = verts[fx];
		Vector3 b = verts[fy];
		Vector3 c = verts[fz];
		Vector3 old_normal = (b - a).cross(c - a);
		if (old_normal.length_squared() < 1e-10) {
			continue;
		}
		Vector3 na = (fx == v1 || fx == v2) ? new_pos : a;
		Vector3 nb = (fy == v1 || fy == v2) ? new_pos : b;
		Vector3 nc = (fz == v1 || fz == v2) ? new_pos : c;
		Vector3 new_normal = (nb - na).cross(nc - na);
		if (new_normal.length_squared() < 1e-10) {
			return true;
		}
		if (old_normal.dot(new_normal) < 0.0) {
			return true;
		}
	}
	return false;
}

static void heap_push(Vector<HeapEntry> &heap, double cost, const Vector2i &edge, int generation) {
	HeapEntry e;
	e.cost = cost;
	e.edge = edge;
	e.gen = generation;
	heap.push_back(e);
	int i = heap.size() - 1;
	while (i > 0) {
		int parent = (i - 1) / 2;
		if (heap[parent].cost > heap[i].cost) {
			HeapEntry tmp = heap[parent];
			heap.write[parent] = heap[i];
			heap.write[i] = tmp;
			i = parent;
		} else {
			break;
		}
	}
}

static HeapEntry heap_pop(Vector<HeapEntry> &heap) {
	if (heap.size() == 1) {
		HeapEntry r = heap[0];
		heap.remove_at(heap.size() - 1);
		return r;
	}
	HeapEntry result = heap[0];
	HeapEntry last = heap[heap.size() - 1];
	heap.remove_at(heap.size() - 1);
	heap.write[0] = last;
	int i = 0;
	int n = heap.size();
	while (true) {
		int left = 2 * i + 1;
		int right = 2 * i + 2;
		int smallest = i;
		if (left < n && heap[left].cost < heap[smallest].cost) {
			smallest = left;
		}
		if (right < n && heap[right].cost < heap[smallest].cost) {
			smallest = right;
		}
		if (smallest == i) {
			break;
		}
		HeapEntry tmp = heap[smallest];
		heap.write[smallest] = heap[i];
		heap.write[i] = tmp;
		i = smallest;
	}
	return result;
}

struct QemResult {
	Vector<Vector3i> faces;
	PackedVector3Array verts;
	PackedVector2Array uvs;
};

static QemResult qem_decimate(const Vector<Vector3i> &faces, const PackedVector3Array &verts, const PackedVector2Array &uvs, const HashMap<Vector2i, Vector<int>> &adjacency, double threshold) {
	int vert_count = verts.size();

	PackedVector3Array m_verts = verts;
	PackedVector2Array m_uvs = uvs;

	PackedVector3Array face_normals = compute_face_normals(faces, m_verts);
	Vector<Quad> vert_quadrics;
	vert_quadrics.resize(vert_count);
	for (int i = 0; i < vert_count; i++) {
		vert_quadrics.write[i] = quad_zero();
	}

	Vector<Vector<int>> vert_faces;
	vert_faces.resize(vert_count);

	for (int fi = 0; fi < faces.size(); fi++) {
		const Vector3i &f = faces[fi];
		Vector3 n = face_normals[fi];
		if (n.length_squared() < 0.001) {
			continue;
		}
		Quad plane_q = make_plane_quadric(n, m_verts[f.x]);
		vert_quadrics.write[f.x] = add_quadrics(vert_quadrics[f.x], plane_q);
		vert_quadrics.write[f.y] = add_quadrics(vert_quadrics[f.y], plane_q);
		vert_quadrics.write[f.z] = add_quadrics(vert_quadrics[f.z], plane_q);
		vert_faces.write[f.x].push_back(fi);
		vert_faces.write[f.y].push_back(fi);
		vert_faces.write[f.z].push_back(fi);
	}

	HashMap<int, bool> boundary_verts;
	for (const KeyValue<Vector2i, Vector<int>> &kv : adjacency) {
		if (kv.value.size() == 1) {
			boundary_verts[kv.key.x] = true;
			boundary_verts[kv.key.y] = true;
		}
	}

	Vector<HeapEntry> heap;
	HashMap<int, bool> deleted_verts;
	HashMap<Vector2i, int> edge_generation;

	for (const KeyValue<Vector2i, Vector<int>> &kv : adjacency) {
		const Vector2i &edge = kv.key;
		edge_generation[edge] = 0;
		double cost = compute_collapse_cost(edge, m_verts, vert_quadrics, boundary_verts);
		heap_push(heap, cost, edge, 0);
	}

	Vector<Vector3i> m_faces = faces;

	while (!heap.is_empty()) {
		HeapEntry entry = heap_pop(heap);
		double cost = entry.cost;
		Vector2i edge = entry.edge;
		int gen = entry.gen;

		if (cost > threshold) {
			break;
		}
		HashMap<Vector2i, int>::ConstIterator git = edge_generation.find(edge);
		if (git && git->value != gen) {
			continue;
		}
		if (deleted_verts.has(edge.x) || deleted_verts.has(edge.y)) {
			continue;
		}

		int v1 = edge.x;
		int v2 = edge.y;

		Quad q_merged = add_quadrics(vert_quadrics[v1], vert_quadrics[v2]);
		Vector3 midpoint = (m_verts[v1] + m_verts[v2]) * 0.5;
		double edge_len = m_verts[v1].distance_to(m_verts[v2]);
		Vector3 opt;
		bool has_opt = optimal_pos(q_merged, opt);
		Vector3 new_pos;
		if (has_opt && opt.distance_to(midpoint) <= edge_len) {
			new_pos = opt;
		} else {
			double err_v1 = eval_quadric(q_merged, m_verts[v1]);
			double err_v2 = eval_quadric(q_merged, m_verts[v2]);
			double err_mid = eval_quadric(q_merged, midpoint);
			if (err_v1 <= err_v2 && err_v1 <= err_mid) {
				new_pos = m_verts[v1];
			} else if (err_v2 <= err_mid) {
				new_pos = m_verts[v2];
			} else {
				new_pos = midpoint;
			}
		}

		if (collapse_would_flip(v1, v2, new_pos, m_verts, m_faces, vert_faces)) {
			continue;
		}

		Vector3 old_v1 = m_verts[v1];
		Vector3 old_v2 = m_verts[v2];
		m_verts.write[v1] = new_pos;
		double uv_edge_len = old_v1.distance_to(old_v2);
		if (uv_edge_len > 1e-8) {
			double t = CLAMP(old_v1.distance_to(new_pos) / uv_edge_len, 0.0, 1.0);
			m_uvs.write[v1] = m_uvs[v1].lerp(m_uvs[v2], t);
		}
		vert_quadrics.write[v1] = q_merged;
		deleted_verts[v2] = true;

		HashMap<Vector2i, bool> affected_edges;
		const Vector<int> &v2f = vert_faces[v2];
		for (int idx = 0; idx < v2f.size(); idx++) {
			int fi = v2f[idx];
			Vector3i f = m_faces[fi];
			if (f.x < 0) {
				continue;
			}
			Vector3i new_f(
					f.x == v2 ? v1 : f.x,
					f.y == v2 ? v1 : f.y,
					f.z == v2 ? v1 : f.z);
			if (new_f.x == new_f.y || new_f.y == new_f.z || new_f.x == new_f.z) {
				m_faces.write[fi] = Vector3i(-1, -1, -1);
			} else {
				m_faces.write[fi] = new_f;
				Vector2i nedges[3];
				face_edges(new_f, nedges);
				for (int e = 0; e < 3; e++) {
					affected_edges[nedges[e]] = true;
				}
			}
			bool present = false;
			const Vector<int> &v1f = vert_faces[v1];
			for (int q = 0; q < v1f.size(); q++) {
				if (v1f[q] == fi) {
					present = true;
					break;
				}
			}
			if (!present) {
				vert_faces.write[v1].push_back(fi);
			}
		}

		for (const KeyValue<Vector2i, bool> &kv : affected_edges) {
			const Vector2i &e = kv.key;
			if (deleted_verts.has(e.x) || deleted_verts.has(e.y)) {
				continue;
			}
			int new_gen = 0;
			HashMap<Vector2i, int>::ConstIterator it = edge_generation.find(e);
			if (it) {
				new_gen = it->value + 1;
			} else {
				new_gen = 1;
			}
			edge_generation[e] = new_gen;
			double cost2 = compute_collapse_cost(e, m_verts, vert_quadrics, boundary_verts);
			heap_push(heap, cost2, e, new_gen);
		}
	}

	QemResult out;
	for (int fi = 0; fi < m_faces.size(); fi++) {
		Vector3i f = m_faces[fi];
		if (f.x < 0) {
			continue;
		}
		if (deleted_verts.has(f.x) || deleted_verts.has(f.y) || deleted_verts.has(f.z)) {
			continue;
		}
		out.faces.push_back(f);
	}
	out.verts = m_verts;
	out.uvs = m_uvs;
	return out;
}

} // namespace

// Port of decimate_surface.
Array GlassMeshDecimator::decimate_surface(const Array &p_arrays, bool p_use_qem, double p_error_threshold) {
	PackedVector3Array verts = p_arrays[Mesh::ARRAY_VERTEX];
	PackedVector2Array uvs;
	if (p_arrays[Mesh::ARRAY_TEX_UV].get_type() != Variant::NIL) {
		uvs = p_arrays[Mesh::ARRAY_TEX_UV];
	}
	PackedVector3Array in_normals;
	if (p_arrays[Mesh::ARRAY_NORMAL].get_type() != Variant::NIL) {
		in_normals = p_arrays[Mesh::ARRAY_NORMAL];
	}

	if (verts.size() < 9) {
		return p_arrays;
	}
	if (uvs.size() != verts.size()) {
		uvs.resize(verts.size());
	}

	HashMap<Vector3i, Vector3> norm_by_pos;
	if (in_normals.size() == verts.size()) {
		for (int i = 0; i < verts.size(); i++) {
			Vector3i k = pos_key(verts[i]);
			HashMap<Vector3i, Vector3>::Iterator it = norm_by_pos.find(k);
			if (it) {
				it->value = it->value + in_normals[i];
			} else {
				norm_by_pos.insert(k, in_normals[i]);
			}
		}
	}

	Indexed indexed = build_indexed(verts, uvs);
	PackedVector3Array w_verts = indexed.verts;
	PackedVector2Array w_uvs = indexed.uvs;
	Vector<Vector3i> faces = indexed.faces;

	if (faces.is_empty()) {
		return p_arrays;
	}

	HashMap<Vector2i, Vector<int>> adjacency = build_adjacency(faces);
	PackedVector3Array normals = compute_face_normals(faces, w_verts);

	Vector<Vector3i> planar_faces = planar_decimate(faces, w_verts, w_uvs, adjacency, normals);

	Vector<Vector3i> final_faces = planar_faces;
	PackedVector3Array final_verts = w_verts;
	PackedVector2Array final_uvs = w_uvs;
	if (p_use_qem && planar_faces.size() > 4) {
		HashMap<Vector2i, Vector<int>> qem_adj = build_adjacency(planar_faces);
		QemResult qr = qem_decimate(planar_faces, w_verts, w_uvs, qem_adj, p_error_threshold);
		final_faces = qr.faces;
		final_verts = qr.verts;
		final_uvs = qr.uvs;
	}

	return rebuild_soup(final_faces, final_verts, final_uvs, norm_by_pos);
}

void GlassMeshDecimator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("decimate_surface", "arrays", "use_qem", "error_threshold"),
			&GlassMeshDecimator::decimate_surface, DEFVAL(true), DEFVAL(0.1));
}
