#include "glass_height_sampler.h"

#include "core/object/class_db.h"

const double GlassHeightSampler::NO_HIT = -1e18;

// Port of _build_face_grid: bucket every triangle into all XZ grid cells its
// bounding box overlaps. Grid stores the face start index (i, a multiple of 3).
void GlassHeightSampler::build(const PackedVector3Array &p_faces, double p_cell_size) {
	faces = p_faces;
	grid_cs = p_cell_size;
	grid.clear();
	if (p_cell_size <= 0.0) {
		return;
	}
	const double inv_cs = 1.0 / p_cell_size;
	const int count = faces.size();
	const Vector3 *r = faces.ptr();
	int i = 0;
	while (i + 2 < count) {
		const Vector3 &v0 = r[i];
		const Vector3 &v1 = r[i + 1];
		const Vector3 &v2 = r[i + 2];
		const double min_x = MIN(v0.x, MIN(v1.x, v2.x));
		const double max_x = MAX(v0.x, MAX(v1.x, v2.x));
		const double min_z = MIN(v0.z, MIN(v1.z, v2.z));
		const double max_z = MAX(v0.z, MAX(v1.z, v2.z));
		const int cx0 = (int)Math::floor(min_x * inv_cs);
		const int cx1 = (int)Math::floor(max_x * inv_cs);
		const int cz0 = (int)Math::floor(min_z * inv_cs);
		const int cz1 = (int)Math::floor(max_z * inv_cs);
		for (int cx = cx0; cx <= cx1; cx++) {
			for (int cz = cz0; cz <= cz1; cz++) {
				grid[_cell_key(cx, cz)].push_back(i);
			}
		}
		i += 3;
	}
}

// Port of _build_face_grid_bounded: only grid triangles overlapping `bounds`,
// and only into cells inside it.
void GlassHeightSampler::build_bounded(const PackedVector3Array &p_faces, double p_cell_size, const Rect2 &p_bounds) {
	faces = p_faces;
	grid_cs = p_cell_size;
	grid.clear();
	if (p_cell_size <= 0.0) {
		return;
	}
	const double inv_cs = 1.0 / p_cell_size;
	const double b_min_x = p_bounds.position.x;
	const double b_max_x = p_bounds.position.x + p_bounds.size.x;
	const double b_min_z = p_bounds.position.y;
	const double b_max_z = p_bounds.position.y + p_bounds.size.y;
	const int count = faces.size();
	const Vector3 *r = faces.ptr();
	int i = 0;
	while (i + 2 < count) {
		const Vector3 &v0 = r[i];
		const Vector3 &v1 = r[i + 1];
		const Vector3 &v2 = r[i + 2];
		const double min_x = MIN(v0.x, MIN(v1.x, v2.x));
		const double max_x = MAX(v0.x, MAX(v1.x, v2.x));
		const double min_z = MIN(v0.z, MIN(v1.z, v2.z));
		const double max_z = MAX(v0.z, MAX(v1.z, v2.z));
		if (max_x >= b_min_x && min_x <= b_max_x && max_z >= b_min_z && min_z <= b_max_z) {
			const int cx0 = (int)Math::floor(MAX(min_x, b_min_x) * inv_cs);
			const int cx1 = (int)Math::floor(MIN(max_x, b_max_x) * inv_cs);
			const int cz0 = (int)Math::floor(MAX(min_z, b_min_z) * inv_cs);
			const int cz1 = (int)Math::floor(MIN(max_z, b_max_z) * inv_cs);
			for (int cx = cx0; cx <= cx1; cx++) {
				for (int cz = cz0; cz <= cz1; cz++) {
					grid[_cell_key(cx, cz)].push_back(i);
				}
			}
		}
		i += 3;
	}
}

// Port of _sample_height PRIMARY path: barycentric XZ triangle test over the
// candidate faces in the point's grid cell; returns the highest hit's Y.
double GlassHeightSampler::sample(const Vector2 &p_xz) const {
	if (faces.size() < 3 || grid.is_empty()) {
		return NO_HIT;
	}
	const int cx = (int)Math::floor(p_xz.x / grid_cs);
	const int cz = (int)Math::floor(p_xz.y / grid_cs);
	const HashMap<int64_t, Vector<int>>::ConstIterator it = grid.find(_cell_key(cx, cz));
	if (!it) {
		return NO_HIT;
	}
	const Vector<int> &indices = it->value;
	const Vector3 *r = faces.ptr();
	double best_y = NO_HIT;
	const int n = indices.size();
	for (int k = 0; k < n; k++) {
		const int idx = indices[k];
		const Vector3 &v0 = r[idx];
		const Vector3 &v1 = r[idx + 1];
		const Vector3 &v2 = r[idx + 2];

		const double ax = v1.x - v0.x;
		const double az = v1.z - v0.z;
		const double bx = v2.x - v0.x;
		const double bz = v2.z - v0.z;
		const double denom = ax * bz - az * bx;
		if (Math::abs(denom) < 1e-6) {
			continue;
		}

		const double px = p_xz.x - v0.x;
		const double pz = p_xz.y - v0.z;
		const double inv = 1.0 / denom;
		const double u = (px * bz - pz * bx) * inv;
		const double v = (ax * pz - az * px) * inv;

		if (u >= -1e-5 && v >= -1e-5 && (u + v) <= 1.0 + 1e-5) {
			const double y = v0.y * (1.0 - u - v) + v1.y * u + v2.y * v;
			if (y > best_y) {
				best_y = y;
			}
		}
	}
	return best_y;
}

PackedFloat64Array GlassHeightSampler::sample_many(const PackedVector2Array &p_points) const {
	PackedFloat64Array out;
	const int n = p_points.size();
	out.resize(n);
	const Vector2 *pr = p_points.ptr();
	double *wr = out.ptrw();
	for (int i = 0; i < n; i++) {
		wr[i] = sample(pr[i]);
	}
	return out;
}

void GlassHeightSampler::_bind_methods() {
	ClassDB::bind_method(D_METHOD("build", "faces", "cell_size"), &GlassHeightSampler::build);
	ClassDB::bind_method(D_METHOD("build_bounded", "faces", "cell_size", "bounds"), &GlassHeightSampler::build_bounded);
	ClassDB::bind_method(D_METHOD("sample", "xz"), &GlassHeightSampler::sample);
	ClassDB::bind_method(D_METHOD("sample_many", "points"), &GlassHeightSampler::sample_many);
	ClassDB::bind_method(D_METHOD("face_count"), &GlassHeightSampler::face_count);
}
