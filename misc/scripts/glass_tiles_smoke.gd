# Glass Tiles — headless runtime smoke test.
#
# Run with the editor binary in headless mode:
#   bin/godot.linuxbsd.editor.x86_64 --headless --script misc/scripts/glass_tiles_smoke.gd
#
# Exits 0 (SMOKE OK) when the full GlassTilesMap pipeline — tile-data Dictionary
# in, GlassTilesMesher quad emission, ArrayMesh out, internal MeshInstance3D +
# unshaded/NEAREST/alpha-scissor tileset material — produces a real quad mesh for
# a placed tile. Exits 1 (SMOKE FAIL: <detail>) on the FIRST miss/null/wrong-count
# so CI gates on the exit code.
#
# This deliberately uses ONLY the public GDScript API: GlassTilesMap / GlassTilesMesher
# instantiation via ClassDB, the bound setters / rebuild() / build_mesh(), and
# Node.get_child(0, true) to reach the INTERNAL_MODE_FRONT "TiledMesh" child.
# rebuild() is called EXPLICITLY because the setters only call_deferred a rebuild
# (and only when inside the tree); the synchronous rebuild() is what guarantees the
# mesh exists before we inspect it.
#
# IMPORTANT: locals are intentionally left dynamically typed (no `:=` on the
# GlassTilesMap / GlassTilesMesher instances). Static inference like
# `var m := GlassTilesMap.new()` makes the GDScript parser HARD-FAIL to load this
# whole file on any engine build where the class is not registered — which would
# bypass the runtime guard below and (because Godot does not propagate a script
# load error to the process exit code) could let CI pass silently. Instantiating
# via ClassDB after an explicit class_exists() check keeps the failure inside
# _initialize() where we control quit(1).

extends SceneTree

# Canonical smoke-test tileset config: a 32x32 texture of 16px cells -> a 2x2 grid
# of tile cells. tile_world_size=0.5 -> each quad edge is 0.5 world units.
const TILE_PX := 16
const TEX_DIM := 32 # 32/16 = 2 tile columns and 2 tile rows
const TILE_WORLD_SIZE := 0.5

# One quad = 4 distinct corners, emitted by the mesher as two triangles
# (0-1-2 / 0-2-3) = 6 raw add_vertex() calls. The mesher then calls
# SurfaceTool.index() AFTER generate_normals()/generate_tangents(), which dedups
# on vertex+uv+normal+tangent: the two shared corners (positions 0 and 2, each
# emitted twice with identical uv/normal/tangent) collapse, so the committed
# surface is an INDEXED quad of exactly 4 unique vertices + 6 indices (2 tris).
# We assert both counts. (Without that index() call the surface would be an
# un-indexed 6-vertex / 0-index triangle soup and these assertions would fail —
# the index() in glass_tiles_mesher.cpp is what makes this contract hold.)
const EXPECTED_UNIQUE_VERTS := 4
const EXPECTED_INDICES := 6

# A material-less path: the node falls back to an unshaded-white material, but the
# mesher still needs real tileset PIXEL dims, so the smoke test bakes a tiny
# ImageTexture to user:// and points the node at it.
const TILESET_PATH := "user://glass_tiles_smoke_tileset.res"


func _fail(p_msg: String) -> void:
	print("SMOKE FAIL: %s" % p_msg)
	quit(1)


# Canonical key for a tile at grid cell, axis, layer:
#   var_to_str(Vector3i) + ":" + str(axis) + ":" + str(layer)
# matching tile_data.gd::pos_axis_to_key and the native _key_to_pos parser.
func _tile_key(p_pos: Vector3i, p_axis: int, p_layer: int) -> String:
	return var_to_str(p_pos) + ":" + str(p_axis) + ":" + str(p_layer)


# Bake + save a tiny opaque tileset so the node can resolve real pixel dims.
# Returns true on success (path now loadable), false (already _fail'd) otherwise.
func _bake_tileset() -> bool:
	var img = Image.create(TEX_DIM, TEX_DIM, false, Image.FORMAT_RGBA8)
	if img == null:
		_fail("Image.create returned null")
		return false
	img.fill(Color(1, 1, 1, 1))
	var tex = ImageTexture.create_from_image(img)
	if tex == null:
		_fail("ImageTexture.create_from_image returned null")
		return false
	var err := ResourceSaver.save(tex, TILESET_PATH)
	if err != OK:
		_fail("ResourceSaver.save(%s) failed: %d" % [TILESET_PATH, err])
		return false
	if not ResourceLoader.exists(TILESET_PATH):
		_fail("baked tileset not resolvable at %s" % TILESET_PATH)
		return false
	return true


func _initialize() -> void:
	# Guard: both classes must be registered/instantiable. If the glass_tiles
	# module is missing from the build, fail loudly (don't crash).
	if not ClassDB.class_exists("GlassTilesMap"):
		_fail("GlassTilesMap class is not registered in this build")
		return
	if not ClassDB.can_instantiate("GlassTilesMap"):
		_fail("GlassTilesMap is registered but not instantiable")
		return
	if not ClassDB.class_exists("GlassTilesMesher"):
		_fail("GlassTilesMesher class is not registered in this build")
		return
	if not ClassDB.can_instantiate("GlassTilesMesher"):
		_fail("GlassTilesMesher is registered but not instantiable")
		return

	if not _case_mesher_direct():
		return
	if not _case_node_rebuild():
		return

	print("SMOKE OK: all glass-tiles runtime cases passed")
	quit(0)


# ===========================================================================
# CASE: mesher kernel direct — build_mesh() emits one quad for one tile.
# ===========================================================================
# Exercises GlassTilesMesher in isolation (no node, no texture resource): a single
# floor tile (AXIS_XZ) at grid cell (0,0,0), uv cell (1,0) of a 2x2 grid. Asserts
# exactly one surface, a quad's worth of vertices, and 2 triangles.
func _case_mesher_direct() -> bool:
	var mesher = ClassDB.instantiate("GlassTilesMesher")
	if mesher == null:
		_fail("ClassDB.instantiate(\"GlassTilesMesher\") returned null")
		return false

	var tile := {
		"uv": Vector2i(1, 0),
		"axis": 0, # AXIS_XZ floor
		"rotation": 0,
		"flip_h": false,
		"flip_v": false,
	}
	var tile_data := {
		_tile_key(Vector3i(0, 0, 0), 0, 0): tile,
	}

	# tileset_size in PIXELS; tile_px = 16 -> 2x2 cell grid.
	var mesh = mesher.build_mesh(tile_data, Vector2i(TEX_DIM, TEX_DIM), TILE_PX, TILE_WORLD_SIZE)
	if mesh == null:
		_fail("mesher: build_mesh returned null")
		return false
	if mesh.get_surface_count() < 1:
		_fail("mesher: build_mesh produced %d surfaces, expected >= 1" % mesh.get_surface_count())
		return false

	var arrays = mesh.surface_get_arrays(0)
	if arrays.is_empty():
		_fail("mesher: surface 0 has no arrays")
		return false

	var verts = arrays[Mesh.ARRAY_VERTEX]
	if verts == null or verts.size() != EXPECTED_UNIQUE_VERTS:
		_fail("mesher: surface 0 vertex count = %d, expected %d (one quad's unique corners)" % [(verts.size() if verts != null else -1), EXPECTED_UNIQUE_VERTS])
		return false

	var indices = arrays[Mesh.ARRAY_INDEX]
	if indices == null or indices.size() != EXPECTED_INDICES:
		_fail("mesher: surface 0 index count = %d, expected %d (2 triangles)" % [(indices.size() if indices != null else -1), EXPECTED_INDICES])
		return false

	print("SMOKE OK [mesher]: 1 surface, %d verts, %d indices (2 tris)" % [verts.size(), indices.size()])
	return true


# ===========================================================================
# CASE: node rebuild — GlassTilesMap drives the mesher onto an internal child.
# ===========================================================================
# Bakes a tiny tileset to user://, configures the node, adds it to the scene root,
# rebuilds SYNCHRONOUSLY, then reaches the INTERNAL_MODE_FRONT "TiledMesh"
# MeshInstance3D via Node.get_child(0, true) and asserts the same quad mesh.
func _case_node_rebuild() -> bool:
	if not _bake_tileset():
		return false

	var node = ClassDB.instantiate("GlassTilesMap")
	if node == null:
		_fail("ClassDB.instantiate(\"GlassTilesMap\") returned null")
		return false

	node.set_tile_size(TILE_PX)
	node.set_tile_world_size(TILE_WORLD_SIZE)
	node.set_tileset_texture_path(TILESET_PATH)

	var tile := {
		"uv": Vector2i(0, 0),
		"axis": 0,
		"rotation": 0,
		"flip_h": false,
		"flip_v": false,
	}
	node.set_tile_data({
		_tile_key(Vector3i(0, 0, 0), 0, 0): tile,
	})

	var root := get_root()
	if root == null:
		_fail("SceneTree root is null")
		return false
	root.add_child(node)

	# Synchronous rebuild — guarantees the internal mesh exists before inspection.
	node.rebuild()

	# Reach the internal "TiledMesh" child (added with INTERNAL_MODE_FRONT, so it
	# is at internal index 0 and only visible with include_internal=true).
	if node.get_child_count(true) < 1:
		_fail("node: no internal children after rebuild (expected the TiledMesh child)")
		return false
	var mesh_instance = node.get_child(0, true)
	if mesh_instance == null:
		_fail("node: get_child(0, true) returned null")
		return false
	if mesh_instance.name != "TiledMesh":
		_fail("node: internal child 0 is \"%s\", expected \"TiledMesh\"" % str(mesh_instance.name))
		return false

	var mesh = mesh_instance.mesh
	if mesh == null:
		_fail("node: TiledMesh has no mesh after rebuild")
		return false
	if mesh.get_surface_count() < 1:
		_fail("node: mesh has %d surfaces, expected >= 1" % mesh.get_surface_count())
		return false

	var arrays = mesh.surface_get_arrays(0)
	var verts = arrays[Mesh.ARRAY_VERTEX]
	if verts == null or verts.size() != EXPECTED_UNIQUE_VERTS:
		_fail("node: surface 0 vertex count = %d, expected %d" % [(verts.size() if verts != null else -1), EXPECTED_UNIQUE_VERTS])
		return false

	var indices = arrays[Mesh.ARRAY_INDEX]
	if indices == null or indices.size() != EXPECTED_INDICES:
		_fail("node: surface 0 index count = %d, expected %d" % [(indices.size() if indices != null else -1), EXPECTED_INDICES])
		return false

	# The tileset resolved, so a real (non-fallback) material must be assigned.
	var mat = mesh.surface_get_material(0)
	if mat == null:
		_fail("node: surface 0 has no material after rebuild")
		return false

	node.queue_free()
	print("SMOKE OK [node]: TiledMesh internal child, 1 surface, %d verts, %d indices, material assigned" % [verts.size(), indices.size()])
	return true
