#pragma once

#include "scene/3d/physics/character_body_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "height_map_data.h"

#include <chrono>

class TerrainGenerator : public Node3D {
	GDCLASS(TerrainGenerator, Node3D);
	
protected:
	// export these values -> to be defined in editor
	int seed = 0;
	int render_distance = 6;
	std::atomic_bool ready_queued = {true};
	NodePath _player_node_path;

	/*
	* COLLISION MAPPING MEMBERS
	*/
	bool _manual_collision_update = false;
	Size2 collision_size = {32, 32};
	Vector<Vector3> shape_faces;
	StaticBody3D* static_body_3d = nullptr;
	CollisionShape3D* collision_map = nullptr;
	Ref<ConcavePolygonShape3D> collision_shape;
	void update_shape(int x, int z);

	std::queue<Callable> create_queue;
	bool update_check() {
		Vector3 new_pos = calculate_player_chunk();
		bool should_update = false;
		for (const auto &m : lod_meshes) {
			Vector3 chunk_pos = new_pos + Vector3(m.key.x, 0, m.key.y);
			if (( should_update = !chunk_table.has(chunk_pos) )) break;
		}
		return should_update;
	}
	void push_create_task(Vector3 chunk_pos, Vector2i grid_pos) {
		//auto hmap_data = reuse_pool.data_left() ? reuse_pool.read() : memnew(HeightMapData);
		// take from reuse_pool if reuse_pool is not empty
		Ref<HeightMapData> hmap_data;
		if (reuse_pool.data_left()) {
			DEBUG_PRINT_OFTEN("REUSE HEIGHTMAP DATA", chunk_pos);
			hmap_data = reuse_pool.read();
		} 
		else {
			DEBUG_PRINT_OFTEN("CREATE HEIGHTMAP DATA", chunk_pos);
			hmap_data = memnew(HeightMapData);
		}
		create_tasks[chunk_pos] = WorkerThreadPool::get_singleton()->add_task(
			callable_mp(this, &TerrainGenerator::create_chunk).bind(hmap_data, chunk_pos, grid_pos)
		);
	}

	/*
	* PLAYER CHARACTER -> get with NodePath for safety
	*/
    // Helper: get the actual CharacterBody3D node
    CharacterBody3D *get_player() const {
        return !_player_node_path.is_empty() ? cast_to<CharacterBody3D>(get_node(_player_node_path)) : nullptr;
    }
	// calculate player chunk -> convert global position to grid position
	Vector3 player_chunk;
	Vector3 calculate_player_chunk() const {
		Vector3 p_chunk = (get_player()->get_global_position() / WorldData::LENGTH).round();
		p_chunk.y = 0.0;
		return p_chunk;
	}

	/*
	* helper to get heights from nearest heightmap	
	* if height cannot be read, generate height using FastNoiseLite -> rare occurance
	*/
	Vector<Vector3> heights_not_found;
	float get_height(const Vector<Ref<HeightMapData>> &data, Vector3 vert, bool generate_invalid = false) {
		for (auto h : data) {
			if (h->in_bounds(vert))
				return h->get_height_global(vert);
		}
		// backup in case no valid height found
		heights_not_found.push_back(vert);
		return generate_invalid ? data[0]->generate_height(vert.x,vert.z) : 0.0;
	}

	/*
	CHUNK MANAGING MEMBERS
	*/
	// precomputed lod meshes -> 2**LODS.center
	HashMap<Vector2i, Ref<MeshData>> lod_meshes;
	// chunk master list -> only holds chunks that are not being processed
	HashMap<Vector3, Ref<HeightMapData>> chunk_table;
	HashMap<Vector3, uint64_t> create_tasks;

protected:
	// Only for worker threads
	void create_chunk(Ref<HeightMapData> hmap_data, Vector3 chunk_pos, Vector2i grid_pos);
	// Only for main thread
	void add_chunk(Ref<HeightMapData> hmap_data, Vector3 chunk_pos, Vector2i grid_pos);
	void delete_far_away_chunks();

	static void _bind_methods();

public:
	RingBuffer<Ref<HeightMapData>> reuse_pool;

	TerrainGenerator();
	~TerrainGenerator();

	void _notification(int p_notification);
	void _ready();
	void _enter_tree();
	void _exit_tree();

	void _process(double delta);
	void _physics_process(double physics_delta);
	
	/*
	* GDSCRIPT PARAMETERS 
	*/

	// PARAMETERS (STATIC) -> should convert for dynamic reloading
	void set_step_size(const int8_t &new_step_exp) {
		WorldData::STEP_EXP = new_step_exp;
		WorldData::STEP_SIZE = 1 << WorldData::STEP_EXP;
	}
	void set_length(const int8_t &new_length_exp) {
		WorldData::LENGTH_EXP = new_length_exp;
		WorldData::LENGTH = 1 << WorldData::LENGTH_EXP;
	}
	void set_render_distance(const int &new_render_distance) { render_distance = new_render_distance; }
	void set_seed(const int &new_seed) { seed = new_seed; }

	// PARAMETERS (DYNAMIC)
	void set_player_node_path(const NodePath &p_path);
	void set_terrain_shader(Ref<Shader> p_shader);
	void set_terrain_offset(const Vector3 &p_pos);
	void set_terrain_amplitude(const real_t &new_amp);
	void set_terrain_height_exp(const real_t &new_height_exp);

	NodePath get_player_node_path() const { return _player_node_path; }
	Ref<Shader> get_terrain_shader() const { return WorldData::terrain_shader; }
	Vector3 get_terrain_offset() const { return WorldData::WORLD_OFFSET; };
	real_t get_terrain_amplitude() const { return WorldData::AMPLITUDE; }
	real_t get_terrain_height_exp() const { return WorldData::HEIGHT_EXP; }

	// re-init terrain for low-level paramater changes
	void setter_process(bool is_null, String p_name) {
		if (is_null) {
			DEBUG_PRINT_RARE("REMOVE " + p_name);
			_exit_tree();
			return;
		}
		DEBUG_PRINT_RARE("ADD " + p_name);
		if (ready_queued.load()) return;
		ready_queued.store(true);
		_ready();
	}
};