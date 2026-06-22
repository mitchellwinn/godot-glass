#pragma once

#include "editor/scene/3d/node_3d_editor_gizmos.h"

// Draws a read-only viewport outline for a GlassTerrain: each island's polygon
// ring is emitted as line segments at world Y = elevation * level_height. No
// editable handles in this slice — purely a viewport authoring aid, mirroring
// FlowRegionGizmoPlugin's outline-draw structure (minus the box handles).
class GlassTerrainGizmoPlugin : public EditorNode3DGizmoPlugin {
	GDCLASS(GlassTerrainGizmoPlugin, EditorNode3DGizmoPlugin);

public:
	virtual bool has_gizmo(Node3D *p_spatial) override;
	virtual String get_gizmo_name() const override;
	virtual int get_priority() const override;
	virtual void redraw(EditorNode3DGizmo *p_gizmo) override;

	GlassTerrainGizmoPlugin();
};
