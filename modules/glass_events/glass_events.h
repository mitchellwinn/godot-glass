#pragma once

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/variant/callable.h"

// GlassEvents: the native command/event registry — the generic engine half of the
// dialogue/event system. Games register their command vocabulary as GDScript
// Callables (`register_event("setFlag", callable)`); the dialogue runner dispatches
// by name via `process_events("setFlag|x|true")`. The engine knows nothing about
// any specific command. Exposed to GDScript as the global singleton `GlassEvents`.
class GlassEvents : public Object {
	GDCLASS(GlassEvents, Object);

	static GlassEvents *singleton;
	HashMap<StringName, Callable> custom_events;

protected:
	static void _bind_methods();

public:
	static GlassEvents *get_singleton() { return singleton; }

	void register_event(const StringName &p_name, const Callable &p_handler);
	void unregister_event(const StringName &p_name);
	bool has_event(const StringName &p_name) const;

	// Dispatch one event string "name|arg1|arg2". Passes the full split array
	// (args[0] == name) to the handler, mirroring the GDScript `func(args: Array)`
	// contract. Unwraps the ["text", true] blocking-marker convention to its text.
	Variant process_events(const String &p_event_string);

	GlassEvents();
	~GlassEvents();
};
