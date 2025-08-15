#include "terrain_generator.h"

#include "scene/resources/surface_tool.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/mesh_data_tool.h"
#include "scene/3d/mesh_instance_3d.h"

/*
NOTE: 
-> chunk is not a real class/type -> it's a mesh and a heightmap referred together
*/
TerrainGenerator::TerrainGenerator() {
}
TerrainGenerator::~TerrainGenerator() {
}

/*
allows C++ classes to use the same ready/process signals as in gdscript
*/
void TerrainGenerator::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
			_enter_tree();
			break;
		case NOTIFICATION_EXIT_TREE:
			_exit_tree();
			break;
		case NOTIFICATION_READY:
			_ready();
			break;
		case NOTIFICATION_PHYSICS_PROCESS:
			_physics_process(get_physics_process_delta_time());
			break;
		case NOTIFICATION_PROCESS:
			_process(get_process_delta_time());
			break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			for (KeyValue<Vector2i, Ref<MeshData>> m : lod_meshes) {
				m.value->set_visiblity(is_visible_in_tree());
			}
		} break;
	}
}

void TerrainGenerator::_enter_tree() {
	// just in case...
	create_tasks.clear();
	lod_meshes.clear();
	chunk_table.clear();

	// set scenario for custom mesh instances
	WorldData::world_scenario = get_world_3d()->get_scenario();

	// default values
    WorldData::STEP_EXP = 0;
    WorldData::STEP_SIZE = 1 << WorldData::STEP_EXP;
    WorldData::LENGTH_EXP = 6;
    WorldData::LENGTH = 1 << WorldData::LENGTH_EXP;
    WorldData::H_RESOLUTION = (1 << (WorldData::LENGTH_EXP - WorldData::STEP_EXP)) + 1 + 2;
	// determines heights
    WorldData::AMPLITUDE = 1.0;
	WorldData::HEIGHT_EXP = 1.0;

	// need to create set/get functions
    WorldData::noise_type = FastNoiseLite::TYPE_SIMPLEX_SMOOTH;
    WorldData::fractal_type = FastNoiseLite::FRACTAL_FBM;
    WorldData::NOISE_FREQUENCY = 1.0 / (1000.0 * WorldData::STEP_SIZE);
    WorldData::FRACTAL_OCTAVES = 10.0;
    WorldData::FRACTAL_LACUNARITY = 2.0;
    WorldData::FRACTAL_GAIN = 0.45;

	/*
	* COLLISION SETUP -> godot managed, do not make direct memory modifications
	*/
	collision_map = memnew(CollisionShape3D);
	static_body_3d = memnew(StaticBody3D);
	static_body_3d->add_child(collision_map);
	add_child(static_body_3d);
	collision_shape = memnew(ConcavePolygonShape3D);
}

void TerrainGenerator::_exit_tree() {
	set_process(false);
	set_physics_process(false);

	if (!create_tasks.is_empty()) {
		for (auto &t : create_tasks) {
			WorkerThreadPool::get_singleton()->wait_for_task_completion(t.value);
		}
	}
	create_tasks.clear();
	for (auto &m : lod_meshes) {
		m.value->set_visiblity(false);
	}
	lod_meshes.clear();
	chunk_table.clear();

	_manual_collision_update = false;
}

/*
* READY
*/
void TerrainGenerator::_ready() {
	if (_player_node_path.is_empty()) return;

	RS::get_singleton()->global_shader_parameter_set("amplitude", WorldData::AMPLITUDE);
	RS::get_singleton()->global_shader_parameter_set("vert_step_size", WorldData::STEP_SIZE);
	RS::get_singleton()->global_shader_parameter_set("clipmap_partition_length", WorldData::LENGTH);
	RS::get_singleton()->global_shader_parameter_set("height_exp", WorldData::HEIGHT_EXP);
	/*
	GENERATE ALL LOD MESHES
	*/
	WorldData::LOD_LIMIT = WorldData::LENGTH_EXP - WorldData::STEP_EXP - 1.0;

	for (int z = -render_distance; z <= render_distance; z++) {
		for (int x = -render_distance; x <= render_distance; x++) {
			if (player_chunk.distance_to(Vector3(x, 0, z)) >= render_distance) {
				continue;
			}
			Vector2i coord = {x, z};
			lod_meshes[coord].instantiate(LODS(x,z,WorldData::LOD_LIMIT), Vector3(x,0,z));
		}
	}
	// + LENGTH -> chunks are added before deletion in process()
	chunk_table.reserve(lod_meshes.size() + WorldData::LENGTH);
	reuse_pool.resize(render_distance);
	/*
	* SETUP COLLISION MAP
	*/
	collision_size.x = 32 * WorldData::STEP_SIZE;
	collision_size.y = 32 * WorldData::STEP_SIZE;
	int sub_div = 31;
	// initial mesh -> will not need later
	PlaneMesh collision_mesh;
	collision_mesh.set_size(collision_size);
	collision_mesh.set_subdivide_width(sub_div);
	collision_mesh.set_subdivide_depth(sub_div);
	shape_faces = Variant(collision_mesh.get_faces());
	collision_shape->set_faces(shape_faces);
	collision_map->set_shape(collision_shape);

	/*
	* FINALIZE
	*/
	set_process(true);
	set_physics_process(true);
	ready_queued.store(false);
	_manual_collision_update = false;
	
	// initial collision map
	update_shape(-1, -1);
}


/*
* PHYSICS PROCESS
*
* 33 * 33 * 6 = 6534  VERTICES IN FACES
* perhaps reduce subdivisions to 15x15 or even 7x7
*/
void TerrainGenerator::_physics_process(double physics_delta) {
	if (_player_node_path.is_empty()) return;

	// snap to nearest Chunk quadrant
	real_t snap = collision_size.x / 2.0;
	Vector3 player_rounded_position = (get_player()->get_global_position()).snappedf(snap);
	player_rounded_position.y = 0.0;

	if (collision_map->get_global_position() != player_rounded_position || _manual_collision_update) {
		collision_map->set_global_position(player_rounded_position);

		Vector3 center = calculate_player_chunk() * WorldData::LENGTH;
		int x = (player_rounded_position.x <= center.x) ? -1 : 1;
		int z = (player_rounded_position.z <= center.z) ? -1 : 1;
		update_shape(x, z);
	}
}

/*
* CALLED FROM : _ready() _physics_process()
*/
void TerrainGenerator::update_shape(int x, int z) {
	// need to re-calculate player_chunk -> _physics_process is faster (120 tics) than _process (60 tics)
	Vector3 p_chunk = calculate_player_chunk();
	/*
	* Prepare nearest chunk HeightMapData based on quadrant offset
	*/
	Vector3 positions[4] = {
		p_chunk,
		p_chunk + Vector3(x, 0, 0),
		p_chunk + Vector3(0, 0, z),
		p_chunk + Vector3(x, 0, z)
	};
	// make sure all nearby chunks are valid -> keep trying next tic until success
	Vector<Ref<HeightMapData>> nearest;
	for (auto v : positions) {
		auto itr = chunk_table.find(v);
		_manual_collision_update = itr == chunk_table.end();
		if (_manual_collision_update) { 
			return;
		}
		nearest.push_back(itr->value);
	}
	DEBUG_PRINT_OFTEN("UPDATE COLLISION SHAPE");

	// Adjust shape_faces heights based on nearest chunks
	for (auto &face : shape_faces) {
		Vector3 global_vert = face + collision_map->get_global_position();
		face.y = get_height(nearest, global_vert, true);
	}
	if (!heights_not_found.is_empty()) {
		DEBUG_PRINT_ERROR("NUMBER OF INVALID HEIGHTS:", heights_not_found.size(), heights_not_found);
		heights_not_found.clear();
	}
	// update shape faces
	collision_shape->set_faces(shape_faces);
}


/*
* PROCESS
*/
void TerrainGenerator::_process(double delta) {
	if (_player_node_path.is_empty()) return;
	/*
	TODO: account for diagonal chunk movement
	*/
	Vector3 new_player_chunk = calculate_player_chunk();

	// check if we do not need to update
	if (player_chunk == new_player_chunk && !update_check()) { 
		return;
	}
	RS::get_singleton()->global_shader_parameter_set("clipmap_position", new_player_chunk * WorldData::LENGTH);

	// Try to generate chunks ahead of time based on where the player is moving.
	//player_chunk.y += round(CLAMP(player_character->get_velocity().y, -render_distance/4, render_distance/4));

	/*
	* priority based chunk processing -> spawn/update chunks according to player view direction
	* for now, only considers 4 directions (diagonals) -> considering 8 directions in the future
	*/
	double yaw = get_player()->get_global_rotation_degrees().y;
	bool x_flip = yaw < 0.0 && yaw >= -180.0;
	bool z_flip = yaw > 90.0 || yaw <= -90.0;
	range_flip x_range(-render_distance, render_distance, x_flip);
	range_flip z_range(-render_distance, render_distance, z_flip);

	// check chunks from outside in -> queues create tasks first
	for (int x : x_range) {
		for (int z : z_range) {
			Vector2i grid_pos = Vector2i(x, z);
			Vector3 chunk_pos = new_player_chunk + Vector3(x, 0, z);

			bool distance_check = new_player_chunk.distance_to(chunk_pos) >= render_distance;
			if (distance_check || create_tasks.has(chunk_pos)) continue;

			auto chunk_itr = chunk_table.find(chunk_pos);
			auto mesh_itr = lod_meshes.find(grid_pos);
			auto mesh_val = mesh_itr->value;

			if (chunk_itr == chunk_table.end()) {
				// CREATE NEW/REUSE CHUNK
				mesh_val->set_visiblity(false);
				create_tasks[chunk_pos] = WorkerThreadPool::INVALID_TASK_ID;
				create_queue.push(callable_mp(this, &TerrainGenerator::push_create_task).bind(chunk_pos, grid_pos));
			}
			else if (chunk_pos != mesh_val->get_chunk_pos()) {
				// UPDATE CHUNKS
				DEBUG_PRINT_OFTEN("UPDATE MESH DATA", chunk_pos);
				mesh_val->update(chunk_itr->value->get_image(), chunk_pos);
			}
			/*
			* START TASK CALL-CHAIN
			*
			* tasks calls next task at completion -> chained calls
			* one task needed to start
			* ((create_tasks/queue).size() == 1) -> no tasks are running, start task
			*/
			if (create_tasks.size() != 1 || create_queue.size() != 1) { 
				continue;
			}
			create_queue.front().call_deferred();
			create_queue.pop();
		}
	}

	player_chunk = new_player_chunk;
	delete_far_away_chunks();
}

/*
* CALLED FROM : _process()
*/
void TerrainGenerator::create_chunk(Ref<HeightMapData> hmap_data, Vector3 chunk_pos, Vector2i grid_pos) {
	hmap_data->_instantiate(
		chunk_pos, callable_mp(this, &TerrainGenerator::add_chunk).bind(hmap_data, chunk_pos, grid_pos)
	);
}
void TerrainGenerator::add_chunk(Ref<HeightMapData> hmap_data, Vector3 chunk_pos, Vector2i grid_pos) {
	WorkerThreadPool::get_singleton()->wait_for_task_completion(create_tasks[chunk_pos]);

	chunk_table[chunk_pos] = hmap_data;
	lod_meshes[grid_pos]->update(hmap_data->get_image(), chunk_pos);
	create_tasks.erase(chunk_pos);
	
	// run next create task if it exists -> continue call chain
	if (create_queue.empty()) {
		return;
	}	
	create_queue.front().call_deferred();
	create_queue.pop();
}
void TerrainGenerator::delete_far_away_chunks() {
	// Also take the opportunity to delete far away chunks.
	for (auto c : chunk_table) {
		if (player_chunk.distance_to(c.key) < render_distance) {
			continue;
		}
		reuse_pool.write(c.value);
		chunk_table.erase(c.key);
	}
}

/*
* standard editor only exports 
* player character used for position only (for now)
*
* if player is removed from loaded scene, de-spawn terrain
* if player is added to empty scene, spawn terrain
*/
void TerrainGenerator::set_player_node_path(const NodePath &p_path) {
	if (_player_node_path == p_path) return;
	_player_node_path = p_path;
	setter_process(_player_node_path.is_empty(), "PLAYER");
}
void TerrainGenerator::set_terrain_shader(Ref<Shader> p_shader) {
	if (WorldData::terrain_shader == p_shader) return;
	WorldData::terrain_shader = p_shader;
	setter_process(WorldData::terrain_shader.is_null(), "SHADER");
}
void TerrainGenerator::set_terrain_offset(const Vector3 &p_pos) {
	if (WorldData::WORLD_OFFSET == p_pos) return;
	WorldData::WORLD_OFFSET = p_pos;
	for (auto m : lod_meshes) {
		m.value->update_position();
	}
}
void TerrainGenerator::set_terrain_amplitude(const real_t &new_amp) {
	if (WorldData::AMPLITUDE == new_amp) return;
	WorldData::AMPLITUDE = new_amp;
	RS::get_singleton()->global_shader_parameter_set("amplitude", new_amp);
}
void TerrainGenerator::set_terrain_height_exp(const real_t &new_height_exp) {
	if (WorldData::HEIGHT_EXP == new_height_exp) return;
	WorldData::HEIGHT_EXP = new_height_exp;
	RS::get_singleton()->global_shader_parameter_set("height_exp", new_height_exp);
}

// properties are exposed in gdscript
void TerrainGenerator::_bind_methods() {
	// PARAMETERS (STATIC)
	ClassDB::bind_method(D_METHOD("set_render_distance", "new_render_distance"), &TerrainGenerator::set_render_distance);
	ClassDB::bind_method(D_METHOD("set_step_size", "new_step_exp"), &TerrainGenerator::set_step_size);
	ClassDB::bind_method(D_METHOD("set_length", "new_length_exp"), &TerrainGenerator::set_length);
	ClassDB::bind_method(D_METHOD("set_seed", "new_seed"), &TerrainGenerator::set_seed);

	// PARAMETERS (DYNAMIC)
	ClassDB::bind_method(D_METHOD("set_player_node_path", "p_path"), &TerrainGenerator::set_player_node_path);
	ClassDB::bind_method(D_METHOD("set_terrain_shader", "p_shader"), &TerrainGenerator::set_terrain_shader);
	ClassDB::bind_method(D_METHOD("set_terrain_offset", "p_pos"), &TerrainGenerator::set_terrain_offset);
	ClassDB::bind_method(D_METHOD("set_terrain_amplitude", "new_amp"), &TerrainGenerator::set_terrain_amplitude);
	ClassDB::bind_method(D_METHOD("set_terrain_height_exp", "new_height_exp"), &TerrainGenerator::set_terrain_height_exp);

	ClassDB::bind_method(D_METHOD("get_player_node_path"), &TerrainGenerator::get_player_node_path);
	ClassDB::bind_method(D_METHOD("get_terrain_shader"), &TerrainGenerator::get_terrain_shader);
	ClassDB::bind_method(D_METHOD("get_terrain_offset"), &TerrainGenerator::get_terrain_offset);
	ClassDB::bind_method(D_METHOD("get_terrain_amplitude"), &TerrainGenerator::get_terrain_amplitude);
	ClassDB::bind_method(D_METHOD("get_terrain_height_exp"), &TerrainGenerator::get_terrain_height_exp);
}