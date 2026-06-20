#include "flow_warp.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

void FlowWarp::set_target(const NodePath &p_target) {
	target = p_target;
}

NodePath FlowWarp::get_target() const {
	return target;
}

void FlowWarp::_on_warp_body_entered(Node3D *p_body) {
	if (!p_body || target.is_empty()) {
		return;
	}
	Node3D *dest = Object::cast_to<Node3D>(get_node_or_null(target));
	if (dest) {
		p_body->set_global_position(dest->get_global_position());
	}
}

void FlowWarp::_notification(int p_what) {
	// FlowRegion (parent) builds the detector + emits body_entered at READY;
	// we just hook our own signal to perform the teleport.
	if (p_what == NOTIFICATION_READY && !Engine::get_singleton()->is_editor_hint()) {
		connect(SNAME("body_entered"), callable_mp(this, &FlowWarp::_on_warp_body_entered));
	}
}

void FlowWarp::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target", "target"), &FlowWarp::set_target);
	ClassDB::bind_method(D_METHOD("get_target"), &FlowWarp::get_target);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "target", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_target", "get_target");
}

FlowWarp::FlowWarp() {
}
