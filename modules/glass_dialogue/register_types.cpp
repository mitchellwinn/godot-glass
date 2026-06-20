#include "register_types.h"

#include "glass_events.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static GlassEvents *_glass_events = nullptr;

void initialize_glass_dialogue_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(GlassEvents);
	_glass_events = memnew(GlassEvents);
	Engine::get_singleton()->add_singleton(Engine::Singleton("GlassEvents", GlassEvents::get_singleton()));
}

void uninitialize_glass_dialogue_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	if (_glass_events) {
		memdelete(_glass_events);
		_glass_events = nullptr;
	}
}
