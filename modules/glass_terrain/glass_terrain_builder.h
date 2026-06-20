#pragma once

#include "core/object/ref_counted.h"
#include "scene/resources/mesh.h"

// GlassTerrainBuilder
//
// Native (C++) terrain mesh construction for Glass. The heavy mesh/collision
// bakes that bottleneck the GDScript mahou_terrain plugin live here so they run
// at native speed and are available to every Glass project.
//
// build_test() is a minimal GDScript<->C++ ArrayMesh round-trip proof: it builds
// an n x n flat quad grid and hands a finished ArrayMesh back to GDScript. Once
// the pipeline is proven, the real builders (autotile/road, terrain, collision,
// decimation) are ported onto this class slice by slice.
class GlassTerrainBuilder : public RefCounted {
	GDCLASS(GlassTerrainBuilder, RefCounted);

protected:
	static void _bind_methods();

public:
	Ref<ArrayMesh> build_test(int p_n);

	GlassTerrainBuilder() {}
};
