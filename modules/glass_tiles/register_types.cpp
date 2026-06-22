#include "register_types.h"

#include "glass_tiles_map.h"
#include "glass_tiles_mesher.h"

#include "core/object/class_db.h"

void initialize_glass_tiles_module(ModuleInitializationLevel p_level) {
	// Runtime classes — available in every project, editor and exported game alike.
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(GlassTilesMesher);
		GDREGISTER_CLASS(GlassTilesMap);
	}
}

void uninitialize_glass_tiles_module(ModuleInitializationLevel p_level) {
}
