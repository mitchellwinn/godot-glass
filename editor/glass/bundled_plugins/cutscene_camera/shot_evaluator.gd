@tool
extends RefCounted
class_name CameraShotEvaluator

# GLASS NOTE: the original addon used `const CS := preload(camera_shot.gd)`
# to dodge a global-class race on a fresh res:// add. For the engine-bundled
# build that race is gone — the bundled loader registers CameraShot's
# class_name FIRST (it's manifest entry 0), so we reference the enums by the
# global `CameraShot` class directly. A preload(res://...) would also FAIL
# here under disabled packs (no FileAccess), so dropping it is mandatory.
## Shared, headless-safe evaluator for a CutsceneCamera's authored `shots`
## timeline. ONE implementation drives BOTH surfaces so they can never diverge:
##   • the editor preview (plugin.gd::_apply_t) — drives the viewport camera
##   • the runtime (cutscene_camera.gd) — drives the live CutsceneCamera in-game
##
## It's a pure function of timeline time `t`: walk the shots in order, chaining
## a running pose / fov / roll, and return the camera state at `t`. Target
## resolution differs per surface (editor resolves NodePaths against the edited
## scene root; runtime resolves via the Entity registry / scene tree), so the
## caller injects a `resolve_target` Callable. STATIC/dolly poses are authored
## in the camera-parent's local space, so a `local_to_world` Callable is injected
## too (pass an invalid Callable to treat poses as world-space).

## Evaluate the whole shots timeline at time `t`. Returns
## {xform: Transform3D, fov: float, roll: float} with roll baked into xform.
##
## resolve_target: Callable(shot, from_xform) -> null | {pos: Vector3, basis: Basis}
##   basis is the target's orientation (for local-space look_at_offset);
##   IDENTITY for focal-point / entity-id targets that have no basis.
## local_to_world: Callable(local_xform: Transform3D) -> Transform3D
static func evaluate_timeline(shots: Array, t: float, base_xform: Transform3D,
		base_fov: float, base_roll: float,
		resolve_target: Callable, local_to_world: Callable) -> Dictionary:
	var running_xform: Transform3D = base_xform
	var running_fov: float = base_fov
	var running_roll: float = base_roll
	var elapsed: float = 0.0
	for i in shots.size():
		var shot = shots[i]
		if shot == null:
			continue
		var dur: float = maxf(shot.duration, 0.0001)
		var shot_end: float = elapsed + dur
		# A trailing looping swing never "completes" — it stays the active shot
		# for all t past its start and cycles forever (the runtime feeds raw,
		# unbounded t; the editor preview just shows one cycle up to its end).
		var is_loop: bool = shot.loop or (shot.type == CameraShot.ShotType.ORBIT_SWING and shot.swing_loop)
		var raw_local: float = (t - elapsed) / dur  # UNclamped (can exceed 1 while looping)
		var in_this_shot: bool = t >= elapsed and (is_loop or t <= shot_end)
		var fully_done: bool = not is_loop and t > shot_end
		var eased_t: float = apply_ease(shot.ease_type, clampf(raw_local, 0.0, 1.0))
		var result: Dictionary = evaluate_shot(
			shot, running_xform, running_fov, running_roll,
			eased_t, fully_done, raw_local, is_loop, resolve_target, local_to_world)
		running_xform = result.xform
		running_fov = result.fov
		running_roll = result.roll
		if in_this_shot:
			break
		elapsed = shot_end
	# Bake roll (about the view axis) into the final basis. Kept OUT of the
	# chain above so it never compounds shot-to-shot.
	return {
		"xform": _with_roll(running_xform, running_roll),
		"fov": running_fov,
		"roll": running_roll,
	}


## One shot's contribution, starting from from_xform/fov/roll at eased progress
## `eased_t` (already eased). Returns the UN-rolled {xform, fov, roll}.
static func evaluate_shot(shot, from_xform: Transform3D, from_fov: float,
		from_roll: float, eased_t: float, fully_done: bool,
		raw_local: float, is_loop: bool,
		resolve_target: Callable, local_to_world: Callable) -> Dictionary:
	var e: float = 1.0 if fully_done else eased_t
	# Progress used for FOV + roll. Normally the shot's eased progress; while
	# looping it follows the shot's LoopStyle so it never pops at the seam.
	var fr_e: float = _loop_factor(shot.loop_style, raw_local, shot.ease_type) if is_loop else e
	# For looping NON-swing shots the same factor drives the POSITION too, so a
	# dolly zooms in/out, a pan goes there-and-back, etc. (ORBIT_SWING keeps its
	# own continuous pendulum below, so leave its position factor alone.)
	if is_loop and shot.type != CameraShot.ShotType.ORBIT_SWING:
		e = fr_e
	var out: Dictionary = {"xform": from_xform, "fov": from_fov, "roll": from_roll}
	# Roll chains for every shot type (eases from running roll to this shot's).
	out.roll = lerpf(from_roll, shot.roll_deg, fr_e)
	match shot.type:
		CameraShot.ShotType.STATIC:
			# Hold at pose (snaps if an explicit pose is authored), but fov +
			# roll animate — this is the in-place "zoom" / tilt.
			if shot.pose != Transform3D.IDENTITY:
				out.xform = _resolve_pose(shot.pose, local_to_world)
			out.fov = lerpf(from_fov, shot.fov, fr_e)
		CameraShot.ShotType.PAN:
			var to_xform := Transform3D(from_xform.basis, shot.target_pos)
			out.xform = from_xform.interpolate_with(to_xform, e)
			out.fov = lerpf(from_fov, shot.fov, fr_e)
		CameraShot.ShotType.PAN_LOCAL:
			var delta_world: Vector3 = from_xform.basis * shot.local_offset
			var to_xform2 := Transform3D(from_xform.basis, from_xform.origin + delta_world)
			out.xform = from_xform.interpolate_with(to_xform2, e)
			out.fov = lerpf(from_fov, shot.fov, fr_e)
		CameraShot.ShotType.LOOK_AT:
			var look_pos = _resolve_aim(shot, from_xform, resolve_target)
			if look_pos != null:
				var target_basis: Basis = Transform3D(from_xform.basis, from_xform.origin) \
					.looking_at(look_pos, Vector3.UP).basis
				out.xform = Transform3D(from_xform.basis.slerp(target_basis, e), from_xform.origin)
			out.fov = lerpf(from_fov, shot.fov, fr_e)
		CameraShot.ShotType.ORBIT:
			var center = _resolve_aim(shot, from_xform, resolve_target)
			if center != null:
				var c: Vector3 = center
				var offset := from_xform.origin - c
				var radius := Vector2(offset.x, offset.z).length()
				var height := offset.y
				var start_angle := atan2(offset.x, offset.z)
				var swept: float = deg_to_rad(shot.orbit_speed) * shot.duration * e
				var ang: float = start_angle + swept
				var pos := c + Vector3(radius * sin(ang), height, radius * cos(ang))
				out.xform = Transform3D.IDENTITY.translated(pos).looking_at(c, Vector3.UP)
			out.fov = lerpf(from_fov, shot.fov, fr_e)
		CameraShot.ShotType.ORBIT_SWING:
			var center2 = _resolve_aim(shot, from_xform, resolve_target)
			if center2 != null:
				var c2: Vector3 = center2
				var offset2 := from_xform.origin - c2
				var radius2 := Vector2(offset2.x, offset2.z).length()
				var height2 := offset2.y
				var center_angle := atan2(offset2.x, offset2.z)
				# While looping, drive the pendulum off RAW progress (constant-
				# speed continuous sine); for a one-shot swing use the eased
				# progress so it eases in/out over its single cycle.
				var phase: float = fposmod(raw_local, 1.0) if is_loop else fposmod(e, 1.0)
				var ang2: float = center_angle - deg_to_rad(shot.swing_amp_deg) * sin(phase * TAU)
				var pos2 := c2 + Vector3(radius2 * sin(ang2), height2, radius2 * cos(ang2))
				out.xform = Transform3D(Transform3D.IDENTITY.looking_at(c2 - pos2, Vector3.UP).basis, pos2)
			out.fov = lerpf(from_fov, shot.fov, fr_e)
		CameraShot.ShotType.DOLLY_ZOOM:
			# Push along forward while counter-animating fov so the subject at
			# d0 keeps its screen size. fov is derived; shot.fov is ignored.
			var forward := -from_xform.basis.z.normalized()
			var d0: float = maxf(shot.dolly_subject_distance, 0.01)
			var push: float = shot.dolly_distance * e
			var d1: float = maxf(d0 - push, 0.01)
			out.xform = Transform3D(from_xform.basis, from_xform.origin + forward * push)
			var new_fov := rad_to_deg(2.0 * atan(tan(deg_to_rad(from_fov) * 0.5) * d0 / d1))
			out.fov = clampf(new_fov, 1.0, 179.0)
	return out


## Easing curves. SMOOTHSTEP matches the legacy hardcoded ease. NOTE: must NOT
## be named `ease` — that shadows GDScript's built-in @GlobalScope.ease(x,curve)
## and silently mis-resolves the call (returns 0 or 1 for all t).
static func apply_ease(ease_type: int, t: float) -> float:
	t = clampf(t, 0.0, 1.0)
	match ease_type:
		CameraShot.EaseType.LINEAR:
			return t
		CameraShot.EaseType.EASE_IN:
			return t * t
		CameraShot.EaseType.EASE_OUT:
			return 1.0 - (1.0 - t) * (1.0 - t)
		CameraShot.EaseType.EASE_IN_OUT:
			return 2.0 * t * t if t < 0.5 else 1.0 - pow(-2.0 * t + 2.0, 2.0) * 0.5
		_:  # SMOOTHSTEP
			return t * t * (3.0 - 2.0 * t)


## FOV/roll progress factor (0..1) for a looping swing, per its LoopStyle.
## All three are continuous at the loop seam (raw_local crossing an integer),
## so fov/roll never pop. `raw_local` is the UNclamped, monotonically-growing
## per-shot progress (0,1,2,… across cycles).
static func _loop_factor(loop_style: int, raw_local: float, ease_type: int) -> float:
	var p: float = fposmod(raw_local, 1.0)  # position within the current cycle
	match loop_style:
		CameraShot.LoopStyle.BREATHE:
			# Smooth sinusoidal pulse base→target→base, peaking mid-cycle.
			return (1.0 - cos(p * TAU)) * 0.5
		CameraShot.LoopStyle.PING_PONG:
			# Eased there-and-back triangle.
			return apply_ease(ease_type, 1.0 - absf(2.0 * p - 1.0))
		_:  # HOLD — ramp to target over the first cycle, then hold forever.
			return apply_ease(ease_type, clampf(raw_local, 0.0, 1.0))


## Apply a shot's look_at_offset to a resolved target position.
static func offset_target(pos: Vector3, shot, target_basis: Basis) -> Vector3:
	var off: Vector3 = shot.look_at_offset
	if off == Vector3.ZERO:
		return pos
	if shot.look_at_offset_local:
		return pos + target_basis * off
	return pos + off


# ─── internals ───────────────────────────────────────────────────────────

# Resolve the aim point (target + offset) via the injected resolver. Returns
# null (no target — caller leaves rotation/orbit untouched) or a Vector3.
static func _resolve_aim(shot, from_xform: Transform3D, resolve_target: Callable) -> Variant:
	if not resolve_target.is_valid():
		return null
	var res = resolve_target.call(shot, from_xform)
	if res == null:
		return null
	var pos: Vector3 = res.get("pos", Vector3.ZERO)
	var basis: Basis = res.get("basis", Basis.IDENTITY)
	return offset_target(pos, shot, basis)


static func _resolve_pose(local_pose: Transform3D, local_to_world: Callable) -> Transform3D:
	if local_to_world.is_valid():
		return local_to_world.call(local_pose)
	return local_pose


# Roll about the camera's view axis (local Z). Camera looks down -Z, so this
# tilts the horizon without changing where it points.
static func _with_roll(xform: Transform3D, roll_deg: float) -> Transform3D:
	if is_zero_approx(roll_deg):
		return xform
	return Transform3D(xform.basis * Basis(Vector3(0, 0, 1), deg_to_rad(roll_deg)), xform.origin)
