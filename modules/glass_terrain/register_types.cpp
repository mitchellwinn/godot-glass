#include "register_types.h"

#include "glass_height_sampler.h"
#include "glass_mesh_decimator.h"
#include "glass_terrain.h"
#include "glass_terrain_builder.h"
#include "glass_terrain_mesh_builder.h"

#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/glass_terrain_editor_plugin.h"
#include "editor/plugins/editor_plugin.h"
#endif

void initialize_glass_terrain_module(ModuleInitializationLevel p_level) {
	// Runtime classes — available in every project, editor and exported game alike.
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(GlassTerrainBuilder);
		GDREGISTER_CLASS(GlassHeightSampler);
		GDREGISTER_CLASS(GlassTerrainMeshBuilder);
		GDREGISTER_CLASS(GlassMeshDecimator);
		GDREGISTER_CLASS(GlassTerrain);
	}

#ifdef TOOLS_ENABLED
	// Editor tooling (outline gizmo + rebuild button) — only in the editor build.
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::add_by_type<GlassTerrainEditorPlugin>();
	}
#endif
}

void uninitialize_glass_terrain_module(ModuleInitializationLevel p_level) {
}
