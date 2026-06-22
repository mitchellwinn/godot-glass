#include "glass_terrain_editor_plugin.h"

#include "../glass_terrain.h"
#include "glass_terrain_gizmo_plugin.h"

#include "core/object/callable_mp.h"
#include "scene/gui/button.h"
#include "scene/scene_string_names.h"

GlassTerrainEditorPlugin::GlassTerrainEditorPlugin() {
	// Read-only island-outline gizmo for GlassTerrain.
	Ref<GlassTerrainGizmoPlugin> gizmo_plugin = Ref<GlassTerrainGizmoPlugin>(memnew(GlassTerrainGizmoPlugin));
	add_node_3d_gizmo_plugin(gizmo_plugin);

	// Minimal "Rebuild Terrain" surface in the 3D editor toolbar. Hidden until a
	// GlassTerrain is selected (see make_visible()).
	rebuild_button = memnew(Button);
	rebuild_button->set_text(TTR("Rebuild Terrain"));
	rebuild_button->set_tooltip_text(TTR("Rebuild the selected GlassTerrain (mesh, materials, collision)."));
	rebuild_button->hide();
	rebuild_button->connect(SceneStringName(pressed), callable_mp(this, &GlassTerrainEditorPlugin::_rebuild_pressed));
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, rebuild_button);
}

bool GlassTerrainEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<GlassTerrain>(p_object) != nullptr;
}

void GlassTerrainEditorPlugin::edit(Object *p_object) {
	edited_terrain = Object::cast_to<GlassTerrain>(p_object);
}

void GlassTerrainEditorPlugin::make_visible(bool p_visible) {
	if (rebuild_button) {
		rebuild_button->set_visible(p_visible);
	}
	if (!p_visible) {
		edited_terrain = nullptr;
	}
}

void GlassTerrainEditorPlugin::_rebuild_pressed() {
	if (edited_terrain) {
		edited_terrain->rebuild();
		edited_terrain->update_gizmos();
	}
}
