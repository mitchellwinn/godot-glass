# Glass Tiles

Glass Tiles is a native low-poly **tile editor** in Godot Glass — a
Sprytile/Crocotile-style painter for building rooms, levels, and props out of
textured quads on a 3D grid. It's the tile-grid sibling to
[Glass Terrain](glass_terrain.md), compiled into the engine (no addon to enable).

> **Status.** The native runtime (the mesh tiles bake into) is in and
> runtime-verified. The in-viewport **painting editor** described below is being
> ported to native now — this guide documents the authoring workflow it provides;
> steps tagged *(in progress)* aren't wired into the native editor yet.

---

## Painting tiles

You build a tile map by **painting**, not by writing data:

1. **Add a `GlassTilesMap`** to your scene and point it at a **tileset texture** —
   the sheet your tiles are sliced from (its cell size is a property on the node).
2. **Open the Tiles dock** and pick a tile from the **palette** — the tileset
   shown as a grid; click the cell you want to paint with.
3. **Paint on the work plane.** The cursor snaps to the grid, and the plane
   **auto-orients to floor or wall** based on your camera angle, so you can lay a
   floor and then build walls up off it without changing modes.
   - **Click-drag** to paint a run; **rectangle fill** and **multi-tile brushes**
     for speed; **Erase** and **Fill** tools.
   - **Rotate / flip** the selected tile before placing it.
   - Hold to **corner-snap** for precise alignment.
4. **Stack layers** and switch the work-plane axis to build floors, walls, and
   ceilings into one map.
5. *(In progress)* Drop **GLB architecture and props** onto the grid, and **bake
   collision + navmesh** for the finished map.

The map rebuilds live as you paint — what you see is the baked mesh.

---

## Under the hood (you rarely need this)

A `GlassTilesMap` node holds the painted tiles as data and a native C++ mesher
bakes them into one mesh — unshaded, `NEAREST`-filtered, alpha-scissor, so
pixel-art tiles stay crisp. A game that places no `GlassTilesMap` runs none of it.

For **procedural** generation (scripting a map instead of painting it), the data
model is a `Dictionary` keyed by `"Vector3i(cell):axis:layer"` with per-tile
`{ uv, rotation, flip_h, flip_v }`, and you call `rebuild()`. But for hand-building
levels you'll paint — that's what the editor is for.

---

## Status

- **Runtime mesher** (tileset → baked mesh): done, runtime-verified in CI.
- **Painting editor** (palette, work-plane, brush, rotate/flip): in-progress native port.
- **Architecture/props, collision, navmesh, LOD**: later slices.

See [Glass Terrain](glass_terrain.md) for the sibling heightfield editor.
