#pragma once

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "scene/resources/surface_tool.h"

// GlassTilesMesher
//
// Native port of addons/mahou_tiled/core/mesh_builder.gd (the CORE textured-quad
// path). Emits one textured quad per tile into a single SurfaceTool surface and
// commits it to an ArrayMesh.
//
// One node = one tileset texture = one mesh surface. Each tile is a Dictionary
// record (see tile_data.gd::make_tile):
//   "uv"       Vector2i  tileset CELL coord (column,row), NOT pixels.
//   "axis"     int       0=XZ floor/ceiling, 1=XY front/back wall, 2=YZ side wall.
//   "rotation" int       90-degree steps (used mod 4), rotates UV-to-corner map.
//   "flip_h"   bool
//   "flip_v"   bool
//   "offset"   Vector3   OPTIONAL (h,v,depth) in PIXELS; Vector2 (h,v) accepted.
//   "layer"    int       OPTIONAL z-fight nudge + (future) overlay tint.
//   "vertices" PackedVector3Array OPTIONAL freeform override — OUT OF SCOPE here;
//              records carrying it are skipped by this slice.
//
// The grid cell comes from the Dictionary key (var_to_str(Vector3i):axis:layer),
// parsed by _key_to_pos(). Design mirrors GlassTerrainMeshBuilder: the mesher is
// fed data explicitly (it never reaches back into the node), so it is pure and
// unit-testable. SurfaceTool usage (begin / set_uv+add_vertex / generate_normals
// / generate_tangents / index / commit) mirrors the terrain builder's _add_quad,
// with a trailing index() so the committed surface is indexed (4 unique verts +
// 6 indices per quad) rather than a 6-vertex un-indexed triangle soup.
class GlassTilesMesher : public RefCounted {
	GDCLASS(GlassTilesMesher, RefCounted);

	// Axis constants (mirror tile_data.gd AXIS_XZ / AXIS_XY / AXIS_YZ).
	enum {
		AXIS_XZ = 0, // floor / ceiling (lies flat on XZ), normal UP
		AXIS_XY = 1, // front / back wall (faces -Z), normal BACK
		AXIS_YZ = 2, // left / right wall (faces -X), normal LEFT
	};

	// Parse the grid cell out of a composite tile key
	// "var_to_str(Vector3i):axis:layer". Returns false if the key is freeform or
	// otherwise unparseable.
	static bool _key_to_pos(const String &p_key, Vector3i &r_pos);

	// Per-axis CCW corner quad at grid_pos (origin = Vector3(grid_pos) * size).
	static void _get_quad_corners(const Vector3i &p_grid_pos, int p_axis, double p_size, Vector3 r_corners[4]);

	// Per-axis outward normal (XZ=UP, XY=BACK, YZ=LEFT) for the layer z-fight nudge.
	static Vector3 _get_axis_normal(int p_axis);

	// Cell rect in [0,1] UV space, shrunk by half a texel on every edge.
	static Rect2 _get_uv_rect(const Vector2i &p_uv, int p_tiles_x, int p_tiles_y, const Vector2 &p_half_texel);

	// Cycle the 4 corner UVs by rotation steps (90-degree increments).
	static void _rotate_corners(Vector3 r_corners[4], int p_rotation);

	// Emit one quad (two CCW triangles 0-1-2 / 0-2-3) with UVs from p_uv_rect.
	static void _add_quad(const Ref<SurfaceTool> &p_st, const Vector3 p_corners[4], const Rect2 &p_uv_rect);

protected:
	static void _bind_methods();

public:
	// Build a single-surface ArrayMesh from the tile data.
	//   p_tile_data       : Dictionary keyed by "Vector3i(...):axis:layer".
	//   p_tileset_size    : tileset texture size in PIXELS (Vector2i(width,height)).
	//   p_tile_px         : pixel size of one tile cell.
	//   p_tile_world_size : world units per tile cell edge.
	// Returns a committed Ref<ArrayMesh> (0 surfaces if no tiles emitted).
	Ref<ArrayMesh> build_mesh(const Dictionary &p_tile_data, const Vector2i &p_tileset_size, int p_tile_px, double p_tile_world_size);

	GlassTilesMesher();
};
