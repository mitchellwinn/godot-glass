@tool
extends Resource
class_name CameraShot
## A single authored shot on a CutsceneCamera. Mirrors the dialogue-event
## vocabulary so the same camera can be driven either by events (legacy) or
## by a list of these resources (new authoring surface).
##
## Each shot describes one operation. Lay them end-to-end on a
## CutsceneCamera's `shots` array and call `play_shots()` to run them in
## sequence at runtime, or scrub them via the editor dock to preview.
##
## GLASS NOTE: bundled verbatim from the YGBF addon — it is a pure-data
## leaf Resource with `class_name CameraShot` and zero dependencies, so it
## loads FIRST in the bundled manifest and every later script resolves it
## by class_name.

## NEVER reorder these — saved scenes store the enum as an int. Append only.
enum ShotType {
	STATIC,       ## Hold at this pose for `duration` seconds.
	PAN,          ## Slide world position to `target_pos` over `duration`.
	PAN_LOCAL,    ## Slide along camera-local axes by `local_offset`.
	LOOK_AT,      ## Track an entity (or clear via empty `entity_id`).
	ORBIT,        ## Continuous orbit at `orbit_speed` deg/sec.
	ORBIT_SWING,  ## Pendulum swing `swing_amp_deg` over `duration`.
	DOLLY_ZOOM,   ## Vertigo: push in/out while counter-animating FOV.
}

## Interpolation curve applied to position, rotation AND fov over the shot.
## SMOOTHSTEP is the legacy default (= the old hardcoded ease). Append only.
enum EaseType { SMOOTHSTEP, LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT }

## How FOV and roll behave while a looping swing (ORBIT_SWING + swing_loop)
## repeats forever. Append only.
##   HOLD      — ease to the target once, then hold it while the swing loops.
##   BREATHE   — smooth sinusoidal pulse base↔target every cycle (peaks mid-swing).
##   PING_PONG — ease out to target then back to base, repeating (eased triangle).
## (Position always swings as a continuous pendulum regardless of this.)
enum LoopStyle { HOLD, BREATHE, PING_PONG }

@export var name: String = "Shot"
@export var type: ShotType = ShotType.STATIC

## How long this shot runs / interpolates over (seconds).
@export_range(0.0, 60.0, 0.01, "or_greater") var duration: float = 1.0

## Easing curve for this shot's interpolation (position, rotation, fov, roll).
@export var ease_type: EaseType = EaseType.SMOOTHSTEP

## Pose captured for STATIC / PAN starting reference. The dock's "Snap from
## View" button writes the editor viewport pose into here.
@export var pose: Transform3D = Transform3D.IDENTITY
@export_range(10.0, 120.0, 0.5) var fov: float = 70.0

## Camera roll / Dutch tilt (degrees) animated to over the shot. Chained like
## fov — each shot eases from the running roll to this value. 0 = level.
@export_range(-180.0, 180.0, 0.5) var roll_deg: float = 0.0

## Loop this shot forever (any type). With BREATHE/PING_PONG the whole shot
## oscillates (a dolly zooms in/out, a pan goes there-and-back, fov/roll pulse);
## HOLD just plays once and holds. For ORBIT_SWING the position is always a
## continuous pendulum and loop_style only governs fov/roll. (ORBIT_SWING also
## still honors its legacy `swing_loop` flag.)
@export var loop: bool = false

## PAN target — world position to slide to.
@export var target_pos: Vector3 = Vector3.ZERO

## PAN_LOCAL — delta in the camera's local axes at shot start.
@export var local_offset: Vector3 = Vector3.ZERO

## LOOK_AT / ORBIT / ORBIT_SWING — node in the scene to track or orbit
## around. Resolved relative to the camera's owner (i.e. the edited scene
## root). When set, takes precedence over `entity_id` and over the
## camera's scene-level `look_at_target` fallback. Editor-friendly: it
## works without the Entity registry being populated.
@export var target_node: NodePath = NodePath()

## When true, the shot ignores `target_node` / `entity_id` / camera fallback
## and uses the camera's focal point as the target. The focal point is the
## world position `focus_distance` units in front of the camera at SHOT
## START — orbits and swings then revolve around that fixed point rather
## than chasing the moving camera. Lets you author "look-at" / "orbit"
## shots without wiring a Node3D into the scene.
@export var use_focal_point: bool = false

## LOOK_AT / ORBIT / ORBIT_SWING — offset added to the resolved target before
## the camera aims/orbits. e.g. +Y to look at an entity's head instead of its
## origin. When `look_at_offset_local` is true the offset is rotated by the
## target node's basis (so +Z tracks where the target faces); otherwise it's a
## fixed world-space delta. Ignored for focal-point / entity-id targets (no
## basis) — there it's always treated as world-space.
@export var look_at_offset: Vector3 = Vector3.ZERO
@export var look_at_offset_local: bool = false

## LOOK_AT — Entity.id of the entity to track (runtime fallback when
## `target_node` is empty). Empty = clear tracking.
## "none" = disable_tracking() (ignore even the scene-level fallback).
@export var entity_id: String = ""

## ORBIT — degrees per second; sign sets direction. 0 = stop_orbit().
@export var orbit_speed: float = 0.0

## ORBIT_SWING — half-amplitude in degrees and loop toggle.
@export_range(0.0, 360.0, 0.5) var swing_amp_deg: float = 60.0
@export var swing_loop: bool = false

## ORBIT_SWING + swing_loop — how fov/roll animate while it loops (see LoopStyle).
@export var loop_style: LoopStyle = LoopStyle.HOLD

## DOLLY_ZOOM (vertigo) — push the camera `dolly_distance` metres along its
## forward (+ = toward the subject) while widening/narrowing the FOV so the
## subject at `dolly_subject_distance` keeps the same on-screen size. The
## background appears to warp. fov is derived, so the shot's `fov` is ignored.
@export_range(-100.0, 100.0, 0.1, "or_greater") var dolly_distance: float = 0.0
@export_range(0.1, 500.0, 0.1, "or_greater") var dolly_subject_distance: float = 5.0


## Convert this shot into the equivalent dialogue-event string. Useful for
## "export to XML" — lets you author visually then paste into a dialogue.
func to_event_string(camera_name: String = "") -> String:
	match type:
		ShotType.STATIC:
			# Static is implicit — switching to the camera holds its pose.
			# Emitting `switchCamera|name` is the closest equivalent.
			if camera_name != "":
				return "switchCamera|%s" % camera_name
			return ""
		ShotType.PAN:
			return "cameraPan|%.3f|%.3f|%.3f|%.2f" % [target_pos.x, target_pos.y, target_pos.z, duration]
		ShotType.PAN_LOCAL:
			return "cameraPanLocal|%.3f|%.3f|%.3f|%.2f" % [local_offset.x, local_offset.y, local_offset.z, duration]
		ShotType.LOOK_AT:
			if entity_id == "":
				return "cameraLookAt"
			return "cameraLookAt|%s" % entity_id
		ShotType.ORBIT:
			if orbit_speed == 0.0:
				return "cameraOrbit|stop"
			return "cameraOrbit|%.2f" % orbit_speed
		ShotType.ORBIT_SWING:
			if swing_loop:
				return "cameraOrbitSwing|%.2f|%.2f|loop" % [swing_amp_deg, duration]
			return "cameraOrbitSwing|%.2f|%.2f" % [swing_amp_deg, duration]
		ShotType.DOLLY_ZOOM:
			# No dialogue-event equivalent — dolly-zoom is authored-only.
			return ""
	return ""
