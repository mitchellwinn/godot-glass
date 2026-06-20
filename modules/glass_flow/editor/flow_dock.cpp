#include "flow_dock.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"

void FlowDock::_add_node(const String &p_class) {
	Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (!scene_root) {
		return; // no scene open
	}
	Node *node = Object::cast_to<Node>(ClassDB::instantiate(p_class));
	if (!node) {
		return;
	}
	node->set_name(p_class);

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("Add " + p_class);
	ur->add_do_method(scene_root, "add_child", node, true);
	ur->add_do_method(node, "set_owner", scene_root);
	ur->add_do_reference(node);
	ur->add_undo_method(scene_root, "remove_child", node);
	ur->commit_action();

	EditorSelection *sel = EditorInterface::get_singleton()->get_selection();
	sel->clear();
	sel->add_node(node);
}

void FlowDock::_on_add_region() {
	_add_node("FlowRegion");
}

void FlowDock::_on_add_marker() {
	_add_node("FlowMarker");
}

void FlowDock::_on_add_warp() {
	_add_node("FlowWarp");
}

void FlowDock::_on_add_event() {
	_add_node("FlowEvent");
}

FlowDock::FlowDock() {
	set_name("Flow");
	set_default_slot(EditorDock::DOCK_SLOT_RIGHT_UL);
	set_custom_minimum_size(Size2(0, 120));

	VBoxContainer *vb = memnew(VBoxContainer);
	add_child(vb);

	Label *lbl = memnew(Label);
	lbl->set_text("Add Flow nodes to the open scene:");
	vb->add_child(lbl);

	Button *b_region = memnew(Button);
	b_region->set_text("Add FlowRegion");
	vb->add_child(b_region);
	b_region->connect(SNAME("pressed"), callable_mp(this, &FlowDock::_on_add_region));

	Button *b_marker = memnew(Button);
	b_marker->set_text("Add FlowMarker");
	vb->add_child(b_marker);
	b_marker->connect(SNAME("pressed"), callable_mp(this, &FlowDock::_on_add_marker));

	Button *b_warp = memnew(Button);
	b_warp->set_text("Add FlowWarp");
	vb->add_child(b_warp);
	b_warp->connect(SNAME("pressed"), callable_mp(this, &FlowDock::_on_add_warp));

	Button *b_event = memnew(Button);
	b_event->set_text("Add FlowEvent");
	vb->add_child(b_event);
	b_event->connect(SNAME("pressed"), callable_mp(this, &FlowDock::_on_add_event));
}
