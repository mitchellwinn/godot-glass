# Glass Tiles

Glass Tiles is a native low-poly **tile** editor built into Godot Glass — a
Sprytile/Crocotile-style painter that places textured quads from a tileset onto a
3D grid. It's the tile-grid sibling to [Glass Terrain](glass_terrain.md), compiled
into the engine (no addon to enable).

> **Status (incremental).** The native runtime — the `GlassTilesMap` node and the
> tileset→quad mesher — is in and **runtime-verified** (CI builds a tile and asserts
> the mesh on every change). The in-viewport **paint/palette editor** (work-plane
> raycast, brush, tile palette dock) is next; this guide documents what exists today
> and grows with the feature. Until the editor lands, tiles are fed as data (below).

---

## The `GlassTilesMap` node

Add a **GlassTilesMap** node (a `Node3D`) to a scene. It owns the tile data and,
on rebuild, generates one merged mesh as an internal child (never serialized — the
data is the source of truth).

### Properties

| Property | Type | Default | Meaning |
|---|---|---|---|
| `tile_data` | `Dictionary` | `{}` | The placed tiles, keyed by cell/axis/layer (see below). |
| `tileset_texture_path` | `String` | `""` | `res://` path to the shared tileset texture (sliced into tiles). |
| `tile_size` | `int` | `16` | Pixel size of one tile cell in the tileset texture. |
| `pixels_per_unit` | `float` | `32.0` | Texture pixels per world unit. |
| `tile_world_size` | `float` | `0.5` | World-unit edge length of one tile cell. |
| `auto_rebuild` | `bool` | `true` | Rebuild automatically when data changes / on ready (outside the editor). |

### Method

- **`rebuild()`** — regenerate the mesh: run the mesher over `tile_data` → one
  `ArrayMesh` on the internal `MeshInstance3D`, with a single unshaded /
  `NEAREST` / alpha-scissor material built from the tileset texture (crisp
  pixel-art, no filtering, cutout transparency).

---

## The tile data model

`tile_data` is a `Dictionary`. Each **key** identifies a cell, the plane it sits
on, and a layer; each **value** is a per-tile `Dictionary`.

**Key format** — `var_to_str(Vector3i(cell)) + ":" + str(axis) + ":" + str(layer)`,
e.g. `"Vector3i(0, 0, 0):0:0"`.

**Per-tile value:**

| Field | Type | Meaning |
|---|---|---|
| `uv` | `Vector2i` | The tile's column/row in the tileset (which cell of the texture to use). |
| `axis` | `int` | The plane the quad lies on — `0` = XZ (floor); wall planes use the other axes. |
| `rotation` | `int` | Quarter-turn rotation of the tile (0–3). |
| `flip_h` | `bool` | Flip horizontally. |
| `flip_v` | `bool` | Flip vertically. |

A single floor tile at the origin, pulling tileset cell (1, 0):

```gdscript
var tiles := GlassTilesMap.new()
tiles.tileset_texture_path = "res://art/tileset.png"
tiles.tile_data = {
    "Vector3i(0, 0, 0):0:0": {
        "uv": Vector2i(1, 0),
        "axis": 0,        # XZ floor
        "rotation": 0,
        "flip_h": false,
        "flip_v": false,
    },
}
add_child(tiles)
tiles.rebuild()
```

The visual painter that writes `tile_data` for you (click-drag on a work plane,
pick from a palette) is the in-progress editor work.

---

## How it works (pipeline)

`rebuild()` drives the native `GlassTilesMesher`: for each entry in `tile_data` it
emits a textured quad — UV-sliced from the tileset (with a half-texel inset to stop
bleeding), positioned per `axis`, rotated/flipped per the tile — into a single
indexed surface, then assigns the unshaded pixel-art material. It's native C++; a
game that places no `GlassTilesMap` runs none of it.

---

## Scope today

This slice is the **textured-quad path**: one tileset texture → one mesh surface.
Freeform vertices, mesh LOD, and collision/navmesh baking land in later slices,
alongside the in-viewport editor. See [Glass Terrain](glass_terrain.md) for the
sibling heightfield editor.
