#include "register_types.h"

#include "glass_terrain_builder.h"

#include "core/object/class_db.h"

void initialize_glass_terrain_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(GlassTerrainBuilder);
}

void uninitialize_glass_terrain_module(ModuleInitializationLevel p_level) {
}
