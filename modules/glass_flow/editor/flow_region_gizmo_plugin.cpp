#include "flow_region_gizmo_plugin.h"

#include "../flow_region.h"

FlowRegionGizmoPlugin::FlowRegionGizmoPlugin() {
	helper.instantiate();
	create_material("flow_material", Color(0.45, 0.85, 1.0));
	create_handle_material("handles");
}

bool FlowRegionGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<FlowRegion>(p_spatial) != nullptr;
}

String FlowRegionGizmoPlugin::get_gizmo_name() const {
	return "FlowRegion";
}

int FlowRegionGizmoPlugin::get_priority() const {
	return -1;
}

String FlowRegionGizmoPlugin::get_handle_name(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const {
	return helper->box_get_handle_name(p_id);
}

Variant FlowRegionGizmoPlugin::get_handle_value(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const {
	FlowRegion *region = Object::cast_to<FlowRegion>(p_gizmo->get_node_3d());
	return region ? Variant(region->get_size()) : Variant();
}

void FlowRegionGizmoPlugin::begin_handle_action(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) {
	helper->initialize_handle_action(get_handle_value(p_gizmo, p_id, p_secondary), p_gizmo->get_node_3d()->get_global_transform());
}

void FlowRegionGizmoPlugin::set_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, Camera3D *p_camera, const Point2 &p_point) {
	FlowRegion *region = Object::cast_to<FlowRegion>(p_gizmo->get_node_3d());
	if (!region) {
		return;
	}
	Vector3 sg[2];
	helper->get_segment(p_camera, p_point, sg);

	Vector3 size = region->get_size();
	Vector3 position;
	helper->box_set_handle(sg, p_id, size, position);
	region->set_size(size);
	region->set_global_position(position);
}

void FlowRegionGizmoPlugin::commit_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, const Variant &p_restore, bool p_cancel) {
	FlowRegion *region = Object::cast_to<FlowRegion>(p_gizmo->get_node_3d());
	if (!region) {
		return;
	}
	helper->box_commit_handle(TTR("Change FlowRegion Size"), p_cancel, region, region);
}

void FlowRegionGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	FlowRegion *region = Object::cast_to<FlowRegion>(p_gizmo->get_node_3d());
	if (!region) {
		return;
	}
	p_gizmo->clear();

	const Vector3 size = region->get_size();
	AABB aabb;
	aabb.position = -size / 2.0;
	aabb.size = size;

	Vector<Vector3> lines;
	for (int i = 0; i < 12; i++) {
		Vector3 a, b;
		aabb.get_edge(i, a, b);
		lines.push_back(a);
		lines.push_back(b);
	}

	const Ref<Material> material = get_material("flow_material", p_gizmo);
	const Ref<Material> handles_material = get_material("handles");
	p_gizmo->add_lines(lines, material);
	p_gizmo->add_collision_segments(lines);
	p_gizmo->add_handles(helper->box_get_handles(size), handles_material);
}
