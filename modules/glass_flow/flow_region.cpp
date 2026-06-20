#include "flow_region.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/physics/area_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/3d/box_shape_3d.h"

void FlowRegion::set_size(const Vector3 &p_size) {
	size = p_size;
	update_gizmos();
}

Vector3 FlowRegion::get_size() const {
	return size;
}

void FlowRegion::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
	if (area) {
		area->set_collision_mask(p_mask);
	}
}

uint32_t FlowRegion::get_collision_mask() const {
	return collision_mask;
}

void FlowRegion::_build_area() {
	if (area) {
		return;
	}
	// Generate the detector at runtime from the inline `size`. Both nodes are
	// internal children — never shown in the scene tree, never serialized — so the
	// author only ever sees/edits the single inline box.
	area = memnew(Area3D);
	area->set_collision_layer(0); // detector only
	area->set_collision_mask(collision_mask);
	add_child(area, false, INTERNAL_MODE_BACK);

	CollisionShape3D *cs = memnew(CollisionShape3D);
	Ref<BoxShape3D> box;
	box.instantiate();
	box->set_size(size);
	cs->set_shape(box);
	area->add_child(cs, false, INTERNAL_MODE_BACK);

	area->connect(SNAME("body_entered"), callable_mp(this, &FlowRegion::_on_body_entered));
	area->connect(SNAME("body_exited"), callable_mp(this, &FlowRegion::_on_body_exited));
}

void FlowRegion::_on_body_entered(Node3D *p_body) {
	emit_signal(SNAME("body_entered"), p_body);
}

void FlowRegion::_on_body_exited(Node3D *p_body) {
	emit_signal(SNAME("body_exited"), p_body);
}

void FlowRegion::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY && !Engine::get_singleton()->is_editor_hint()) {
		_build_area();
	}
}

void FlowRegion::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &FlowRegion::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &FlowRegion::get_size);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &FlowRegion::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &FlowRegion::get_collision_mask);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");

	ADD_SIGNAL(MethodInfo("body_entered", PropertyInfo(Variant::OBJECT, "body", PROPERTY_HINT_RESOURCE_TYPE, "Node3D")));
	ADD_SIGNAL(MethodInfo("body_exited", PropertyInfo(Variant::OBJECT, "body", PROPERTY_HINT_RESOURCE_TYPE, "Node3D")));
}

FlowRegion::FlowRegion() {
}
