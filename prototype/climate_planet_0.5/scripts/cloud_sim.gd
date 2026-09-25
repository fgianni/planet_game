class_name CloudSim
extends Node
## Persistent, advected cloud field on the GPU (two textures taking turns: each step reads the other's
## last result). Clouds keep their identity, travel with the winds for as long as they live, and
## stretch, split and merge through the eddies. Feeds the planet's cloud shader.

const W := 1024
const H := 512
const RATE := 20.0            # simulation steps per second

var _vps: Array[SubViewport] = []
var _mats: Array[ShaderMaterial] = []
var _cur := 0
var _acc := 0.0
var _time := 0.0
var _started := false
var _cloud_mat: ShaderMaterial
var speed_scale := 1.0


func setup(climate_tex: Texture2D, wind_tex: Texture2D, cloud_mat: ShaderMaterial) -> void:
	_cloud_mat = cloud_mat
	var shader := load("res://shaders/cloud_advect.gdshader")
	for k in 2:
		var vp := SubViewport.new()
		vp.size = Vector2i(W, H)
		vp.disable_3d = true
		vp.use_hdr_2d = true                     # more precision where supported
		vp.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS
		vp.render_target_update_mode = SubViewport.UPDATE_DISABLED
		var rect := ColorRect.new()
		rect.size = Vector2(W, H)
		var m := ShaderMaterial.new()
		m.shader = shader
		m.set_shader_parameter("climate_tex", climate_tex)
		m.set_shader_parameter("wind_tex", wind_tex)
		rect.material = m
		vp.add_child(rect)
		add_child(vp)
		_vps.append(vp)
		_mats.append(m)
	_mats[0].set_shader_parameter("prev", _vps[1].get_texture())
	_mats[1].set_shader_parameter("prev", _vps[0].get_texture())


func _process(delta: float) -> void:
	if _vps.is_empty():
		return
	_acc += delta * speed_scale
	_time += delta * speed_scale
	if _started and _acc < 1.0 / RATE:
		return
	var step_dt := minf(_acc, 0.2)
	_acc = 0.0
	var nxt := 1 - _cur
	var m := _mats[nxt]
	m.set_shader_parameter("dt", step_dt)
	m.set_shader_parameter("time", _time)
	m.set_shader_parameter("init", not _started)
	_vps[nxt].render_target_update_mode = SubViewport.UPDATE_ONCE
	_cur = nxt
	_started = true
	_cloud_mat.set_shader_parameter("cloud_field", _vps[_cur].get_texture())
	_cloud_mat.set_shader_parameter("use_cloud_field", true)


func texture() -> Texture2D:
	return _vps[_cur].get_texture() if not _vps.is_empty() else null
