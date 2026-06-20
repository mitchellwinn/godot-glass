#pragma once

#include "flow_region.h"

// FlowWarp: a FlowRegion that teleports the entering body to a target Node3D
// (typically a FlowMarker). Inherits FlowRegion's inline box, box gizmo, and
// trigger detection — only the warp behavior is new.
class FlowWarp : public FlowRegion {
	GDCLASS(FlowWarp, FlowRegion);

	NodePath target;

	void _on_warp_body_entered(Node3D *p_body);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_target(const NodePath &p_target);
	NodePath get_target() const;

	FlowWarp();
};
