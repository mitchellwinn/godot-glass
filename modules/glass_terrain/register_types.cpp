#include "register_types.h"

#include "glass_height_sampler.h"
#include "glass_mesh_decimator.h"
#include "glass_terrain_builder.h"
#include "glass_terrain_mesh_builder.h"

#include "core/object/class_db.h"

void initialize_glass_terrain_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(GlassTerrainBuilder);
	GDREGISTER_CLASS(GlassHeightSampler);
	GDREGISTER_CLASS(GlassTerrainMeshBuilder);
	GDREGISTER_CLASS(GlassMeshDecimator);
}

void uninitialize_glass_terrain_module(ModuleInitializationLevel p_level) {
}
