#pragma once

#include "scene/3d/node_3d.h"

class Area3D;

// FlowRegion: a native box region node. The box is an INLINE value (`size`) on the
// node itself — no child CollisionShape3D, no shared Shape3D resource, no "Make
// Unique" footgun. Duplicating just copies the value. Edited in the normal
// Inspector or via the built-in box gizmo.
//
// At runtime it generates its own Area3D + box collision from `size` and re-emits
// body_entered / body_exited — so it's a usable trigger volume out of the box,
// while the AUTHORING stays a single clean box you drag. Foundation for the
// Trigger/Warp/Zone entity types.
class FlowRegion : public Node3D {
	GDCLASS(FlowRegion, Node3D);

	Vector3 size = Vector3(2, 2, 2);
	uint32_t collision_mask = 1;
	Area3D *area = nullptr;

	void _build_area();
	void _on_body_entered(Node3D *p_body);
	void _on_body_exited(Node3D *p_body);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_size(const Vector3 &p_size);
	Vector3 get_size() const;

	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const;

	FlowRegion();
};
