#pragma once

#include "flow_region.h"

// FlowEvent: a FlowRegion that fires a named event when a body enters. The engine
// stays game-agnostic — it just emits `event_triggered(event_key, body)`; the game
// wires that signal to its own dialogue/event runner (e.g. DialogueManager). The
// bridge from "a region in the scene" to "something happens."
class FlowEvent : public FlowRegion {
	GDCLASS(FlowEvent, FlowRegion);

	String event_key;
	bool one_shot = false;
	bool _fired = false;

	void _on_event_body_entered(Node3D *p_body);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_event_key(const String &p_key);
	String get_event_key() const;
	void set_one_shot(bool p_one_shot);
	bool get_one_shot() const;

	FlowEvent();
};
