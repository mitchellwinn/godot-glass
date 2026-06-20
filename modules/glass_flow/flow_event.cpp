#include "flow_event.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

void FlowEvent::set_event_key(const String &p_key) {
	event_key = p_key;
}

String FlowEvent::get_event_key() const {
	return event_key;
}

void FlowEvent::set_one_shot(bool p_one_shot) {
	one_shot = p_one_shot;
}

bool FlowEvent::get_one_shot() const {
	return one_shot;
}

void FlowEvent::_on_event_body_entered(Node3D *p_body) {
	if (one_shot && _fired) {
		return;
	}
	_fired = true;
	emit_signal(SNAME("event_triggered"), event_key, p_body);
}

void FlowEvent::_notification(int p_what) {
	// FlowRegion (parent) builds the detector + emits body_entered at READY;
	// we hook it to fire the named event.
	if (p_what == NOTIFICATION_READY && !Engine::get_singleton()->is_editor_hint()) {
		connect(SNAME("body_entered"), callable_mp(this, &FlowEvent::_on_event_body_entered));
	}
}

void FlowEvent::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_event_key", "key"), &FlowEvent::set_event_key);
	ClassDB::bind_method(D_METHOD("get_event_key"), &FlowEvent::get_event_key);
	ClassDB::bind_method(D_METHOD("set_one_shot", "one_shot"), &FlowEvent::set_one_shot);
	ClassDB::bind_method(D_METHOD("get_one_shot"), &FlowEvent::get_one_shot);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "event_key"), "set_event_key", "get_event_key");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "one_shot"), "set_one_shot", "get_one_shot");

	ADD_SIGNAL(MethodInfo("event_triggered", PropertyInfo(Variant::STRING, "event_key"), PropertyInfo(Variant::OBJECT, "body", PROPERTY_HINT_RESOURCE_TYPE, "Node3D")));
}

FlowEvent::FlowEvent() {
}
