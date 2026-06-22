# Godot Glass

**Godot Glass** is a fork of [Godot Engine](https://godotengine.org) 4.7 that adds
native, compiled-in modules for visual gameflow authoring, a game-agnostic event
registry, and mesh tooling — no addons to enable.

## Getting Glass

Prebuilt Windows and macOS editors are available on the GitHub Releases page:

**→ [Download the latest Godot Glass editor](https://github.com/mitchellwinn/godot-glass/releases/latest)**

The editor auto-checks for new Glass releases on launch and shows an update notice
in the Project Manager when one is available.

## Glass Flow

Glass Flow is a native gameflow-authoring feature. It lets you lay out the
non-geometry logic of a scene — invisible trigger volumes, named points, teleport
doors, and event triggers — as real scene nodes you place and size visually in the
3D viewport, with a built-in **"Flow"** dock and a box gizmo that feels identical
to native engine shapes. There is no addon to turn on: the Flow nodes and dock are
compiled into the engine.

**→ [Read the Glass Flow guide](docs/glass_flow.md)**

## What Glass adds over Godot

- **`glass_flow`** — visual gameflow authoring: native `FlowRegion`, `FlowMarker`,
  `FlowWarp`, and `FlowEvent` nodes, a built-in Flow dock for spawning them, and a
  3D box gizmo for sizing trigger volumes in the viewport.
- **`glass_events`** — a game-agnostic **event registry** (`GlassEvents`): a
  native singleton where a project registers its own command vocabulary at runtime
  and dispatches commands by string. The engine ships no commands itself.
- **`glass_terrain`** — native mesh tooling for terrain authoring.

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
