#include "flow_editor_plugin.h"

#include "flow_dock.h"
#include "flow_region_gizmo_plugin.h"

#include "editor/docks/editor_dock_manager.h"

FlowEditorPlugin::FlowEditorPlugin() {
	// Box gizmo for FlowRegion (and its subclasses, e.g. FlowWarp).
	Ref<FlowRegionGizmoPlugin> gizmo_plugin = Ref<FlowRegionGizmoPlugin>(memnew(FlowRegionGizmoPlugin));
	add_node_3d_gizmo_plugin(gizmo_plugin);

	// Native Flow authoring dock.
	FlowDock *dock = memnew(FlowDock);
	EditorDockManager::get_singleton()->add_dock(dock);
}
