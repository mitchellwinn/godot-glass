# Glass Flow

Glass Flow is a native gameflow-authoring feature built into Godot Glass. It lets
you lay out the *non-geometry* logic of a playable scene — trigger volumes, named
points, teleport doors, and event triggers — as real scene nodes you place and
size visually in the 3D viewport. There is **no addon to enable**: the Flow nodes
are compiled into the engine and the Flow dock appears automatically in editor
builds.

This document is engine-level. It describes the four Flow node types, the Flow
dock, the FlowRegion gizmo, and the `GlassEvents` command-registry mechanism. It
does **not** prescribe any specific game commands or content layout — those are
yours to define.

---

## 1. What Glass Flow is

A playable scene is mostly two things: static geometry (meshes, collision,
lights) and *gameflow* — everything that "does something" but isn't visible
geometry: spots where an actor stands, invisible patches that start an event when
the player walks in, doors that send you to another scene, areas that fire a named
game event.

Glass Flow gives you four native nodes for that second category:

| Node | Extends | Role |
|---|---|---|
| `FlowRegion` | `Node3D` | Base box trigger-volume. Re-emits `body_entered` / `body_exited`. |
| `FlowMarker` | `Marker3D` | A named point in space (`id`) — spawn points, POIs, warp targets, camera anchors. |
| `FlowWarp` | `FlowRegion` | Teleports an entering body to a target node. |
| `FlowEvent` | `FlowRegion` | Emits a named `event_triggered` signal when a body enters. |

All four are registered at scene-initialization level, so they exist in both the
editor and exported game builds. The Flow **dock** and the FlowRegion **gizmo**
are editor-only (`TOOLS_ENABLED`).

Because Flow data is just nodes in the scene, you author it the same way you
author anything else in Godot: drag in the viewport, set properties in the
Inspector, undo/redo as usual.

---

## 2. The Flow node types

### 2.1 FlowRegion

The foundation node. A `FlowRegion` is a `Node3D` that carries an **inline box**
directly on the node — there is no child `CollisionShape3D` and no shared
`Shape3D` resource. This is deliberate: the box is a plain `size` value on the
node, so duplicating the node just copies the value (no "Make Unique" footgun, no
accidentally-shared shape).

At **runtime only** — when `NOTIFICATION_READY` fires and `Engine` is *not* in
editor mode — a FlowRegion builds its own detector: an internal `Area3D` plus a
`CollisionShape3D` with a `BoxShape3D` sized from `size`. It then re-emits the
area's `body_entered` / `body_exited` signals as its own. These detector children
are created with `INTERNAL_MODE_BACK`: they never appear in the scene tree and are
never serialized. In the editor, no `Area3D` exists at all — you only ever see and
edit the single clean box.

| Property | Type | Default | Description |
|---|---|---|---|
| `size` | `Vector3` | `(2, 2, 2)` | Inline box dimensions in meters (`suffix:m`). This value — not a child shape — defines the runtime `BoxShape3D` detector. Setting it updates the gizmo. |
| `collision_mask` | `int` | `1` | 3D physics layers the generated detector scans for bodies. If the live `Area3D` already exists, changing this updates it. The detector's own collision *layer* is `0` (it only detects). |

| Signal | Description |
|---|---|
| `body_entered(body: Node3D)` | Re-emitted from the internal `Area3D`. |
| `body_exited(body: Node3D)` | Re-emitted from the internal `Area3D`. |

FlowRegion is the base for `FlowWarp` and `FlowEvent`, which inherit its inline
box, its gizmo, and its trigger detection.

### 2.2 FlowMarker

A named point in space. `FlowMarker` extends `Marker3D`, so it inherits the
crosshair gizmo for free; it adds a single `id` string so warps, events, and game
code can refer to it by name.

| Property | Type | Default | Description |
|---|---|---|---|
| `id` | `String` | `""` | Name/identifier for this marker. A plain stored string with no side effects. |

FlowMarker has no signals and no region/box behavior — it is purely a positioned,
named point. Use it for spawn points, points of interest, warp destinations, and
camera anchors.

### 2.3 FlowWarp

A `FlowRegion` that teleports the entering body to a target node (typically a
`FlowMarker`). It inherits FlowRegion's inline box, box gizmo, and trigger
detection; the only new thing is the teleport behavior.

| Property | Type | Default | Description |
|---|---|---|---|
| `target` | `NodePath` | empty | Path to the destination `Node3D` (hint: valid types = `Node3D`). On entry, the body's `global_position` is set to the target's `global_position`. **Position only — rotation is not copied.** No-op if `target` is empty, null, or resolves to a non-`Node3D`. |

FlowWarp also inherits `size` and `collision_mask`. At ready (runtime, non-editor)
it connects the inherited `body_entered` signal to its teleport handler. It adds no
new signals of its own.

### 2.4 FlowEvent

A `FlowRegion` that fires a **named event** when a body enters. The engine stays
game-agnostic: FlowEvent does not run any game logic itself — it simply emits a
signal carrying a string key, and your game wires that signal to its own dialogue
or event runner.

| Property | Type | Default | Description |
|---|---|---|---|
| `event_key` | `String` | `""` | The named key emitted with `event_triggered`, so the game can dispatch to the right handler. |
| `one_shot` | `bool` | `false` | If true, the event fires only once; after the first body entry, later entries are ignored (guarded by an internal `_fired` flag). |

| Signal | Description |
|---|---|
| `event_triggered(event_key: String, body: Node3D)` | Emitted on body entry, subject to the `one_shot` guard. |

FlowEvent inherits `size` and `collision_mask`. At ready (runtime, non-editor) it
connects the inherited `body_entered` to its handler. The handler emits
`event_triggered(event_key, body)`; if `one_shot` is set and it has already fired,
the entry is ignored.

> **Note:** `_fired` becomes true on the first non-suppressed entry even when
> `one_shot` is `false`. The flag is only *consulted* when `one_shot` is true.

---

## 3. The Flow dock

In editor builds, Glass Flow registers a built-in dock named **"Flow"**. It is a
compiled-in engine panel (not a project addon you toggle in Project Settings), so
it is always present.

- **Where it lives:** it defaults to the right-hand dock column, upper-left slot
  (`DOCK_SLOT_RIGHT_UL`) — the same area the Inspector dock occupies by default.
  Select it via its **"Flow"** tab. Its minimum height is 120px.
- **What it is today:** a node-spawning palette — a header label plus four
  "Add …" buttons. The header reads *"Add Flow nodes to the open scene:"*.

| Control | Action |
|---|---|
| **Add FlowRegion** | Instantiates a `FlowRegion` (box trigger volume), adds it to the open scene root, selects it. |
| **Add FlowMarker** | Adds a `FlowMarker` (named point, crosshair gizmo). |
| **Add FlowWarp** | Adds a `FlowWarp` (teleport region). |
| **Add FlowEvent** | Adds a `FlowEvent` (signal-firing region). |

Each button instantiates the class via `ClassDB`, names the new node after its
class, and adds it as a child of the **scene root** through the editor's
`UndoRedo` manager — so the add is fully undoable/redoable, and because the node
is owned by the scene, it gets saved with the scene. The new node is then
auto-selected so it is immediately ready to edit in the viewport and Inspector.

> The buttons **no-op if there is no open scene root** (nothing is added if no
> scene is being edited). Open a scene first.

---

## 4. The FlowRegion 3D gizmo

Every `FlowRegion` — and therefore every `FlowWarp` and `FlowEvent` — gets a
viewport gizmo that draws and edits its box. It reuses the engine's shared box
gizmo helper (the same one `CollisionShape3D` uses), so the editing feel is
identical to native box shapes.

In the viewport the region is drawn as a **wireframe box** centered on the node,
rendered in light blue (`Color(0.45, 0.85, 1.0)`). The box edges double as
clickable collision segments, so you can click the wireframe to select the node.

**What you can manipulate:** drag the **per-face handles** to resize the box.
Dragging a face grows the box and recenters the node (the gizmo updates both the
region's `size` and its global position). The action is committed through
undo/redo under the label *"Change FlowRegion Size"*, and the drag can be
cancelled.

> The gizmo only manipulates the box's **size and position**. Type-specific
> fields — `FlowWarp.target`, `FlowEvent.event_key` / `one_shot`, `FlowMarker.id`
> — are **not** gizmo-editable; set them in the Inspector.

---

## 5. Walkthrough: author a gameflow scene

1. **Open the scene** you want to author Flow in. (The dock's spawn buttons do
   nothing without an open scene root.)
2. **Open the Flow dock** — the "Flow" tab in the right-hand dock column (its
   default slot, alongside where the Inspector lives).
3. **Add the entities you need** by clicking the matching button:
   - **Region** for an invisible trigger volume,
   - **Marker** for a named point (e.g. a `spawn_point` or `meeting_point`),
   - **Warp** for a doorway to another location,
   - **Event** for a region that fires a named game event.
4. **Size and place each region.** With a Region / Warp / Event selected, drag the
   per-face handles in the 3D viewport to resize the box (or set `size` in the
   Inspector), and move the node to position it. You never touch a
   `CollisionShape3D` — you grab the visualized box.
5. **Position each marker.** A FlowMarker carries the Marker3D crosshair; place it,
   then give it an `id` in the Inspector so warps/events/game code can refer to it
   by name.
6. **Set type-specific properties in the Inspector:**
   - FlowWarp → point `target` at a destination node (typically a FlowMarker).
   - FlowEvent → set `event_key` to your game's event name, and `one_shot` if it
     should fire only once.
   - Adjust `collision_mask` on any region so it detects the right bodies.
7. **Repeat** until the scene's full set of regions, markers, warps, and events is
   laid out. Save the scene — the Flow nodes are owned by the scene and persist
   with it.

At runtime, each region builds its own `Area3D` detector on ready: warps teleport
entering bodies, and events emit `event_triggered`. Your game connects those
signals to its own runners.

---

## 6. GlassEvents — the command registry

`GlassEvents` is a native engine singleton (exposed to GDScript as the global
`GlassEvents`) that gives your project a **command registry and dispatcher**. It
ships with **zero** built-in game commands — your project supplies its entire
command vocabulary at runtime.

Internally it is an `Object` holding a `HashMap<StringName, Callable>` that starts
empty. You populate it with your own commands.

### API

| Call | Purpose |
|---|---|
| `register_event(name, callable)` | Register a command name → a GDScript `Callable` handler. |
| `unregister_event(name)` | Remove a command. |
| `has_event(name)` | Query whether a command is registered. |
| `process_events("name\|arg1\|arg2")` | Dispatch a command string. |

### How dispatch works

`process_events` splits the string on `|`, looks up `parts[0]` in the registry,
and:

- if the name is **not** registered, it warns
  (`[GlassEvents] Unknown event`) and does nothing else;
- otherwise it calls the registered `Callable`, passing the **full split array**
  as a single `Array` argument (so `args[0]` is the command name itself). This
  matches the GDScript handler contract `func(args: Array)`.

There is also an **array-unwrap convention**: if a handler returns an `Array`,
`process_events` returns just its first element (`a[0]`); the rest of the array is
dropped. By convention handlers use this to return `[text, is_blocking]`, but the
engine only ever reads element 0 — the second element is **not** consulted by
`process_events`. A handler returning `["text", false]` or `["text"]` is unwrapped
identically.

### Wiring FlowEvent to GlassEvents

`FlowEvent` and `GlassEvents` are **decoupled** engine primitives — neither
references the other in code. Your project composes them:

- **FlowEvent** = *"a region in the scene → a named signal fires."* On body entry
  it emits `event_triggered(event_key, body)`. It does not call `GlassEvents`.
- **GlassEvents** = *"a named command string → the game's registered handler
  runs."*

A typical bridge: connect `FlowEvent.event_triggered` to your own event/dialogue
runner, which can then feed command strings into `GlassEvents.process_events`. All
semantics — what `event_key` means, what each command does — live entirely in your
project.

> **Hard rule:** the engine ships **no** concrete commands. Any specific command
> (set a flag, switch a camera, start a battle, warp, etc.) is something *your
> project registers* against `GlassEvents` — it is not an engine feature. Treat
> `GlassEvents` as the mechanism and supply your own vocabulary.
