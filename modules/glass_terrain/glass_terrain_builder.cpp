#include "glass_terrain_builder.h"

#include "core/object/class_db.h"

// Builds an n x n flat quad grid (y = 0) and returns it as a finished ArrayMesh.
// Each cell is 4 verts + 2 tris (6 indices), so the surface has n*n*4 verts and
// n*n*6 indices. Used to prove the GDScript<->C++ ArrayMesh round-trip.
Ref<ArrayMesh> GlassTerrainBuilder::build_test(int p_n) {
	PackedVector3Array verts;
	PackedVector3Array normals;
	PackedInt32Array indices;

	for (int z = 0; z < p_n; z++) {
		for (int x = 0; x < p_n; x++) {
			int base = verts.size();
			verts.push_back(Vector3(x, 0, z));
			verts.push_back(Vector3(x + 1, 0, z));
			verts.push_back(Vector3(x + 1, 0, z + 1));
			verts.push_back(Vector3(x, 0, z + 1));
			for (int i = 0; i < 4; i++) {
				normals.push_back(Vector3(0, 1, 0));
			}
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 0);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_INDEX] = indices;

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

void GlassTerrainBuilder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("build_test", "n"), &GlassTerrainBuilder::build_test);
}
