#include "flow_region.h"

#include "core/object/class_db.h"

void FlowRegion::set_size(const Vector3 &p_size) {
	size = p_size;
	update_gizmos();
}

Vector3 FlowRegion::get_size() const {
	return size;
}

void FlowRegion::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &FlowRegion::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &FlowRegion::get_size);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
}

FlowRegion::FlowRegion() {
}
