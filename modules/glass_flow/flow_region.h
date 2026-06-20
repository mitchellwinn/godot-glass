#pragma once

#include "scene/3d/node_3d.h"

// FlowRegion: a native box region node. The box is an INLINE value (`size`) on the
// node itself — no child CollisionShape3D, no shared Shape3D resource, no "Make
// Unique" footgun. Duplicating just copies the value. Edited in the normal
// Inspector or via the built-in box gizmo. Foundation for Trigger/Warp/Zone types.
class FlowRegion : public Node3D {
	GDCLASS(FlowRegion, Node3D);

	Vector3 size = Vector3(2, 2, 2);

protected:
	static void _bind_methods();

public:
	void set_size(const Vector3 &p_size);
	Vector3 get_size() const;

	FlowRegion();
};
