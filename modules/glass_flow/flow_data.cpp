#include "flow_data.h"

#include "core/object/class_db.h"
#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"
#include "scene/main/node.h"

void FlowData::set_records(const TypedArray<Dictionary> &p_records) {
	records = p_records;
}

TypedArray<Dictionary> FlowData::get_records() const {
	return records;
}

bool FlowData::_is_flow_node_type(const StringName &p_node_type) {
	// Must be a registered class that derives from Node...
	if (!ClassDB::class_exists(p_node_type) || !ClassDB::can_instantiate(p_node_type)) {
		return false;
	}
	if (!ClassDB::is_parent_class(p_node_type, SNAME("Node"))) {
		return false;
	}
	// ...and a Flow* type: either FlowMarker (a Marker3D) or anything under
	// FlowRegion (FlowRegion / FlowWarp / FlowEvent and game subclasses).
	if (ClassDB::is_parent_class(p_node_type, SNAME("FlowMarker"))) {
		return true;
	}
	if (ClassDB::is_parent_class(p_node_type, SNAME("FlowRegion"))) {
		return true;
	}
	return false;
}

TypedArray<Node> FlowData::spawn_into(Node *p_parent) {
	TypedArray<Node> spawned;
	if (p_parent == nullptr) {
		ERR_FAIL_V_MSG(spawned, "FlowData.spawn_into: parent is null.");
	}

	for (int i = 0; i < records.size(); i++) {
		Dictionary record = records[i];

		const StringName node_type = record.get("node_type", StringName());
		if (node_type == StringName()) {
			WARN_PRINT(vformat("FlowData.spawn_into: record %d has no \"node_type\"; skipping.", i));
			continue;
		}
		if (!_is_flow_node_type(node_type)) {
			WARN_PRINT(vformat("FlowData.spawn_into: record %d node_type \"%s\" is not an instantiable Flow* Node; skipping.", i, String(node_type)));
			continue;
		}

		Object *obj = ClassDB::instantiate(node_type);
		Node *node = Object::cast_to<Node>(obj);
		if (node == nullptr) {
			// Guarded above, but cast_to is the authoritative check before we own it.
			if (obj) {
				memdelete(obj);
			}
			WARN_PRINT(vformat("FlowData.spawn_into: record %d node_type \"%s\" did not produce a Node; skipping.", i, String(node_type)));
			continue;
		}

		// Apply the record transform to Node3D records (markers/regions/warps/events
		// are all Node3D; a non-Node3D Flow node simply ignores it).
		Node3D *node_3d = Object::cast_to<Node3D>(node);
		if (node_3d && record.has("transform")) {
			node_3d->set_transform(record["transform"]);
		}

		// Apply each property generically — no field is special-cased. Whatever the
		// game put in "properties" is forwarded straight to the node's set().
		if (record.has("properties")) {
			Dictionary properties = record["properties"];
			LocalVector<Variant> keys = properties.get_key_list();
			for (const Variant &key : keys) {
				node->set(key, properties[key]);
			}
		}

		p_parent->add_child(node);
		spawned.push_back(node);
	}

	return spawned;
}

void FlowData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_records", "records"), &FlowData::set_records);
	ClassDB::bind_method(D_METHOD("get_records"), &FlowData::get_records);
	ClassDB::bind_method(D_METHOD("spawn_into", "parent"), &FlowData::spawn_into);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "records", PROPERTY_HINT_ARRAY_TYPE, vformat("%d:", Variant::DICTIONARY)), "set_records", "get_records");
}

FlowData::FlowData() {
}
