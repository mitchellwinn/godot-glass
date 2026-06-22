@tool
extends VBoxContainer
class_name CutsceneCameraDock
## Dock UI for the Cutscene Camera Preview plugin.
##
## - Lists every Camera3D in the current edited scene.
## - Toggle FOV frustum gizmo visibility for all of them.
## - For the selected camera (must be a CutsceneCamera): edit the `shots`
##   array of CameraShot resources, scrub a timeline, or play it back
##   through the editor viewport.
##
## The plugin is the orchestrator — this dock raises signals to it and asks
## it for the editor viewport camera / scene root / undo-redo handle.
##
## GLASS NOTE: the original addon's dock was backed by `ui/dock.tscn`
## (a PackedScene the plugin `preload`+`instantiate`d) and bound its widgets
## via `@onready $Path`. A res:// PackedScene CANNOT be bundled into the
## engine and does NOT load under disabled packs, so this version BUILDS ITS
## OWN UI tree in `_build_ui()` (called from `_init`) — the dock is now
## GDScript-only and the plugin does `CutsceneCameraDock.new()`. The node
## tree (names + hierarchy) mirrors the old dock.tscn 1:1 so behaviour is
## unchanged. The two preloads (camera_shot.gd, cutscene_camera.gd) are also
## gone: CameraShot is referenced by class_name, and CutsceneCamera is
## DUCK-TYPED away (`_is_cutscene_cam`) so the bundle has zero res:// deps.

signal show_frustums_toggled(value: bool)
signal far_clip_changed(value: float)
signal focus_camera_requested(camera: Camera3D)
signal snap_from_view_requested(camera: Camera3D)
signal scrub_changed(t: float)
signal play_requested()
signal stop_requested()
signal export_events_requested()

# Set by the plugin in _enter_tree (via set_plugin) so we can resolve
# scene-tree queries against the *edited* scene (not the editor's own UI
# tree). Stored via setter so cross-script assignment doesn't hit Godot's
# typed-property rejection path.
var editor_plugin

func set_plugin(p) -> void:
	editor_plugin = p

var _selected_camera: Camera3D = null
var _selected_shot_index: int = -1
# Total duration of the current shot list — recomputed every refresh.
var _total_duration: float = 0.0

# Widgets — assigned in _build_ui() (no @onready / no .tscn). Same names as
# the old dock.tscn node paths for traceability.
var show_frustums_box: CheckBox
var far_clip_spin: SpinBox
var camera_list: ItemList
var focus_btn: Button
var snap_btn: Button
var shot_list: ItemList
var add_shot_menu: MenuButton
var remove_shot_btn: Button
var duplicate_shot_btn: Button
var move_up_btn: Button
var move_down_btn: Button
var params_box: VBoxContainer
var scrubber: HSlider
var scrub_time_label: Label
var play_btn: Button
var stop_btn: Button
var export_btn: Button
var status_label: Label


func _init() -> void:
	name = "CutsceneCameras"
	_build_ui()


## Duck-typed "is this a cutscene camera?" — the bundle has no access to the
## project's CutsceneCamera class_name, so detect it by the `shots` property
## (returns null when absent on a plain Camera3D).
static func _is_cutscene_cam(cam) -> bool:
	return cam != null and cam is Camera3D and cam.get("shots") != null


## Build the entire dock UI tree in code (replaces the old dock.tscn). Node
## names match the original scene so any future debugging maps cleanly.
func _build_ui() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL

	show_frustums_box = CheckBox.new()
	show_frustums_box.name = "ShowFrustums"
	show_frustums_box.text = "Show frustums"
	show_frustums_box.button_pressed = true
	add_child(show_frustums_box)

	var far_clip_row := HBoxContainer.new()
	far_clip_row.name = "FarClipRow"
	add_child(far_clip_row)
	var far_clip_label := Label.new()
	far_clip_label.text = "Far clip"
	far_clip_label.custom_minimum_size = Vector2(140, 0)
	far_clip_row.add_child(far_clip_label)
	far_clip_spin = SpinBox.new()
	far_clip_spin.name = "FarClip"
	far_clip_spin.min_value = 0.5
	far_clip_spin.max_value = 1000.0
	far_clip_spin.step = 0.5
	far_clip_spin.value = 6.0
	far_clip_spin.allow_greater = true
	far_clip_spin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	far_clip_row.add_child(far_clip_spin)

	var cameras_label := Label.new()
	cameras_label.text = "Cameras"
	add_child(cameras_label)

	camera_list = ItemList.new()
	camera_list.name = "CameraList"
	camera_list.custom_minimum_size = Vector2(0, 120)
	camera_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(camera_list)

	var camera_buttons_row := HBoxContainer.new()
	camera_buttons_row.name = "CameraButtonsRow"
	add_child(camera_buttons_row)
	focus_btn = Button.new()
	focus_btn.name = "FocusInViewport"
	focus_btn.text = "Focus in viewport"
	focus_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	camera_buttons_row.add_child(focus_btn)
	snap_btn = Button.new()
	snap_btn.name = "SnapFromView"
	snap_btn.text = "Snap from view"
	snap_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	camera_buttons_row.add_child(snap_btn)

	add_child(HSeparator.new())

	var shots_label := Label.new()
	shots_label.text = "Shots"
	add_child(shots_label)

	shot_list = ItemList.new()
	shot_list.name = "ShotList"
	shot_list.custom_minimum_size = Vector2(0, 120)
	shot_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(shot_list)

	var shot_buttons_row := HBoxContainer.new()
	shot_buttons_row.name = "ShotButtonsRow"
	add_child(shot_buttons_row)
	add_shot_menu = MenuButton.new()
	add_shot_menu.name = "AddShot"
	add_shot_menu.text = "Add"
	add_shot_menu.flat = false
	shot_buttons_row.add_child(add_shot_menu)
	remove_shot_btn = Button.new()
	remove_shot_btn.name = "RemoveShot"
	remove_shot_btn.text = "Remove"
	shot_buttons_row.add_child(remove_shot_btn)
	duplicate_shot_btn = Button.new()
	duplicate_shot_btn.name = "DuplicateShot"
	duplicate_shot_btn.text = "Duplicate"
	shot_buttons_row.add_child(duplicate_shot_btn)
	move_up_btn = Button.new()
	move_up_btn.name = "MoveUp"
	move_up_btn.text = "↑"
	shot_buttons_row.add_child(move_up_btn)
	move_down_btn = Button.new()
	move_down_btn.name = "MoveDown"
	move_down_btn.text = "↓"
	shot_buttons_row.add_child(move_down_btn)

	# Shot parameter panel — a scrollable VBox the inline editors rebuild into.
	var shot_params := VBoxContainer.new()
	shot_params.name = "ShotParams"
	shot_params.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(shot_params)
	var params_scroll := ScrollContainer.new()
	params_scroll.name = "ParamsScroll"
	params_scroll.custom_minimum_size = Vector2(0, 180)
	params_scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	params_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	params_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	shot_params.add_child(params_scroll)
	params_box = VBoxContainer.new()
	params_box.name = "ShotParamsBox"
	params_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	params_scroll.add_child(params_box)

	add_child(HSeparator.new())

	scrubber = HSlider.new()
	scrubber.name = "Scrubber"
	scrubber.min_value = 0.0
	scrubber.max_value = 1.0
	scrubber.step = 0.001
	scrubber.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	add_child(scrubber)

	scrub_time_label = Label.new()
	scrub_time_label.name = "ScrubTimeLabel"
	scrub_time_label.text = "t = 0.00s / 0.00s"
	add_child(scrub_time_label)

	var playback_row := HBoxContainer.new()
	playback_row.name = "PlaybackRow"
	add_child(playback_row)
	play_btn = Button.new()
	play_btn.name = "Play"
	play_btn.text = "Play"
	play_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	playback_row.add_child(play_btn)
	stop_btn = Button.new()
	stop_btn.name = "Stop"
	stop_btn.text = "Stop"
	stop_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	playback_row.add_child(stop_btn)

	export_btn = Button.new()
	export_btn.name = "ExportEventsButton"
	export_btn.text = "Copy events to clipboard"
	add_child(export_btn)

	status_label = Label.new()
	status_label.name = "Status"
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(status_label)


func _ready() -> void:
	show_frustums_box.toggled.connect(_on_show_frustums)
	far_clip_spin.value_changed.connect(_on_far_clip_changed)
	camera_list.item_selected.connect(_on_camera_selected)
	focus_btn.pressed.connect(_on_focus_pressed)
	snap_btn.pressed.connect(_on_snap_pressed)
	shot_list.item_selected.connect(_on_shot_selected)
	remove_shot_btn.pressed.connect(_on_remove_shot)
	duplicate_shot_btn.pressed.connect(_on_duplicate_shot)
	move_up_btn.pressed.connect(_on_move_up)
	move_down_btn.pressed.connect(_on_move_down)
	scrubber.value_changed.connect(_on_scrub)
	play_btn.pressed.connect(func(): play_requested.emit())
	stop_btn.pressed.connect(func(): stop_requested.emit())
	export_btn.pressed.connect(func(): export_events_requested.emit())

	# Populate the Add Shot menu — collapse PAN_LOCAL under "Pan" so the
	# space (world/local) is a parameter inside the shot rather than its
	# own top-level type. PAN_LOCAL is still its own ShotType under the
	# hood for runtime compat.
	var pop := add_shot_menu.get_popup()
	for i in CameraShot.ShotType.size():
		if i == CameraShot.ShotType.PAN_LOCAL:
			continue
		pop.add_item(_shot_type_label(i), i)
	pop.id_pressed.connect(_on_add_shot)

	refresh_camera_list()


# ─── Public API used by plugin.gd ─────────────────────────────────────────

## Repopulate the camera list from the currently edited scene. Called on
## scene change and on a timer so newly-added cameras show up.
func refresh_camera_list() -> void:
	if not is_inside_tree():
		return
	camera_list.clear()
	var scene_root := _get_edited_scene_root()
	if scene_root == null:
		_set_status("(open a 3D scene to begin)")
		return
	var cams := _collect_cameras(scene_root)
	for cam in cams:
		var label: String = cam.name
		if _is_cutscene_cam(cam):
			label = "[C] " + label
		camera_list.add_item(label)
		camera_list.set_item_metadata(camera_list.item_count - 1, cam)
		camera_list.set_item_tooltip(camera_list.item_count - 1, str(cam.get_path()))
	if _selected_camera and is_instance_valid(_selected_camera):
		# Reselect the previously-selected camera if it's still around.
		for i in camera_list.item_count:
			if camera_list.get_item_metadata(i) == _selected_camera:
				camera_list.select(i)
				break


func set_selected_camera(cam: Camera3D) -> void:
	_selected_camera = cam
	_selected_shot_index = -1
	_refresh_shot_list()
	_refresh_scrubber_range()


func get_selected_camera() -> Camera3D:
	return _selected_camera


func get_selected_shot_index() -> int:
	return _selected_shot_index


func get_shots() -> Array:
	if _selected_camera == null:
		return []
	if not _is_cutscene_cam(_selected_camera):
		return []
	var shots = _selected_camera.get("shots")
	if shots == null:
		return []
	return shots


func set_status(s: String) -> void:
	_set_status(s)


func get_total_duration() -> float:
	return _total_duration


func sync_scrub_time(t: float) -> void:
	# Called by plugin during playback so the slider visually tracks.
	scrubber.set_value_no_signal(t)
	_update_scrub_label(t)


# ─── Internal ─────────────────────────────────────────────────────────────

func _collect_cameras(node: Node) -> Array:
	var out: Array = []
	if node is Camera3D:
		out.append(node)
	for child in node.get_children():
		out.append_array(_collect_cameras(child))
	return out


func _get_edited_scene_root() -> Node:
	if editor_plugin == null:
		return null
	var ei = editor_plugin.get_editor_interface()
	return ei.get_edited_scene_root()


func _refresh_shot_list() -> void:
	shot_list.clear()
	_total_duration = 0.0
	var shots := get_shots()
	for i in shots.size():
		var shot: CameraShot = shots[i]
		var lbl := "%d. %s" % [i + 1, _shot_summary(shot)]
		shot_list.add_item(lbl)
		if shot:
			_total_duration += shot.duration
	_refresh_scrubber_range()
	# Re-select the previously-selected shot so the inline param panel
	# keeps its content across refreshes.
	if _selected_shot_index >= 0 and _selected_shot_index < shots.size():
		shot_list.select(_selected_shot_index)
	_rebuild_params_panel()


## Build a one-line summary of a shot for the list. Includes the key
## parameter so the timeline reads at a glance.
func _shot_summary(shot: CameraShot) -> String:
	if shot == null:
		return "<null shot>"
	var dur := " · %.2fs" % shot.duration
	match shot.type:
		CameraShot.ShotType.STATIC:
			return "Static" + dur
		CameraShot.ShotType.PAN:
			var v: Vector3 = shot.target_pos
			return "Pan → (%.1f, %.1f, %.1f)%s" % [v.x, v.y, v.z, dur]
		CameraShot.ShotType.PAN_LOCAL:
			var o: Vector3 = shot.local_offset
			return "Pan +(%.1f, %.1f, %.1f) local%s" % [o.x, o.y, o.z, dur]
		CameraShot.ShotType.LOOK_AT:
			var label := _target_label(shot)
			return "Look At → %s%s" % [label, dur]
		CameraShot.ShotType.ORBIT:
			var around := _target_label(shot)
			return "Orbit %s @ %.0f°/s%s" % [around, shot.orbit_speed, dur]
		CameraShot.ShotType.ORBIT_SWING:
			var loop_tag := " [loop]" if shot.swing_loop else ""
			var around2 := _target_label(shot)
			return "Swing %s ±%.0f°%s%s" % [around2, shot.swing_amp_deg, dur, loop_tag]
		CameraShot.ShotType.DOLLY_ZOOM:
			return "Dolly Zoom %.1fm%s" % [shot.dolly_distance, dur]
	return "?" + dur


## Short human label for a shot's target — focal-point flag wins, then
## node name, then the legacy entity_id, else "(no target)".
func _target_label(shot: CameraShot) -> String:
	if shot.use_focal_point:
		return "focal point"
	if not shot.target_node.is_empty():
		return String(shot.target_node).get_file()
	if shot.entity_id != "":
		return shot.entity_id
	return "(no target)"


func _refresh_scrubber_range() -> void:
	scrubber.max_value = max(_total_duration, 0.001)
	scrubber.set_value_no_signal(0.0)
	_update_scrub_label(0.0)


func _update_scrub_label(t: float) -> void:
	scrub_time_label.text = "t = %.2fs / %.2fs" % [t, _total_duration]


func _shot_type_label(t: int) -> String:
	match t:
		CameraShot.ShotType.STATIC: return "Static"
		CameraShot.ShotType.PAN: return "Pan (world)"
		CameraShot.ShotType.PAN_LOCAL: return "Pan (local)"
		CameraShot.ShotType.LOOK_AT: return "Look At"
		CameraShot.ShotType.ORBIT: return "Orbit"
		CameraShot.ShotType.ORBIT_SWING: return "Orbit Swing"
		CameraShot.ShotType.DOLLY_ZOOM: return "Dolly Zoom"
	return "?"


func _set_status(s: String) -> void:
	if status_label:
		status_label.text = s


# ─── Signal handlers ─────────────────────────────────────────────────────

func _on_show_frustums(v: bool) -> void:
	show_frustums_toggled.emit(v)


func _on_far_clip_changed(v: float) -> void:
	far_clip_changed.emit(v)


func _on_camera_selected(idx: int) -> void:
	var cam = camera_list.get_item_metadata(idx)
	if cam is Camera3D:
		set_selected_camera(cam)
		# Also select it in the editor so the inspector shows its properties.
		if editor_plugin:
			editor_plugin.get_editor_interface().get_selection().clear()
			editor_plugin.get_editor_interface().get_selection().add_node(cam)


func _on_focus_pressed() -> void:
	if _selected_camera:
		focus_camera_requested.emit(_selected_camera)


func _on_snap_pressed() -> void:
	if _selected_camera:
		snap_from_view_requested.emit(_selected_camera)


func _on_shot_selected(idx: int) -> void:
	_selected_shot_index = idx
	_rebuild_params_panel()
	var shots := get_shots()
	if idx >= 0 and idx < shots.size():
		var shot: CameraShot = shots[idx]
		if shot and editor_plugin:
			# Surface the shot resource in the inspector too — duplicated
			# editing surface, but useful for fields we don't expose in
			# the dock.
			editor_plugin.get_editor_interface().inspect_object(shot)


func _on_add_shot(id: int) -> void:
	if _selected_camera == null or not _is_cutscene_cam(_selected_camera):
		_set_status("Select a CutsceneCamera first.")
		return
	var shot := CameraShot.new()
	shot.type = id
	shot.name = _shot_type_label(id)
	shot.pose = _selected_camera.transform
	shot.fov = _selected_camera.fov
	var shots = _selected_camera.get("shots")
	if shots == null:
		shots = []
	shots = shots.duplicate()
	shots.append(shot)
	_apply_shots(shots, "Add Shot")


func _on_remove_shot() -> void:
	if _selected_shot_index < 0:
		return
	var shots = get_shots().duplicate()
	if _selected_shot_index >= shots.size():
		return
	shots.remove_at(_selected_shot_index)
	_apply_shots(shots, "Remove Shot")
	_selected_shot_index = -1


func _on_duplicate_shot() -> void:
	if _selected_shot_index < 0:
		return
	var shots = get_shots().duplicate()
	if _selected_shot_index >= shots.size():
		return
	var orig: CameraShot = shots[_selected_shot_index]
	if orig == null:
		return
	var dup: CameraShot = orig.duplicate(true)
	shots.insert(_selected_shot_index + 1, dup)
	_apply_shots(shots, "Duplicate Shot")


func _apply_shots(new_shots: Array, action_name: String) -> void:
	if _selected_camera == null:
		return
	# Re-pack into a typed Array[Resource] — CutsceneCamera exports `shots`
	# as `Array[Resource]` so an untyped Array would fail the property setter.
	var typed: Array[Resource] = []
	for s in new_shots:
		typed.append(s)
	if editor_plugin == null:
		_selected_camera.set("shots", typed)
		_refresh_shot_list()
		return
	var undo = editor_plugin.get_undo_redo()
	var old_shots = _selected_camera.get("shots")
	undo.create_action(action_name)
	undo.add_do_property(_selected_camera, "shots", typed)
	undo.add_undo_property(_selected_camera, "shots", old_shots if old_shots != null else ([] as Array[Resource]))
	undo.add_do_method(self, "_refresh_shot_list")
	undo.add_undo_method(self, "_refresh_shot_list")
	undo.commit_action()


func _on_scrub(v: float) -> void:
	_update_scrub_label(v)
	scrub_changed.emit(v)


func _on_move_up() -> void:
	_reorder(-1)


func _on_move_down() -> void:
	_reorder(+1)


func _reorder(delta: int) -> void:
	var idx := _selected_shot_index
	if idx < 0:
		return
	var shots = get_shots().duplicate()
	var target := idx + delta
	if target < 0 or target >= shots.size():
		return
	var item = shots[idx]
	shots.remove_at(idx)
	shots.insert(target, item)
	_selected_shot_index = target
	_apply_shots(shots, "Move Shot")


# ─── Inline parameter panel ──────────────────────────────────────────────
#
# Rebuilt from scratch whenever the selected shot changes. Each shot type
# exposes only its relevant fields. Field edits flow through the editor
# UndoRedo so Ctrl+Z works in the dock just like it does in the inspector.

func _rebuild_params_panel() -> void:
	if params_box == null:
		return
	for c in params_box.get_children():
		c.queue_free()
	var shots := get_shots()
	if _selected_shot_index < 0 or _selected_shot_index >= shots.size():
		_add_params_hint("(Select a shot to edit its parameters.)")
		return
	var shot: CameraShot = shots[_selected_shot_index]
	if shot == null:
		_add_params_hint("<null shot>")
		return

	# Title row — type name + a "name" line edit (free-text label).
	var title_row := HBoxContainer.new()
	params_box.add_child(title_row)
	var type_label := Label.new()
	type_label.text = _shot_type_label(shot.type)
	type_label.add_theme_color_override("font_color", Color(0.7, 0.85, 1.0))
	title_row.add_child(type_label)
	var name_edit := LineEdit.new()
	name_edit.text = shot.name
	name_edit.placeholder_text = "Shot name"
	name_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_edit.text_submitted.connect(func(s): _set_field(shot, "name", s, "Rename Shot"))
	name_edit.focus_exited.connect(func(): _set_field(shot, "name", name_edit.text, "Rename Shot"))
	title_row.add_child(name_edit)

	# Duration — every shot has one.
	_add_float_row(shot, "duration", "Duration (s)", 0.0, 60.0, 0.05)

	# FOV — every shot except LOOK_AT keeps it (LOOK_AT doesn't change FOV
	# but we still expose it since it's harmless). Ignored for DOLLY_ZOOM (derived).
	_add_float_row(shot, "fov", "FOV (°)", 10.0, 120.0, 0.5)

	# Motion — easing curve, roll/Dutch tilt, and looping apply to every shot type.
	_add_ease_row(shot)
	_add_float_row(shot, "roll_deg", "Roll / Dutch (°)", -180.0, 180.0, 0.5)
	_add_loop_rows(shot)

	# Type-specific fields.
	match shot.type:
		CameraShot.ShotType.STATIC:
			_add_pose_section(shot, true)
		CameraShot.ShotType.PAN, CameraShot.ShotType.PAN_LOCAL:
			_add_pose_section(shot, true)
			_add_section_label("Pan target")
			_add_pan_space_row(shot)
			if shot.type == CameraShot.ShotType.PAN:
				_add_vec3_row(shot, "target_pos", "Target (world)")
				_add_button("Pick from viewport", func(): _pick_world_pos_into_shot(shot))
			else:
				_add_vec3_row(shot, "local_offset", "Offset (local)")
		CameraShot.ShotType.LOOK_AT:
			_add_pose_section(shot, false)
			_add_section_label("Look at")
			_add_bool_row(shot, "use_focal_point", "Use camera focal point")
			_add_target_node_row(shot)
			_add_string_row(shot, "entity_id", "Entity ID (XML fallback)")
			_add_offset_rows(shot)
		CameraShot.ShotType.ORBIT:
			_add_pose_section(shot, false)
			_add_section_label("Orbit around")
			_add_bool_row(shot, "use_focal_point", "Use camera focal point")
			_add_target_node_row(shot)
			_add_offset_rows(shot)
			_add_float_row(shot, "orbit_speed", "Speed (°/s, 0 = stop)", -360.0, 360.0, 1.0)
			if shot.target_node.is_empty() and not shot.use_focal_point:
				_add_warning("⚠ No target set — orbit won't move without one.")
		CameraShot.ShotType.ORBIT_SWING:
			_add_pose_section(shot, false)
			_add_section_label("Swing around")
			_add_bool_row(shot, "use_focal_point", "Use camera focal point")
			_add_target_node_row(shot)
			_add_offset_rows(shot)
			_add_float_row(shot, "swing_amp_deg", "Amplitude (°)", 0.0, 360.0, 1.0)
			if shot.target_node.is_empty() and not shot.use_focal_point:
				_add_warning("⚠ No target set — swing won't move without one.")
		CameraShot.ShotType.DOLLY_ZOOM:
			_add_pose_section(shot, true)
			_add_section_label("Dolly zoom (vertigo)")
			_add_float_row(shot, "dolly_distance", "Push distance (m)", -100.0, 100.0, 0.1)
			_add_float_row(shot, "dolly_subject_distance", "Subject distance (m)", 0.1, 500.0, 0.1)
			_add_static_label("FOV is derived to hold the subject's size — the FOV field above is ignored.")


func _add_params_hint(text: String) -> void:
	var l := Label.new()
	l.text = text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	params_box.add_child(l)


func _add_static_label(text: String) -> void:
	var l := Label.new()
	l.text = text
	l.add_theme_color_override("font_color", Color(0.65, 0.65, 0.65))
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	params_box.add_child(l)


func _add_warning(text: String) -> void:
	var l := Label.new()
	l.text = text
	l.add_theme_color_override("font_color", Color(1.0, 0.7, 0.3))
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	params_box.add_child(l)


func _add_section_label(text: String) -> void:
	var l := Label.new()
	l.text = text
	l.add_theme_color_override("font_color", Color(0.85, 0.85, 0.85))
	params_box.add_child(l)


func _add_float_row(shot: CameraShot, prop: String, label: String, lo: float, hi: float, step: float) -> void:
	var row := HBoxContainer.new()
	params_box.add_child(row)
	var l := Label.new()
	l.text = label
	l.custom_minimum_size = Vector2(140, 0)
	row.add_child(l)
	var sb := SpinBox.new()
	sb.min_value = lo
	sb.max_value = hi
	sb.step = step
	sb.allow_greater = true
	sb.allow_lesser = true
	sb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	sb.value = float(shot.get(prop))
	sb.value_changed.connect(func(v): _set_field(shot, prop, v, "Edit " + label))
	row.add_child(sb)


func _add_string_row(shot: CameraShot, prop: String, label: String) -> void:
	var row := HBoxContainer.new()
	params_box.add_child(row)
	var l := Label.new()
	l.text = label
	l.custom_minimum_size = Vector2(140, 0)
	row.add_child(l)
	var le := LineEdit.new()
	le.text = str(shot.get(prop))
	le.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	le.text_submitted.connect(func(s): _set_field(shot, prop, s, "Edit " + label))
	le.focus_exited.connect(func(): _set_field(shot, prop, le.text, "Edit " + label))
	row.add_child(le)


func _add_bool_row(shot: CameraShot, prop: String, label: String) -> void:
	var cb := CheckBox.new()
	cb.text = label
	cb.button_pressed = bool(shot.get(prop))
	cb.toggled.connect(func(v): _set_field(shot, prop, v, "Toggle " + label))
	params_box.add_child(cb)


func _add_vec3_row(shot: CameraShot, prop: String, label: String) -> void:
	_add_section_label(label)
	var row := HBoxContainer.new()
	params_box.add_child(row)
	var current: Vector3 = shot.get(prop)
	for axis_idx in 3:
		var sb := SpinBox.new()
		sb.min_value = -1_000_000.0
		sb.max_value = 1_000_000.0
		sb.step = 0.1
		sb.allow_greater = true
		sb.allow_lesser = true
		sb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		sb.value = current[axis_idx]
		var captured_axis := axis_idx
		sb.value_changed.connect(func(v): _set_vec3_axis(shot, prop, captured_axis, v, label))
		row.add_child(sb)


func _set_vec3_axis(shot: CameraShot, prop: String, axis_idx: int, v: float, label: String) -> void:
	var cur: Vector3 = shot.get(prop)
	match axis_idx:
		0: cur.x = v
		1: cur.y = v
		2: cur.z = v
	_set_field(shot, prop, cur, "Edit " + label)


func _add_ease_row(shot: CameraShot) -> void:
	var row := HBoxContainer.new()
	params_box.add_child(row)
	var l := Label.new()
	l.text = "Easing"
	l.custom_minimum_size = Vector2(140, 0)
	row.add_child(l)
	var opt := OptionButton.new()
	# Items added in enum order so item index == EaseType value.
	opt.add_item("Smoothstep", CameraShot.EaseType.SMOOTHSTEP)
	opt.add_item("Linear", CameraShot.EaseType.LINEAR)
	opt.add_item("Ease in", CameraShot.EaseType.EASE_IN)
	opt.add_item("Ease out", CameraShot.EaseType.EASE_OUT)
	opt.add_item("Ease in-out", CameraShot.EaseType.EASE_IN_OUT)
	opt.selected = shot.ease_type
	opt.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	opt.item_selected.connect(func(idx): _set_field(shot, "ease_type", idx, "Edit Easing"))
	row.add_child(opt)


## "Loop forever" toggle (+ style dropdown when on), for every shot type.
## ORBIT_SWING keeps its legacy `swing_loop` flag; all other types use `loop`.
func _add_loop_rows(shot: CameraShot) -> void:
	var flag := "swing_loop" if shot.type == CameraShot.ShotType.ORBIT_SWING else "loop"
	_add_bool_row(shot, flag, "Loop forever")
	if shot.get(flag):
		_add_loop_style_row(shot)


func _add_loop_style_row(shot: CameraShot) -> void:
	var row := HBoxContainer.new()
	params_box.add_child(row)
	var l := Label.new()
	l.text = "Loop style"
	l.custom_minimum_size = Vector2(140, 0)
	row.add_child(l)
	var opt := OptionButton.new()
	# Items added in enum order so item index == LoopStyle value.
	opt.add_item("Hold (ramp once)", CameraShot.LoopStyle.HOLD)
	opt.add_item("Breathe (sin pulse)", CameraShot.LoopStyle.BREATHE)
	opt.add_item("Ping-pong", CameraShot.LoopStyle.PING_PONG)
	opt.selected = shot.loop_style
	opt.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	opt.tooltip_text = "How FOV and roll animate while the swing loops forever."
	opt.item_selected.connect(func(idx): _set_field(shot, "loop_style", idx, "Edit Loop Style"))
	row.add_child(opt)


## Look-at offset (vector) + world/local toggle, shared by the targeting types.
func _add_offset_rows(shot: CameraShot) -> void:
	_add_vec3_row(shot, "look_at_offset", "Target offset")
	_add_bool_row(shot, "look_at_offset_local", "Offset in target-local space")


func _add_pan_space_row(shot: CameraShot) -> void:
	var row := HBoxContainer.new()
	params_box.add_child(row)
	var l := Label.new()
	l.text = "Space"
	l.custom_minimum_size = Vector2(140, 0)
	row.add_child(l)
	var opt := OptionButton.new()
	opt.add_item("World position", CameraShot.ShotType.PAN)
	opt.add_item("Local offset", CameraShot.ShotType.PAN_LOCAL)
	opt.selected = 0 if shot.type == CameraShot.ShotType.PAN else 1
	opt.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	opt.item_selected.connect(func(idx): _change_pan_space(shot, idx))
	row.add_child(opt)


func _change_pan_space(shot: CameraShot, idx: int) -> void:
	var new_type := CameraShot.ShotType.PAN if idx == 0 else CameraShot.ShotType.PAN_LOCAL
	_set_field(shot, "type", new_type, "Change Pan Space")
	_rebuild_params_panel()


func _add_target_node_row(shot: CameraShot) -> void:
	var row := HBoxContainer.new()
	params_box.add_child(row)
	var l := Label.new()
	l.text = "Target node"
	l.custom_minimum_size = Vector2(140, 0)
	row.add_child(l)
	var le := LineEdit.new()
	le.text = str(shot.target_node)
	le.placeholder_text = "(NodePath, e.g. Player)"
	le.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	le.text_submitted.connect(func(s): _set_field(shot, "target_node", NodePath(s), "Set Target"))
	le.focus_exited.connect(func(): _set_field(shot, "target_node", NodePath(le.text), "Set Target"))
	row.add_child(le)
	var pick := Button.new()
	pick.text = "Pick…"
	pick.tooltip_text = "Use the editor's currently-selected Node3D as the target."
	pick.pressed.connect(func(): _pick_target_from_selection(shot))
	row.add_child(pick)


func _add_button(label: String, on_pressed: Callable) -> void:
	var b := Button.new()
	b.text = label
	b.pressed.connect(on_pressed)
	params_box.add_child(b)


## Editable camera placement for a shot, straight in the dock (no Inspector
## round-trip). Position is always shown (it's the camera's location for
## every shot type); rotation only where the shot's orientation isn't
## runtime-driven by a look/orbit target (STATIC / PAN / PAN_LOCAL).
func _add_pose_section(shot: CameraShot, include_rotation: bool) -> void:
	_add_section_label("Pose")

	var prow := HBoxContainer.new()
	params_box.add_child(prow)
	var pl := Label.new()
	pl.text = "Position"
	pl.custom_minimum_size = Vector2(140, 0)
	prow.add_child(pl)
	for axis_idx in 3:
		var sb := SpinBox.new()
		sb.min_value = -1_000_000.0
		sb.max_value = 1_000_000.0
		sb.step = 0.05
		sb.allow_greater = true
		sb.allow_lesser = true
		sb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		sb.value = shot.pose.origin[axis_idx]
		var cax := axis_idx
		sb.value_changed.connect(func(v): _set_pose_origin_axis(shot, cax, v))
		prow.add_child(sb)

	if include_rotation:
		var rrow := HBoxContainer.new()
		params_box.add_child(rrow)
		var rl := Label.new()
		rl.text = "Rotation (°)"
		rl.custom_minimum_size = Vector2(140, 0)
		rrow.add_child(rl)
		var euler := shot.pose.basis.get_euler()
		for axis_idx in 3:
			var sb := SpinBox.new()
			sb.min_value = -360.0
			sb.max_value = 360.0
			sb.step = 0.5
			sb.allow_greater = true
			sb.allow_lesser = true
			sb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			sb.value = rad_to_deg(euler[axis_idx])
			var cax := axis_idx
			sb.value_changed.connect(func(v): _set_pose_euler_axis(shot, cax, v))
			rrow.add_child(sb)

	_add_button("Snap pose from viewport", func(): _snap_pose_into_shot(shot))


func _set_pose_origin_axis(shot: CameraShot, axis_idx: int, v: float) -> void:
	var xf: Transform3D = shot.pose
	var o := xf.origin
	o[axis_idx] = v
	xf.origin = o
	_set_field(shot, "pose", xf, "Edit Shot Position")


func _set_pose_euler_axis(shot: CameraShot, axis_idx: int, v: float) -> void:
	var xf: Transform3D = shot.pose
	var e := xf.basis.get_euler()
	e[axis_idx] = deg_to_rad(v)
	# Camera shots carry no scale — rebuild a clean orthonormal basis.
	xf.basis = Basis.from_euler(e)
	_set_field(shot, "pose", xf, "Edit Shot Rotation")


## Apply a single property change on a CameraShot via the editor's
## UndoRedo, then refresh the dock so labels reflect the new value.
func _set_field(shot: CameraShot, prop: String, new_value, action_name: String) -> void:
	var old_value = shot.get(prop)
	if str(old_value) == str(new_value):
		return
	if editor_plugin == null:
		shot.set(prop, new_value)
		_refresh_shot_list()
		_live_refresh_preview()
		return
	var undo = editor_plugin.get_undo_redo()
	undo.create_action(action_name)
	undo.add_do_property(shot, prop, new_value)
	undo.add_undo_property(shot, prop, old_value)
	undo.add_do_method(self, "_refresh_shot_list")
	undo.add_undo_method(self, "_refresh_shot_list")
	undo.commit_action()
	_live_refresh_preview()


## Re-drive the viewport camera at the current scrub time so in-dock edits
## are visible immediately (the reason to edit here instead of the Inspector).
## Reuses the existing scrub→plugin._apply_t path.
func _live_refresh_preview() -> void:
	if editor_plugin != null and scrubber != null:
		scrub_changed.emit(scrubber.value)


func _snap_pose_into_shot(shot: CameraShot) -> void:
	if _selected_camera == null:
		return
	# Re-use the same "snap from view" path the plugin already implements
	# for the camera itself, but apply the result to this specific shot.
	if editor_plugin == null:
		return
	var ed_cam = editor_plugin._get_editor_camera_3d()
	if ed_cam == null:
		_set_status("No editor camera available.")
		return
	# Convert the editor cam's world transform into the shot's local space
	# (relative to the cutscene camera's parent), matching how STATIC.pose
	# is interpreted by CameraShotEvaluator (local_to_world).
	var parent := _selected_camera.get_parent() as Node3D
	var local_xform: Transform3D = ed_cam.global_transform
	if parent:
		local_xform = parent.global_transform.affine_inverse() * ed_cam.global_transform
	_set_field(shot, "pose", local_xform, "Snap Pose")
	_set_field(shot, "fov", ed_cam.fov, "Snap FOV")
	_set_status("Snapped pose into shot.")


func _pick_world_pos_into_shot(shot: CameraShot) -> void:
	if editor_plugin == null:
		return
	var ed_cam = editor_plugin._get_editor_camera_3d()
	if ed_cam == null:
		_set_status("No editor camera available.")
		return
	_set_field(shot, "target_pos", ed_cam.global_position, "Pick Pan Target")
	_set_status("Set pan target to viewport position.")


func _pick_target_from_selection(shot: CameraShot) -> void:
	if editor_plugin == null:
		return
	var sel = editor_plugin.get_editor_interface().get_selection().get_selected_nodes()
	var picked: Node3D = null
	for n in sel:
		if n is Node3D and n != _selected_camera:
			picked = n
			break
	if picked == null:
		_set_status("Select a Node3D in the scene tree first.")
		return
	var scene_root := _get_edited_scene_root()
	if scene_root == null:
		return
	var path := scene_root.get_path_to(picked)
	_set_field(shot, "target_node", path, "Pick Target Node")
	_set_status("Target set to %s." % picked.name)
