@tool
extends TerrainGenerator
@export var player_node_path:NodePath = NodePath("../PlayerCharacter"):
	set(value):
		player_node_path = value
		set_player_node_path(value)
		
@export var terrain_shader:Shader = preload("res://terrain_assets/heightmap.gdshader"):
	set(value):
		terrain_shader = value
		set_terrain_shader(terrain_shader)
		
@export var terrain_amplitude:float = 16.0:
	set(value):
		terrain_amplitude = value
		set_terrain_amplitude(value)
		
@export var height_exponent:float = 4.0:
	set(value):
		height_exponent = value
		set_terrain_height_exp(value)
		
@export var terrain_offset:Vector3 = Vector3.ZERO:
	set(value):
		terrain_offset = value
		set_terrain_offset(value)

# Called when the node enters the scene tree for the first time.
func _enter_tree() -> void:
	set_render_distance(8)
	set_terrain_amplitude(terrain_amplitude)
	set_terrain_height_exp(height_exponent)
	set_step_size(1)
	set_length(7)
	
	set_player_node_path(player_node_path)
	set_terrain_shader(terrain_shader)
	set_terrain_offset(terrain_offset)
	
func _ready() -> void:
	pass

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(_delta: float) -> void:
	pass
