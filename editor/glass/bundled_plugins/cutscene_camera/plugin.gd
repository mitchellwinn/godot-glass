@tool
extends EditorPlugin
## Cutscene Camera Preview — visualize cameras as FOV frustums in the 3D
## viewport and author shot lists inline (without dialogue events) that can
## be scrubbed and played back in-editor.
##
## Architecture:
##   • frustum_gizmo.gd   — EditorNode3DGizmoPlugin drawing the FOV pyramid
##                          (class_name CutsceneFrustumGizmo).
##   • camera_shot.gd     — Resource describing one shot (class_name CameraShot).
##   • shot_evaluator.gd  — shared timeline math (class_name CameraShotEvaluator).
##   • dock.gd            — code-built dock UI (class_name CutsceneCameraDock).
##
## GLASS NOTE: this is the engine-bundled, packs-disabled-safe version of the
## YGBF addon's plugin.gd. ALL five original preloads are gone:
##   - FrustumGizmo/CameraShotScript/ShotEvaluator preloads → class_name refs
##     (CutsceneFrustumGizmo / CameraShot / CameraShotEvaluator), which the
##     bundled loader registers IN ORDER before this script compiles.
##   - DockScene preload of ui/dock.tscn → `CutsceneCameraDock.new()`. A
##     PackedScene cannot be a class_name and won't resolve under disabled
##     packs, so the dock now builds its own UI in code.
##   - CutsceneCameraScript preload of res://scripts/camera/cutscene_camera.gd
##     → DUCK-TYPED (`_is_cutscene_cam`, checks the `shots` property), so the
##     bundle has zero res:// project dependency and compiles in any project.
## This plugin is the bundled EditorPlugin entrypoint: the loader instances it
## and calls add_editor_plugin, and its _enter_tree prints the load marker.

# Untyped so we can call script-defined methods/signals on these without
# fighting the static type checker (the dock script extends VBoxContainer
# and the gizmo extends EditorNode3DGizmoPlugin — neither base type
# declares the project-specific API we use on them here).
var _gizmo
var _dock
var _refresh_timer: Timer

# Playback state — we drive the editor viewport camera ourselves to mimic
# what the in-game CutsceneCamera would do when running the same shot list.
var _playing: bool = false
var _play_t: float = 0.0
var _scrub_t: float = 0.0
# When true, the viewport is being driven by us (scrub or play). We stash
# the original viewport-cam transform so Stop can restore it.
var _drive_active: bool = false
var _viewport_cam_pre_drive_xform: Transform3D
var _viewport_cam_pre_drive_fov: float


## Duck-typed "is this a cutscene camera?" — see the GLASS NOTE above. The
## bundle can't reference the project's CutsceneCamera class, so detect it by
## the `shots` property every CutsceneCamera exposes.
static func _is_cutscene_cam(cam) -> bool:
	return cam != null and cam is Camera3D and cam.get("shots") != null


func _enter_tree() -> void:
	# Glass load marker — the CI editor-load gate greps for this exact string
	# to prove the engine-bundled multi-file camera plugin compiled (all 5
	# sources, class_name cross-refs resolved) AND reached _enter_tree.
	print("[GLASS-CAMERA] cutscene camera editor loaded")

	_gizmo = CutsceneFrustumGizmo.new()
	add_node_3d_gizmo_plugin(_gizmo)

	_dock = CutsceneCameraDock.new()
	_dock.set_plugin(self)
	add_control_to_dock(DOCK_SLOT_RIGHT_BL, _dock)

	_dock.show_frustums_toggled.connect(_on_show_frustums_toggled)
	_dock.far_clip_changed.connect(_on_far_clip_changed)
	_dock.focus_camera_requested.connect(_on_focus_camera_requested)
	_dock.snap_from_view_requested.connect(_on_snap_from_view_requested)
	_dock.scrub_changed.connect(_on_scrub_changed)
	_dock.play_requested.connect(_on_play_requested)
	_dock.stop_requested.connect(_on_stop_requested)
	_dock.export_events_requested.connect(_on_export_events_requested)

	scene_changed.connect(_on_scene_changed)
	get_editor_interface().get_selection().selection_changed.connect(_on_editor_selection_changed)

	# Light periodic poll so newly-added cameras show in the list without
	# requiring a scene reload. Cheap — just enumerates Camera3D nodes.
	_refresh_timer = Timer.new()
	_refresh_timer.wait_time = 1.0
	_refresh_timer.autostart = true
	_refresh_timer.one_shot = false
	_refresh_timer.timeout.connect(_on_refresh_tick)
	add_child(_refresh_timer)

	set_process(false)


func _exit_tree() -> void:
	_stop_drive(true)
	if _refresh_timer:
		# Stop + disconnect BEFORE freeing so a queued timeout can't fire into a
		# half-reloaded plugin instance ("Method not found: _on_refresh_tick").
		_refresh_timer.stop()
		if _refresh_timer.timeout.is_connected(_on_refresh_tick):
			_refresh_timer.timeout.disconnect(_on_refresh_tick)
		_refresh_timer.queue_free()
		_refresh_timer = null
	if _dock:
		remove_control_from_docks(_dock)
		_dock.queue_free()
		_dock = null
	if _gizmo:
		remove_node_3d_gizmo_plugin(_gizmo)
		_gizmo = null


func _process(delta: float) -> void:
	if not _playing:
		return
	_play_t += delta
	var total: float = 0.0
	if _dock:
		total = _dock.get_total_duration()
	if _play_t >= total:
		# A trailing swing_loop shot runs forever — keep playing so the preview
		# shows the loop (and its fov/roll LoopStyle) instead of stopping.
		if _has_looping_tail():
			_apply_t(_play_t)  # evaluator cycles for t past total
			if _dock:
				_dock.sync_scrub_time(total)
			return
		_play_t = total
		_apply_t(_play_t)
		_dock.sync_scrub_time(_play_t)
		_dock.set_status("Playback finished.")
		_playing = false
		set_process(false)
		return
	_apply_t(_play_t)
	if _dock:
		_dock.sync_scrub_time(_play_t)


## True when the last authored shot is a forever-looping swing.
func _has_looping_tail() -> bool:
	if _dock == null:
		return false
	var shots: Array = _dock.get_shots()
	for i in range(shots.size() - 1, -1, -1):
		var s = shots[i]
		if s == null:
			continue
		return s.loop or (s.type == CameraShot.ShotType.ORBIT_SWING and s.swing_loop)
	return false


# ─── Dock event handlers ─────────────────────────────────────────────────

func _on_show_frustums_toggled(v: bool) -> void:
	if _gizmo:
		_gizmo.enabled = v
		_gizmo.refresh_all(get_editor_interface().get_edited_scene_root())


func _on_far_clip_changed(v: float) -> void:
	if _gizmo:
		_gizmo.far_clip = v
		_gizmo.refresh_all(get_editor_interface().get_edited_scene_root())


func _on_focus_camera_requested(cam: Camera3D) -> void:
	var ed_cam := _get_editor_camera_3d()
	if ed_cam == null or cam == null:
		return
	ed_cam.global_transform = cam.global_transform
	ed_cam.fov = cam.fov
	if _dock:
		_dock.set_status("Viewport focused on %s." % cam.name)


func _on_snap_from_view_requested(cam: Camera3D) -> void:
	var ed_cam := _get_editor_camera_3d()
	if ed_cam == null or cam == null:
		return
	var undo := get_undo_redo()
	undo.create_action("Snap %s from view" % cam.name)
	undo.add_do_property(cam, "global_transform", ed_cam.global_transform)
	undo.add_do_property(cam, "fov", ed_cam.fov)
	undo.add_undo_property(cam, "global_transform", cam.global_transform)
	undo.add_undo_property(cam, "fov", cam.fov)
	undo.commit_action()
	# Also bake into the currently-selected CameraShot if there is one — this
	# makes "edit shot pose visually" one click instead of typing numbers.
	if _dock:
		var idx: int = _dock.get_selected_shot_index()
		var shots: Array = _dock.get_shots()
		if idx >= 0 and idx < shots.size():
			var shot: CameraShot = shots[idx]
			if shot:
				shot.pose = cam.transform
				shot.fov = cam.fov
				_dock.set_status("Snapped pose into shot %d." % (idx + 1))


func _on_scrub_changed(t: float) -> void:
	_playing = false
	set_process(false)
	_scrub_t = t
	_apply_t(t)


func _on_play_requested() -> void:
	var cam: Camera3D = null
	if _dock:
		cam = _dock.get_selected_camera()
	if cam == null:
		_dock.set_status("Pick a camera first.")
		return
	if not _is_cutscene_cam(cam):
		_dock.set_status("Selected camera isn't a CutsceneCamera — no shots to play.")
		return
	if _dock.get_shots().is_empty():
		_dock.set_status("Shot list is empty.")
		return
	_play_t = 0.0
	_playing = true
	_start_drive()
	set_process(true)
	_dock.set_status("Playing…")


func _on_stop_requested() -> void:
	_playing = false
	set_process(false)
	_stop_drive(true)
	if _dock:
		_dock.sync_scrub_time(0.0)
		_dock.set_status("Stopped.")


func _on_export_events_requested() -> void:
	var cam: Camera3D = null
	if _dock:
		cam = _dock.get_selected_camera()
	if cam == null:
		_dock.set_status("Pick a camera first.")
		return
	var shots: Array = _dock.get_shots()
	if shots.is_empty():
		_dock.set_status("Nothing to export.")
		return
	var lines: Array[String] = []
	lines.append("`switchCamera|%s`" % cam.name)
	for shot in shots:
		if shot == null:
			continue
		var s: String = shot.to_event_string(cam.name)
		if s == "":
			continue
		lines.append("`%s`" % s)
	var blob := "\n".join(lines)
	DisplayServer.clipboard_set(blob)
	_dock.set_status("Copied %d events to clipboard." % lines.size())


func _on_scene_changed(_root: Node) -> void:
	_stop_drive(true)
	if _dock:
		_dock.refresh_camera_list()


func _on_editor_selection_changed() -> void:
	if _dock == null:
		return
	var sel := get_editor_interface().get_selection().get_selected_nodes()
	for n in sel:
		if n is Camera3D:
			_dock.set_selected_camera(n)
			return


func _on_refresh_tick() -> void:
	if _dock:
		_dock.refresh_camera_list()


# ─── Playback / scrub logic ──────────────────────────────────────────────

## Take over the editor's 3D viewport camera so we can preview shot motion
## without modifying the scene's CutsceneCamera position. We restore the
## viewport's original pose when Stop is pressed.
func _start_drive() -> void:
	if _drive_active:
		return
	var ed_cam := _get_editor_camera_3d()
	if ed_cam == null:
		return
	_viewport_cam_pre_drive_xform = ed_cam.global_transform
	_viewport_cam_pre_drive_fov = ed_cam.fov
	_drive_active = true


func _stop_drive(restore: bool) -> void:
	if not _drive_active:
		return
	if restore:
		var ed_cam := _get_editor_camera_3d()
		if ed_cam:
			ed_cam.global_transform = _viewport_cam_pre_drive_xform
			ed_cam.fov = _viewport_cam_pre_drive_fov
	_drive_active = false


## Evaluate the shot list at time `t` (seconds from shot 0 start) and push
## the resulting pose+fov onto the editor viewport camera. This is the
## "preview" — it never touches the scene CutsceneCamera itself.
func _apply_t(t: float) -> void:
	if _dock == null:
		return
	var cam: Camera3D = _dock.get_selected_camera()
	if cam == null:
		return
	var shots: Array = _dock.get_shots()
	if shots.is_empty():
		return
	if not _drive_active:
		_start_drive()
	var ed_cam := _get_editor_camera_3d()
	if ed_cam == null:
		return
	# Drive the preview through the SAME shared evaluator the runtime uses, so
	# what scrubs here is exactly what plays in-game. Inject the editor-side
	# target resolver (NodePaths vs the edited scene root) and local→world.
	var resolver := func(shot, from_xform): return _resolve_shot_target_position(shot, cam, from_xform)
	var l2w := func(local_xform): return _local_to_world(cam, local_xform)
	var result: Dictionary = CameraShotEvaluator.evaluate_timeline(
		shots, t, cam.global_transform, cam.fov, 0.0, resolver, l2w)
	ed_cam.global_transform = result.xform
	ed_cam.fov = result.fov


## Resolve the shot's aim target for the shared evaluator. Returns `null`
## (no target — evaluator leaves rotation/orbit untouched) or a dict
## `{pos: Vector3, basis: Basis}` (basis = the target's orientation, used for
## a local-space look_at_offset; IDENTITY when there's no node basis).
## Precedence:
##   1. `shot.use_focal_point` → camera focal point at SHOT START (basis = IDENTITY).
##   2. `shot.target_node`     → resolved against the edited scene root.
##   3. `cam.look_at_target`   → legacy scene-level fallback.
func _resolve_shot_target_position(shot, cam: Camera3D, from_xform: Transform3D) -> Variant:
	if shot.get("use_focal_point"):
		var fd_v = cam.get("focus_distance")
		var fd: float = float(fd_v) if fd_v != null else 5.0
		# from_xform.basis.z is the camera's back vector — forward is -z.
		return {"pos": from_xform.origin + (-from_xform.basis.z.normalized()) * fd, "basis": Basis.IDENTITY}
	var np: NodePath = shot.target_node
	if not np.is_empty():
		var scene_root := get_editor_interface().get_edited_scene_root()
		if scene_root:
			var n: Node = scene_root.get_node_or_null(np)
			if n is Node3D:
				return {"pos": (n as Node3D).global_position, "basis": (n as Node3D).global_basis}
	var fallback = cam.get("look_at_target")
	if fallback and fallback is Node3D and is_instance_valid(fallback):
		return {"pos": (fallback as Node3D).global_position, "basis": (fallback as Node3D).global_basis}
	return null


func _local_to_world(cam: Camera3D, local_xform: Transform3D) -> Transform3D:
	# CameraShot.pose is stored relative to the camera node's parent's
	# space (i.e. equivalent to `cam.transform`), so reconstruct world
	# transform via the parent.
	var parent := cam.get_parent() as Node3D
	if parent:
		return parent.global_transform * local_xform
	return local_xform


# ─── Editor viewport helper ──────────────────────────────────────────────

func _get_editor_camera_3d() -> Camera3D:
	var ei := get_editor_interface()
	if not ei.has_method("get_editor_viewport_3d"):
		return null
	var vp = ei.get_editor_viewport_3d()
	if vp == null:
		return null
	if vp is Viewport:
		return (vp as Viewport).get_camera_3d()
	for child in vp.get_children():
		if child is Camera3D:
			return child
	return null
