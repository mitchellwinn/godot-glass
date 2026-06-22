# Glass Flow — FlowData headless runtime smoke test.
#
# Run with the editor binary in headless mode:
#   bin/godot.linuxbsd.editor.x86_64 --headless --script misc/scripts/glass_flow_data_smoke.gd
#
# Exits 0 (SMOKE OK) when the generic, data-driven FlowData container spawns real
# native Flow nodes from plain record Dictionaries:
#   - One { node_type:"FlowMarker", transform origin (3,0,5), properties:{id:"spawn_point"} }
#     record spawns exactly one FlowMarker at (3,0,5) with id == "spawn_point".
#   - One { node_type:"FlowWarp", ... } record spawns a FlowWarp.
# spawn_into(parent) returns the spawned nodes; we assert count, types, the applied
# (local) transform origin, and the forwarded `id`/`size` properties. Exits 1
# (SMOKE FAIL: <detail>) on the FIRST miss, so CI gates on it.
#
# This deliberately uses ONLY the public GDScript API (FlowData instantiation via
# ClassDB plus the bound set_records()/spawn_into()) and the documented record
# Dictionary schema (node_type / transform / properties).
#
# IMPORTANT: locals are intentionally left dynamically typed (no `:=` on the
# FlowData / spawned nodes). Using static inference like `var d := FlowData.new()`
# makes the GDScript parser HARD-FAIL to load this whole file on any engine build
# where FlowData is not registered — which would bypass the runtime guard below and
# (because Godot does not propagate a script load error to the process exit code)
# could let CI pass silently. Instantiating via ClassDB after an explicit
# class_exists() check keeps the failure inside _initialize() where we control
# quit(1).

extends SceneTree

# Generous tolerance for the float-compared transform origin.
const ORIGIN_TOLERANCE := 0.0001


func _fail(p_msg: String) -> void:
	print("SMOKE FAIL: %s" % p_msg)
	quit(1)


func _initialize() -> void:
	# Guard: the classes must be registered/instantiable from GDScript. If the
	# glass_flow module is missing from the build, fail loudly (don't crash).
	if not ClassDB.class_exists("FlowData"):
		_fail("FlowData class is not registered in this build")
		return
	if not ClassDB.can_instantiate("FlowData"):
		_fail("FlowData is registered but not instantiable")
		return
	if not ClassDB.class_exists("FlowMarker"):
		_fail("FlowMarker class is not registered in this build")
		return
	if not ClassDB.class_exists("FlowWarp"):
		_fail("FlowWarp class is not registered in this build")
		return

	# --- Build a FlowData with two generic records ------------------------------
	var data = ClassDB.instantiate("FlowData")
	if data == null:
		_fail("ClassDB.instantiate(\"FlowData\") returned null")
		return

	# Record 0: a FlowMarker at (3,0,5) with id "spawn_point".
	var marker_xform := Transform3D(Basis(), Vector3(3.0, 0.0, 5.0))
	var marker_record := {
		"node_type": &"FlowMarker",
		"transform": marker_xform,
		"properties": {"id": "spawn_point"},
	}

	# Record 1: a FlowWarp (inherits FlowRegion's box). size is a Vector3 property
	# that exists on FlowRegion/FlowWarp; set it to prove generic property forward.
	var warp_xform := Transform3D(Basis(), Vector3(-2.0, 1.0, 0.0))
	var warp_record := {
		"node_type": &"FlowWarp",
		"transform": warp_xform,
		"properties": {"size": Vector3(4.0, 4.0, 4.0)},
	}

	data.set_records([marker_record, warp_record])

	# --- Parent in the live tree, then spawn ------------------------------------
	var root := get_root()
	if root == null:
		_fail("SceneTree root is null")
		return

	var parent = ClassDB.instantiate("Node3D")
	if parent == null:
		_fail("ClassDB.instantiate(\"Node3D\") returned null")
		return
	root.add_child(parent)

	var spawned = data.spawn_into(parent)
	if spawned == null:
		_fail("spawn_into() returned null")
		return

	# --- Assert: exactly two nodes spawned --------------------------------------
	if spawned.size() != 2:
		_fail("spawn_into() returned %d nodes, expected 2" % spawned.size())
		return

	# --- Assert: parent actually adopted exactly two children -------------------
	if parent.get_child_count() != 2:
		_fail("parent has %d children after spawn, expected 2" % parent.get_child_count())
		return

	# --- Assert record 0: FlowMarker at (3,0,5) with id "spawn_point" -----------
	var marker = spawned[0]
	if marker == null:
		_fail("spawned[0] is null")
		return
	if not marker.is_class("FlowMarker"):
		_fail("spawned[0] is a %s, expected FlowMarker" % marker.get_class())
		return
	if marker.get_parent() != parent:
		_fail("spawned[0] FlowMarker is not parented under the Node3D parent")
		return

	# The record transform is applied directly to the node's local transform; the
	# parent Node3D sits at identity, so the marker's local origin IS its world
	# position here. (global_transform is intentionally not asserted: it requires
	# is_inside_tree(), which is false during SceneTree._initialize() by Godot's
	# own lifecycle — the parent is parented to root but root has not entered the
	# tree yet. The local origin equals the global origin under an identity parent,
	# so this is the meaningful, deterministic check.)
	var origin: Vector3 = marker.transform.origin
	if origin.distance_to(Vector3(3.0, 0.0, 5.0)) > ORIGIN_TOLERANCE:
		_fail("FlowMarker local origin=%v, expected (3,0,5)" % origin)
		return

	var marker_id: String = marker.get("id")
	if marker_id != "spawn_point":
		_fail("FlowMarker id=\"%s\", expected \"spawn_point\"" % marker_id)
		return

	# --- Assert record 1: FlowWarp spawned, with forwarded size + transform -----
	var warp = spawned[1]
	if warp == null:
		_fail("spawned[1] is null")
		return
	if not warp.is_class("FlowWarp"):
		_fail("spawned[1] is a %s, expected FlowWarp" % warp.get_class())
		return
	if warp.get_parent() != parent:
		_fail("spawned[1] FlowWarp is not parented under the Node3D parent")
		return

	var warp_origin: Vector3 = warp.transform.origin
	if warp_origin.distance_to(Vector3(-2.0, 1.0, 0.0)) > ORIGIN_TOLERANCE:
		_fail("FlowWarp local origin=%v, expected (-2,1,0)" % warp_origin)
		return

	var warp_size: Vector3 = warp.get("size")
	if warp_size.distance_to(Vector3(4.0, 4.0, 4.0)) > ORIGIN_TOLERANCE:
		_fail("FlowWarp size=%v, expected (4,4,4) — generic property forward failed" % warp_size)
		return

	parent.queue_free()
	print("SMOKE OK: FlowData spawned FlowMarker(id=%s) @ %v and FlowWarp(size=%v) @ %v" % [marker_id, origin, warp_size, warp_origin])
	quit(0)
