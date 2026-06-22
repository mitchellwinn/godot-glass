# Godot Glass

**Godot Glass** is a fork of [Godot Engine](https://godotengine.org) 4.7 with a
single guiding idea: **anything useful in *any* project belongs in the engine, not
bundled as a per-project plugin.** Generally-useful tooling — gameflow authoring, a
low-poly terrain editor, a tile editor, an event registry, cutscene cameras — is
built in (native C++, or engine-bundled), available in every Glass project with
nothing to enable. Only project-specific code stays a plugin, so it's always clear
what's "every Glass game" versus "this game added it."

Native-by-default has no runtime cost when unused: a registered-but-unplaced native
class runs zero code, and editor tools never ship in exported games — so games that
don't use a feature pay nothing, and games that do get C++ speed.

## Getting Glass

Prebuilt Windows and macOS editors are available on the GitHub Releases page:

**→ [Download the latest Godot Glass editor](https://github.com/mitchellwinn/godot-glass/releases/latest)**

The editor auto-checks for new Glass releases on launch and shows an update notice
in the Project Manager when one is available.

## Native engine features

Each feature below is part of the engine — no addon to enable. Every one has (or
will have) a full how-to guide linked here.

| Feature | What it is | Status | Guide |
|---|---|---|---|
| **Glass Flow** (`glass_flow`) | Visual gameflow authoring — doors, trigger zones, markers, spawn points, camera spots; native trigger nodes + a built-in **Flow** dock + a 3D box gizmo. | Available | [User guide](https://mitchellwinn.github.io/godot-glass/flow.html) |
| **Glass Events** (`glass_events`) | Game-agnostic command registry (`GlassEvents`) — a project registers its own command vocabulary and dispatches by string. Ships zero commands. | Available | — |
| **Glass Terrain** (`glass_terrain`) | Native low-poly **terrain** editor — islands, hills, cliffs, slopes; paint grass, roads, fences, trees. | In progress — native runtime is in & verified; the in-viewport authoring tools are landing incrementally | [User guide](https://mitchellwinn.github.io/godot-glass/terrain.html) |
| **Glass Tiles** (`glass_tiles`) | Native low-poly **tile** editor — paint textured tiles onto a 3D grid (the tile-grid sibling to Glass Terrain). | In progress — native node + mesher are in & verified; the in-viewport paint editor is next | [User guide](https://mitchellwinn.github.io/godot-glass/tiles.html) |
| **Cutscene Camera** | Visual cutscene-camera and shot authoring, in every Glass project. | Available — bundled into the editor (native, no addon) | [User guide](https://mitchellwinn.github.io/godot-glass/camera.html) |

## Built on Godot Engine

Godot Glass is a **fork of Godot Engine**, which is free and open source under the
very permissive [MIT license](https://godotengine.org/license) — no strings
attached, no royalties. All upstream Godot credit and licensing is preserved.

- **Godot Engine:** <https://godotengine.org>
- **Official Godot documentation:** <https://docs.godotengine.org>
- **Godot class reference:** <https://docs.godotengine.org/en/latest/classes/>

Godot was open sourced in [February 2014](https://github.com/godotengine/godot/commit/0b806ee0fc9097fa7bda7ac0109191c9c5e0a1ac);
before that it was developed by [Juan Linietsky](https://github.com/reduz) and
[Ariel Manzur](https://github.com/punto-). It is supported by the
[Godot Foundation](https://godot.foundation/) not-for-profit. Glass builds on
their work and is grateful for it.
