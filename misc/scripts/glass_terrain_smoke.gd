# Glass Terrain — headless runtime smoke test.
#
# Run with the editor binary in headless mode:
#   bin/godot.linuxbsd.editor.x86_64 --headless --script misc/scripts/glass_terrain_smoke.gd
#
# Exits 0 (SMOKE OK) when the full GlassTerrain pipeline — island surface build,
# mesh merge, walkable-triangle collision extraction, and the barycentric height
# sampler — produces a real height for a point on a minimal flat square island.
# Exits 1 (SMOKE FAIL) on any miss/null/wrong-height, so CI gates on it.
#
# This deliberately uses ONLY the public GDScript API (GlassTerrain instantiation
# plus the bound setters / rebuild() / sample_height()) and the documented island
# Dictionary schema. rebuild() is called EXPLICITLY because the setters only
# call_deferred a rebuild (and only when inside the tree); the synchronous
# rebuild() is what guarantees the mesh/collision/sampler exist before we sample.
#
# IMPORTANT: locals are intentionally left dynamically typed (no `:=` on the
# GlassTerrain instance). Using static inference like `var t := GlassTerrain.new()`
# makes the GDScript parser HARD-FAIL to load this whole file on any engine build
# where GlassTerrain is not registered — which would bypass the runtime guard
# below and (because Godot does not propagate a script load error to the process
# exit code) could let CI pass silently. Instantiating via ClassDB after an
# explicit class_exists() check keeps the failure inside _initialize() where we
# control quit(1).

extends SceneTree

# GlassHeightSampler::NO_HIT == -1e18. The rest of the codebase treats a value
# below this threshold (-1e17) as "no walkable geometry under this XZ point" —
# which here means the geometry/collision/sampler pipeline produced nothing.
const NO_HIT_THRESHOLD := -1e17

# elevation(1) * level_height(0.5) -> flat top sits at world y = 0.5.
const EXPECTED_HEIGHT := 0.5
# Generous tolerance per the task; the true sample is ~0.5 (well under this).
const HEIGHT_TOLERANCE := 0.6


func _fail(p_msg: String) -> void:
	print("SMOKE FAIL: %s" % p_msg)
	quit(1)


func _initialize() -> void:
	# Guard: the class must be registered/instantiable from GDScript. If the
	# glass_terrain module is missing from the build, fail loudly (don't crash).
	if not ClassDB.class_exists("GlassTerrain"):
		_fail("GlassTerrain class is not registered in this build")
		return
	if not ClassDB.can_instantiate("GlassTerrain"):
		_fail("GlassTerrain is registered but not instantiable")
		return

	# --- Minimal flat square island ----------------------------------------
	# Only "vertices" is load-bearing-required (>= 3 pts). "elevation" and
	# "edge_types" have safe defaults but are set explicitly for determinism:
	#   - elevation = 1  -> flat top Y = elevation * level_height = 0.5
	#   - edge_types all = 2 (EDGE_FLAT) -> no cliff/slope side geometry, just
	#     the flat top, at every elevation.
	# Do NOT set "no_collision": true — that would opt the island out of
	# collision and the height sampler would never be built.
	var island := {
		"vertices": PackedVector2Array([
			Vector2(0.0, 0.0),
			Vector2(8.0, 0.0),
			Vector2(8.0, 8.0),
			Vector2(0.0, 8.0),
		]),
		"elevation": 1,
		"edge_types": PackedInt32Array([2, 2, 2, 2]),
	}

	# Dynamically typed on purpose (see file header) so a missing class fails
	# inside the guard above rather than at parse time.
	var terrain = ClassDB.instantiate("GlassTerrain")
	if terrain == null:
		_fail("ClassDB.instantiate(\"GlassTerrain\") returned null")
		return

	# build_collision must be true (default) or rebuild() returns before the
	# sampler is built. level_height drives the expected top Y. tile_world_size
	# default is 0.5 (grid cell = tile_world_size * 2.0 = 1.0), but set it
	# explicitly so the smoke test does not depend on the C++ default.
	terrain.set_tile_world_size(0.5)
	terrain.set_level_height(0.5)
	terrain.set_build_collision(true)
	terrain.set_islands([island])

	var root := get_root()
	if root == null:
		_fail("SceneTree root is null")
		return
	root.add_child(terrain)

	# Synchronous rebuild — this is what guarantees the mesh/collision/sampler
	# exist before we sample (setters only call_deferred).
	terrain.rebuild()

	# --- Hit: center of the 0..8 square should land on the flat top --------
	var h = terrain.sample_height(Vector2(4.0, 4.0))

	# NO_HIT means the geometry/collision/sampler pipeline produced nothing.
	if h <= NO_HIT_THRESHOLD:
		_fail("sample_height(4,4) returned NO_HIT (%.6e) — sampler/collision/geometry pipeline produced nothing" % h)
		return

	if absf(h - EXPECTED_HEIGHT) > HEIGHT_TOLERANCE:
		_fail("sample_height(4,4)=%.6f, expected ~%.3f (tol %.3f)" % [h, EXPECTED_HEIGHT, HEIGHT_TOLERANCE])
		return

	# --- Miss: a point far outside the footprint must report NO_HIT --------
	# Not strictly required by the task, but it proves the sampler is a real
	# sampler (returns NO_HIT off-mesh) rather than a stub that always hits.
	var miss = terrain.sample_height(Vector2(1000.0, 1000.0))
	if miss > NO_HIT_THRESHOLD:
		_fail("sample_height(1000,1000)=%.6f should be NO_HIT (off-mesh)" % miss)
		return

	print("SMOKE OK: sample_height=%.6f (expected ~%.3f), off-mesh miss=%.6e" % [h, EXPECTED_HEIGHT, miss])
	quit(0)
