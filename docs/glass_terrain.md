# Glass Terrain

Glass Terrain is a native low-poly terrain feature built into Godot Glass — the
Animal-Crossing-style polygon-island heightfield editor, compiled into the engine
(no addon to enable). You author terrain as 2D polygon *islands* with per-edge
profiles; the engine meshes them, builds collision, and answers height queries.

> **Status (incremental port).** The native runtime — the `GlassTerrain` node, the
> mesh/collision/height-sampler pipeline — is in and **runtime-verified** (CI runs a
> headless build-and-sample smoke test on every change). The in-viewport **editor**
> currently provides a rebuild action and an island-outline gizmo; the full island /
> edge / cut / brush authoring tools are landing incrementally. This guide documents
> what exists today and grows with the feature.

---

## The `GlassTerrain` node

Add a **GlassTerrain** node (a `Node3D`) to a scene. It owns the terrain *data*
and, on rebuild, generates its mesh + collision as internal children (which are
never serialized into your scene — the data is the source of truth).

### Properties

| Property | Type | Default | Meaning |
|---|---|---|---|
| `islands` | `Array[Dictionary]` | `[]` | The polygon islands that make up the terrain (see below). |
| `cuts` | `Array[Dictionary]` | `[]` | Boolean cut-outs lowered into the islands. |
| `tile_world_size` | `float` | `0.5` | World size of a texture tile; also drives UV scale and the height-sample grid. |
| `level_height` | `float` | `0.5` | World Y per elevation step (`world_y = elevation * level_height`). |
| `slope_y_threshold` | `float` | `0.15` | A surface counts as *walkable* collision when `abs(normal.y)` exceeds this. |
| `build_collision` | `bool` | `true` | Build a `ConcavePolygonShape3D` body from the walkable triangles. |
| `auto_rebuild` | `bool` | `true` | Rebuild automatically when data changes / on ready (outside the editor). |
| `tile_size` | `int` | `16` | Tiling hint for texture/world mapping. |

### Methods

- **`rebuild()`** — regenerate everything: per-island surfaces → one merged
  `ArrayMesh` → per-surface materials → collision body → height sampler.
- **`sample_height(xz: Vector2) -> float`** — the top-surface world Y at an XZ
  point, or a large-negative sentinel (`< -1e17`) when there is no terrain under
  that point. Built from the collision triangles, so it requires `build_collision`.

---

## The island data model

Each entry in `islands` is a `Dictionary`:

| Key | Type | Meaning |
|---|---|---|
| `vertices` | `PackedVector2Array` | The island polygon in world XZ units (≥ 3 points). |
| `edge_types` | `PackedInt32Array` | One entry per edge: `0` = CLIFF, `1` = SLOPE, `2` = FLAT, `3` = SLOPE_IN. |
| `elevation` | `int` | Elevation step; the island's top sits at `elevation * level_height`. |

A minimal flat square at ground level:

```gdscript
var terrain := GlassTerrain.new()
terrain.islands = [{
    "vertices": PackedVector2Array([Vector2(0,0), Vector2(8,0), Vector2(8,8), Vector2(0,8)]),
    "edge_types": PackedInt32Array([2, 2, 2, 2]), # all FLAT
    "elevation": 1,
}]
add_child(terrain)
terrain.rebuild()
print(terrain.sample_height(Vector2(4, 4))) # -> 0.5  (elevation 1 * level_height 0.5)
```

Edge profiles (cliff faces, ramps, slope-ins), per-edge face settings, and cuts
feed the same kernel; the authoring UI for editing them visually is the in-progress
editor work.

---

## The editor

Select a `GlassTerrain` in a scene:

- A **Rebuild Terrain** button appears in the 3D viewport toolbar — it re-runs
  `rebuild()` on the selected node.
- An **island-outline gizmo** draws each island's polygon outline at its elevation,
  so you can see the terrain footprint in the viewport.

Visual island/edge/cut editing tools and the brush-based painters (autotile, road,
fence, foliage, objects) are being ported into this native editor incrementally.

---

## How it works (pipeline)

`rebuild()` drives the native compute kernels:

1. **`GlassTerrainMeshBuilder.build_island_surfaces`** — per island, triangulate the
   polygon and build ground + edge (cliff/slope) surfaces into per-texture buffers.
2. **`merge_caches`** — merge all islands into one `ArrayMesh` plus a parallel list
   of surface texture keys (used to assign materials).
3. **`extract_walkable_triangles`** — keep up-facing triangles (per
   `slope_y_threshold`) as the collision soup for a `ConcavePolygonShape3D`.
4. **`GlassHeightSampler`** — bucket the collision triangles into a grid so
   `sample_height` is a fast barycentric lookup.

These kernels are native C++; a game that places no `GlassTerrain` runs none of it.
