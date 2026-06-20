#pragma once

#include "editor/docks/editor_dock.h"

// The native Flow dock — a built-in editor panel for authoring Flow. v1: buttons
// that drop Flow nodes into the open scene (with undo/redo + auto-select). Grows
// into the scene overview / bulk-authoring surface as Flow expands.
class FlowDock : public EditorDock {
	GDCLASS(FlowDock, EditorDock);

	void _add_node(const String &p_class);
	void _on_add_region();
	void _on_add_marker();
	void _on_add_warp();
	void _on_add_event();

public:
	FlowDock();
};
