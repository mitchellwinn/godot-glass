#include "glass_events.h"

#include "core/object/class_db.h"

GlassEvents *GlassEvents::singleton = nullptr;

void GlassEvents::register_event(const StringName &p_name, const Callable &p_handler) {
	custom_events[p_name] = p_handler;
}

void GlassEvents::unregister_event(const StringName &p_name) {
	custom_events.erase(p_name);
}

bool GlassEvents::has_event(const StringName &p_name) const {
	return custom_events.has(p_name);
}

Variant GlassEvents::process_events(const String &p_event_string) {
	PackedStringArray parts = p_event_string.split("|");
	if (parts.is_empty()) {
		return Variant();
	}
	const StringName name = parts[0];
	if (!custom_events.has(name)) {
		WARN_PRINT("[GlassEvents] Unknown event: " + String(name));
		return Variant();
	}

	// Hand the full split array (name + args) to the handler as a single Array arg.
	Array args;
	for (int i = 0; i < parts.size(); i++) {
		args.push_back(parts[i]);
	}
	const Variant result = custom_events[name].call(args);

	// Handlers signal a blocking op by returning ["text", true]; unwrap to text.
	if (result.get_type() == Variant::ARRAY) {
		const Array a = result;
		if (a.size() > 0) {
			return a[0];
		}
	}
	return result;
}

void GlassEvents::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_event", "name", "handler"), &GlassEvents::register_event);
	ClassDB::bind_method(D_METHOD("unregister_event", "name"), &GlassEvents::unregister_event);
	ClassDB::bind_method(D_METHOD("has_event", "name"), &GlassEvents::has_event);
	ClassDB::bind_method(D_METHOD("process_events", "event_string"), &GlassEvents::process_events);
}

GlassEvents::GlassEvents() {
	singleton = this;
}

GlassEvents::~GlassEvents() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
