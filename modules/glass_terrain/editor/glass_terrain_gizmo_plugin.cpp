#include "glass_terrain_gizmo_plugin.h"

#include "../glass_terrain.h"

GlassTerrainGizmoPlugin::GlassTerrainGizmoPlugin() {
	create_material("terrain_outline", Color(0.45, 0.85, 1.0));
}

bool GlassTerrainGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<GlassTerrain>(p_spatial) != nullptr;
}

String GlassTerrainGizmoPlugin::get_gizmo_name() const {
	return "GlassTerrain";
}

int GlassTerrainGizmoPlugin::get_priority() const {
	return -1;
}

void GlassTerrainGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	GlassTerrain *terrain = Object::cast_to<GlassTerrain>(p_gizmo->get_node_3d());
	if (!terrain) {
		return;
	}
	p_gizmo->clear();

	const double level_height = terrain->get_level_height();
	const TypedArray<Dictionary> islands = terrain->get_islands();

	// Each island contributes one closed polygon ring at its own elevation. Lines
	// come in PAIRS (a -> b per drawn segment), in the node's local space — the
	// island vertices are already world XZ units (no tile_world_size scaling), so
	// vertex (x, z) maps to Vector3(x, elevation * level_height, z).
	Vector<Vector3> lines;
	for (int i = 0; i < islands.size(); i++) {
		const Dictionary island = islands[i];
		const PackedVector2Array verts = island.get("vertices", PackedVector2Array());
		const int vert_count = verts.size();
		if (vert_count < 3) {
			continue;
		}

		const int elev = island.get("elevation", 0);
		const double world_y = elev * level_height;

		for (int v = 0; v < vert_count; v++) {
			const Vector2 a = verts[v];
			const Vector2 b = verts[(v + 1) % vert_count]; // wrap last -> first
			lines.push_back(Vector3(a.x, world_y, a.y));
			lines.push_back(Vector3(b.x, world_y, b.y));
		}
	}

	if (lines.is_empty()) {
		return;
	}

	const Ref<Material> material = get_material("terrain_outline", p_gizmo);
	p_gizmo->add_lines(lines, material);
	p_gizmo->add_collision_segments(lines);
}
