extends Node3D
## Godgame — native Godot rebuild, slice G1: the island.
##
## Everything is built from code so the scene file stays trivial (safer to
## author without a running editor). Next slices add the village (G2),
## villagers (G3), and the divine hand (G4). See docs/plan-godot.md.
##
## Controls: right-drag orbits, mouse wheel zooms, WASD pans the focus.

const TERRAIN_SIZE := 512.0     # world units across (matches C++ Terrain::SIZE)
const TERRAIN_GRID := 128       # cells per side
const WATER_LEVEL := 0.0
const WORLD_SEED := 20260707

var _cam: Camera3D
var _cam_focus := Vector3.ZERO
var _cam_yaw := 0.6
var _cam_pitch := 0.85
var _cam_dist := 190.0
var _orbiting := false


func _ready() -> void:
	_build_environment()
	_build_terrain()
	_build_water()
	_build_camera()


func _build_environment() -> void:
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-52.0, 40.0, 0.0)
	light.light_energy = 1.15
	light.shadow_enabled = true
	add_child(light)

	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sky_mat := ProceduralSkyMaterial.new()
	sky_mat.sky_top_color = Color(0.35, 0.55, 0.85)
	sky_mat.sky_horizon_color = Color(0.75, 0.82, 0.88)
	sky_mat.ground_horizon_color = Color(0.75, 0.82, 0.88)
	sky_mat.ground_bottom_color = Color(0.55, 0.62, 0.68)
	sky.sky_material = sky_mat
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = 0.6
	env.fog_enabled = true
	env.fog_density = 0.0009
	env.fog_light_color = Color(0.74, 0.82, 0.88)

	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)


func _height(x: float, z: float, noise: FastNoiseLite) -> float:
	var n := (noise.get_noise_2d(x, z) + 1.0) * 0.5   # 0..1
	# Radial island falloff so the edges sink under the water plane.
	var cx := x / (TERRAIN_SIZE * 0.5)
	var cz := z / (TERRAIN_SIZE * 0.5)
	var d := sqrt(cx * cx + cz * cz)
	var fall := clampf(1.0 - d, 0.0, 1.0)
	fall = fall * fall
	return n * 62.0 * fall - 7.0


func _terrain_color(h: float, slope: float) -> Color:
	if h < 1.5:
		return Color(0.80, 0.74, 0.52)          # sand
	if slope > 0.55:
		return Color(0.45, 0.42, 0.40)          # exposed rock
	if h > 42.0:
		return Color(0.92, 0.92, 0.95)          # snow
	var lush := Color(0.32, 0.55, 0.26)
	var dry := Color(0.24, 0.44, 0.20)
	return lush.lerp(dry, clampf(h / 40.0, 0.0, 1.0))


func _build_terrain() -> void:
	var noise := FastNoiseLite.new()
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.seed = WORLD_SEED
	noise.frequency = 0.006
	noise.fractal_octaves = 5

	var step := TERRAIN_SIZE / float(TERRAIN_GRID)
	var half := TERRAIN_SIZE * 0.5

	# Precompute the height grid so each vertex is sampled once.
	var h := []
	for j in range(TERRAIN_GRID + 1):
		var row := []
		for i in range(TERRAIN_GRID + 1):
			var x := -half + float(i) * step
			var z := -half + float(j) * step
			row.append(_height(x, z, noise))
		h.append(row)

	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for j in range(TERRAIN_GRID):
		for i in range(TERRAIN_GRID):
			var x0 := -half + float(i) * step
			var z0 := -half + float(j) * step
			var x1 := x0 + step
			var z1 := z0 + step
			var p00 := Vector3(x0, h[j][i], z0)
			var p10 := Vector3(x1, h[j][i + 1], z0)
			var p01 := Vector3(x0, h[j + 1][i], z1)
			var p11 := Vector3(x1, h[j + 1][i + 1], z1)
			_emit_tri(st, p00, p01, p11)
			_emit_tri(st, p00, p11, p10)
	st.generate_normals()

	var mi := MeshInstance3D.new()
	mi.mesh = st.commit()
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.roughness = 1.0
	# Cull disabled for G1 so the island is visible regardless of winding;
	# once verified in the editor we pin the winding and re-enable back-face
	# culling.
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mi.material_override = mat
	add_child(mi)


func _emit_tri(st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3) -> void:
	var normal := (b - a).cross(c - a).normalized()
	var slope := 1.0 - absf(normal.y)
	for v in [a, b, c]:
		st.set_color(_terrain_color(v.y, slope))
		st.add_vertex(v)


func _build_water() -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(TERRAIN_SIZE * 1.5, TERRAIN_SIZE * 1.5)
	var mi := MeshInstance3D.new()
	mi.mesh = plane
	mi.position = Vector3(0.0, WATER_LEVEL, 0.0)
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.15, 0.35, 0.55, 0.72)
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.roughness = 0.08
	mat.metallic = 0.35
	mi.material_override = mat
	add_child(mi)


func _build_camera() -> void:
	_cam = Camera3D.new()
	_cam.far = 3000.0
	add_child(_cam)
	_update_camera()


func _update_camera() -> void:
	var dir := Vector3(
		cos(_cam_pitch) * sin(_cam_yaw),
		sin(_cam_pitch),
		cos(_cam_pitch) * cos(_cam_yaw))
	_cam.position = _cam_focus + dir * _cam_dist
	_cam.look_at(_cam_focus, Vector3.UP)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT:
			_orbiting = event.pressed
		elif event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_cam_dist = maxf(30.0, _cam_dist - 12.0)
			_update_camera()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_cam_dist = minf(700.0, _cam_dist + 12.0)
			_update_camera()
	elif event is InputEventMouseMotion and _orbiting:
		_cam_yaw -= event.relative.x * 0.005
		_cam_pitch = clampf(_cam_pitch - event.relative.y * 0.005, 0.15, 1.45)
		_update_camera()


func _process(delta: float) -> void:
	var forward := Vector3(sin(_cam_yaw), 0.0, cos(_cam_yaw))
	var right := Vector3(forward.z, 0.0, -forward.x)
	var move := Vector3.ZERO
	if Input.is_key_pressed(KEY_W):
		move -= forward
	if Input.is_key_pressed(KEY_S):
		move += forward
	if Input.is_key_pressed(KEY_A):
		move -= right
	if Input.is_key_pressed(KEY_D):
		move += right
	if move != Vector3.ZERO:
		_cam_focus += move.normalized() * delta * 90.0
		_update_camera()
