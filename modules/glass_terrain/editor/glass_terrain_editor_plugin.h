#pragma once

#include "editor/plugins/editor_plugin.h"

class Button;
class GlassTerrain;

// The Glass Terrain editor plugin. Registers the read-only island-outline gizmo
// (GlassTerrainGizmoPlugin) and surfaces a single "Rebuild Terrain" button in the
// 3D editor toolbar while a GlassTerrain is selected — the editor counterpart to
// FlowEditorPlugin. Inspector-native editing of the island data stays in the
// standard Inspector; this plugin adds only the viewport aid + manual rebuild.
class GlassTerrainEditorPlugin : public EditorPlugin {
	GDCLASS(GlassTerrainEditorPlugin, EditorPlugin);

	Button *rebuild_button = nullptr;
	GlassTerrain *edited_terrain = nullptr;

	void _rebuild_pressed();

public:
	virtual String get_plugin_name() const override { return "GlassTerrain"; }

	virtual bool handles(Object *p_object) const override;
	virtual void edit(Object *p_object) override;
	virtual void make_visible(bool p_visible) override;

	GlassTerrainEditorPlugin();
};
