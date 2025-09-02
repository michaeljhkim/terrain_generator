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
	NodePath player_node_path;

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
		/*
		Callable create_chunk = create_custom_callable_lambda(
			this, [&](Ref<HeightMapData> p_hmap_data, Vector3 p_chunk_pos, Vector2i p_grid_pos) { 
				p_hmap_data->_instantiate(p_chunk_pos, callable_mp(this, &TerrainGenerator::add_chunk).bind(p_hmap_data, p_chunk_pos, p_grid_pos)); 
			}
		);
		*/
		Callable create_chunk = create_custom_callable_lambda(
			this, [=]() { 
				hmap_data->_instantiate(chunk_pos, callable_mp(this, &TerrainGenerator::add_chunk).bind(hmap_data, chunk_pos, grid_pos)); 
			}
		);
		create_tasks[chunk_pos] = WorkerThreadPool::get_singleton()->add_task(create_chunk);
	}

	/*
	* PLAYER CHARACTER -> get with NodePath for safety
	*/
    // Helper: get the actual CharacterBody3D node
    CharacterBody3D *get_player() const {
        return !player_node_path.is_empty() ? cast_to<CharacterBody3D>(get_node(player_node_path)) : nullptr;
    }
	// calculate player chunk -> convert global position to grid position
	Vector3 player_chunk;
	Vector3 calculate_player_chunk() const {
		Vector3 p_chunk = (get_player()->get_global_position() / WorldData::length).round();
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

	CREATE_PROPERTY_REF(int, render_distance)
	CREATE_PROPERTY_REF(int, seed)

	CREATE_PROPERTY_REF_CUSTOM_STATIC(real_t, step_size, WorldData) { 
		WorldData::step_exp = new_step_size;
		WorldData::step_size = 1 << WorldData::step_exp;
	}
	CREATE_PROPERTY_REF_CUSTOM_STATIC(real_t, length, WorldData) {
		WorldData::length_exp = new_length;
		WorldData::length = 1 << WorldData::length_exp;
	}
	CREATE_PROPERTY_REF(NodePath, player_node_path)
	CREATE_PROPERTY_REF_STATIC(Ref<Shader>, terrain_shader, WorldData)
	CREATE_PROPERTY_REF_STATIC(Vector3, terrain_offset, WorldData)
	CREATE_PROPERTY_REF_STATIC(real_t, terrain_amplitude, WorldData)
	CREATE_PROPERTY_REF_STATIC(real_t, terrain_height_exp, WorldData)

	// re-init terrain for low-level paramater changes
	void setter_process(bool is_null, String p_name) {
		String msg = "ADD";
		if (is_null) {
			msg = "REMOVE";
			_exit_tree();
		}
		else if (!ready_queued.load()) { 
			ready_queued.store(true);
			_ready();
		}
		DEBUG_PRINT_RARE(msg, p_name);
	}
};