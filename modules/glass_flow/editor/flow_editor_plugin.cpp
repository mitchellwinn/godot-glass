#include "flow_editor_plugin.h"

#include "flow_region_gizmo_plugin.h"

FlowEditorPlugin::FlowEditorPlugin() {
	Ref<FlowRegionGizmoPlugin> gizmo_plugin = Ref<FlowRegionGizmoPlugin>(memnew(FlowRegionGizmoPlugin));
	add_node_3d_gizmo_plugin(gizmo_plugin);
}
