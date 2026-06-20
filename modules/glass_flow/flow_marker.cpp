#include "flow_marker.h"

#include "core/object/class_db.h"

void FlowMarker::set_id(const String &p_id) {
	id = p_id;
}

String FlowMarker::get_id() const {
	return id;
}

void FlowMarker::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_id", "id"), &FlowMarker::set_id);
	ClassDB::bind_method(D_METHOD("get_id"), &FlowMarker::get_id);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "id"), "set_id", "get_id");
}

FlowMarker::FlowMarker() {
}
