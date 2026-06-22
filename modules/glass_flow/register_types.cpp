#include "register_types.h"

#include "flow_data.h"
#include "flow_event.h"
#include "flow_marker.h"
#include "flow_region.h"
#include "flow_warp.h"

#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/flow_editor_plugin.h"
#include "editor/plugins/editor_plugin.h"
#endif

void initialize_glass_flow_module(ModuleInitializationLevel p_level) {
	// Runtime classes — available in every project, editor and exported game alike.
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(FlowRegion);
		GDREGISTER_CLASS(FlowMarker);
		GDREGISTER_CLASS(FlowWarp);
		GDREGISTER_CLASS(FlowEvent);
		GDREGISTER_CLASS(FlowData);
	}

#ifdef TOOLS_ENABLED
	// Editor tooling (dock/gizmo) — only in the editor build.
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::add_by_type<FlowEditorPlugin>();
	}
#endif
}

void uninitialize_glass_flow_module(ModuleInitializationLevel p_level) {
}
