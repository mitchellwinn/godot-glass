#pragma once

#include "editor/plugins/editor_plugin.h"

// The Glass Flow editor plugin. For now it just registers the FlowRegion box
// gizmo; the Flow authoring dock + inspector will hang off this as Flow grows.
class FlowEditorPlugin : public EditorPlugin {
	GDCLASS(FlowEditorPlugin, EditorPlugin);

public:
	virtual String get_plugin_name() const override { return "Flow"; }

	FlowEditorPlugin();
};
