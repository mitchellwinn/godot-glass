#pragma once

#include "core/io/resource.h"
#include "core/variant/typed_array.h"

class Node;

// FlowData: a GENERIC, data-driven container of Flow records. Instead of authoring
// Flow nodes (FlowMarker / FlowWarp / FlowRegion / FlowEvent) by hand in a scene,
// you describe them as plain data — one Dictionary per record — and let the engine
// spawn the real nodes at load time.
//
// Each record is a Dictionary with (at minimum):
//   "node_type":  StringName — a registered Flow node class to instantiate
//                 (e.g. "FlowMarker", "FlowWarp", "FlowRegion", "FlowEvent").
//   "transform":  Transform3D — applied to the spawned node (if it is a Node3D).
//   "properties": Dictionary — property-name -> value, each applied via set()
//                 on the spawned node (e.g. {"id": "spawn_point"}, {"target": ...}).
//
// The container is deliberately engine-generic: it hardcodes NO game record types
// or fields. The only contract is the three keys above; everything in "properties"
// is forwarded blindly to the node. Game layers build the records however they like.
class FlowData : public Resource {
	GDCLASS(FlowData, Resource);

	// Generic record store. Each entry is a Dictionary as documented above.
	TypedArray<Dictionary> records;

	// Guard helper: only instantiate Node subclasses whose name starts with "Flow"
	// (FlowMarker / FlowRegion / FlowWarp / FlowEvent and game subclasses thereof),
	// so a stray record can't spawn an arbitrary class.
	static bool _is_flow_node_type(const StringName &p_node_type);

protected:
	static void _bind_methods();

public:
	void set_records(const TypedArray<Dictionary> &p_records);
	TypedArray<Dictionary> get_records() const;

	// Instantiate every record under `p_parent`: create node_type via
	// ClassDB::instantiate (skipping anything that isn't a Flow* Node), apply its
	// transform, set each property, add_child(p_parent). Returns the spawned nodes
	// in record order (skipped records leave no gap).
	TypedArray<Node> spawn_into(Node *p_parent);

	FlowData();
};
