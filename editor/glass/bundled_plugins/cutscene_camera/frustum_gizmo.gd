@tool
extends EditorNode3DGizmoPlugin
class_name CutsceneFrustumGizmo
## Draws an FOV frustum for every Camera3D / BaseCamera in the scene so you
## can see exactly what each cutscene shot frames without having to switch
## cameras in-game. Lines only (no filled mesh) — readable against any
## background and cheap.
##
## Three states per camera:
##   • normal  — neutral white, dimmed.
##   • cutscene — pink/coral so CutsceneCameras stand out from gameplay cams.
##   • selected — bright yellow when the camera node is selected.
##
## The plugin owner toggles `enabled` to globally hide/show all frustums via
## the dock's "Show frustums" checkbox.
##
## GLASS NOTE: the original addon hard-referenced the project-specific
## CutsceneCamera class (`const CutsceneCameraScript := preload(...)`,
## `cam is CutsceneCameraScript`). That res:// script does NOT exist in an
## arbitrary engine-bundled project and would FAIL to resolve under disabled
## packs, so we DUCK-TYPE it instead: any Camera3D that exposes a `shots`
## property (and a `look_at_target`) is treated as a cutscene camera. Zero
## res:// project dependency — the bundle compiles in any project.

var enabled: bool = true
## Near/far in scene units — far is short by default so frustums stay readable
## on close-quarters interior cameras without bleeding through walls.
var near_clip: float = 0.1
var far_clip: float = 6.0


func _init() -> void:
	create_material("frustum_normal", Color(1.0, 1.0, 1.0, 0.35), false, true, true)
	create_material("frustum_cutscene", Color(1.0, 0.45, 0.55, 0.85), false, true, true)
	create_material("frustum_selected", Color(1.0, 0.9, 0.2, 1.0), false, true, true)
	create_material("frustum_target", Color(0.3, 0.9, 1.0, 0.9), false, true, true)


## Duck-typed "is this a cutscene camera?" — the bundle has no access to the
## project's CutsceneCamera class_name, so we detect it by the `shots` property
## that every CutsceneCamera exposes. `Object.get()` returns null for an absent
## property, so a plain Camera3D / FollowCamera reads as non-cutscene.
static func _is_cutscene_cam(cam: Camera3D) -> bool:
	return cam != null and cam.get("shots") != null


func _get_gizmo_name() -> String:
	return "Cutscene Camera Frustum"


func _has_gizmo(node: Node3D) -> bool:
	# Show on every Camera3D — covers BaseCamera, CutsceneCamera, FollowCamera,
	# and plain Camera3D nodes. Cheap to compute.
	return node is Camera3D


func _redraw(gizmo: EditorNode3DGizmo) -> void:
	gizmo.clear()
	if not enabled:
		return
	var cam := gizmo.get_node_3d() as Camera3D
	if cam == null:
		return

	var is_cutscene := _is_cutscene_cam(cam)
	var mat_name := "frustum_cutscene" if is_cutscene else "frustum_normal"

	var lines := _build_frustum_lines(cam.fov, cam.keep_aspect, near_clip, far_clip)
	gizmo.add_lines(lines, get_material(mat_name, gizmo))

	# If this is a CutsceneCamera with a look_at_target, draw a thin line from
	# the camera to the target so you can see what it's tracking.
	if is_cutscene:
		var look_target = cam.get("look_at_target")
		if look_target and look_target is Node3D and is_instance_valid(look_target):
			var to_target: PackedVector3Array = PackedVector3Array()
			# Lines are in local space — convert target world pos into the
			# camera's local frame.
			var local_target: Vector3 = cam.global_transform.affine_inverse() * (look_target as Node3D).global_position
			to_target.append(Vector3.ZERO)
			to_target.append(local_target)
			gizmo.add_lines(to_target, get_material("frustum_target", gizmo))


## Build the 8 line-segment-endpoints of a camera frustum in the camera's
## local space (camera looks down -Z). Aspect of 16:9 is assumed when the
## camera is in KEEP_HEIGHT mode (Godot's default). The geometry is fully
## code-generated here (no loaded resource), which is exactly why this gizmo
## bundles cleanly under disabled packs.
func _build_frustum_lines(fov_deg: float, keep_aspect: int, near: float, far: float) -> PackedVector3Array:
	var aspect := 16.0 / 9.0  # editor viewport approximation
	var v_fov := deg_to_rad(fov_deg)
	# Godot's `fov` is the vertical FOV when keep_aspect==KEEP_HEIGHT,
	# horizontal when KEEP_WIDTH. Convert accordingly so the frustum matches.
	var h_fov: float
	if keep_aspect == Camera3D.KEEP_WIDTH:
		h_fov = v_fov
		v_fov = 2.0 * atan(tan(h_fov * 0.5) / aspect)
	else:
		h_fov = 2.0 * atan(tan(v_fov * 0.5) * aspect)

	var near_h := tan(v_fov * 0.5) * near
	var near_w := tan(h_fov * 0.5) * near
	var far_h := tan(v_fov * 0.5) * far
	var far_w := tan(h_fov * 0.5) * far

	# Camera looks down -Z in Godot.
	var n_tl := Vector3(-near_w,  near_h, -near)
	var n_tr := Vector3( near_w,  near_h, -near)
	var n_bl := Vector3(-near_w, -near_h, -near)
	var n_br := Vector3( near_w, -near_h, -near)
	var f_tl := Vector3(-far_w,  far_h, -far)
	var f_tr := Vector3( far_w,  far_h, -far)
	var f_bl := Vector3(-far_w, -far_h, -far)
	var f_br := Vector3( far_w, -far_h, -far)

	var lines := PackedVector3Array()
	# Near rect.
	lines.append_array([n_tl, n_tr,  n_tr, n_br,  n_br, n_bl,  n_bl, n_tl])
	# Far rect.
	lines.append_array([f_tl, f_tr,  f_tr, f_br,  f_br, f_bl,  f_bl, f_tl])
	# Sides — origin → far corners so you read it as a pyramid first, then
	# near→far edges so the truncated portion is visible.
	lines.append_array([
		Vector3.ZERO, f_tl,  Vector3.ZERO, f_tr,
		Vector3.ZERO, f_bl,  Vector3.ZERO, f_br,
	])
	lines.append_array([n_tl, f_tl,  n_tr, f_tr,  n_bl, f_bl,  n_br, f_br])
	return lines


## Force a redraw of every gizmo this plugin manages. Called when the dock
## toggles visibility or far-plane distance.
func refresh_all(scene_root: Node) -> void:
	if scene_root == null:
		return
	for cam in _collect_cameras(scene_root):
		# Touch the node so its gizmo redraws — toggling visible is the
		# cheapest way that works in editor.
		var was_visible: bool = cam.visible
		cam.visible = not was_visible
		cam.visible = was_visible


func _collect_cameras(node: Node) -> Array:
	var out: Array = []
	if node is Camera3D:
		out.append(node)
	for child in node.get_children():
		out.append_array(_collect_cameras(child))
	return out
