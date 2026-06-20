#pragma once

#include "scene/3d/marker_3d.h"

// FlowMarker: a named point in space — spawn points, POIs, warp targets, camera
// anchors. Extends Marker3D, so it inherits the crosshair gizmo for free; adds an
// `id` so other Flow nodes (and game code) can refer to it by name.
class FlowMarker : public Marker3D {
	GDCLASS(FlowMarker, Marker3D);

	String id;

protected:
	static void _bind_methods();

public:
	void set_id(const String &p_id);
	String get_id() const;

	FlowMarker();
};
