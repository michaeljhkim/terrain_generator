#include "height_map_data.h"
#include "thirdparty/embree/kernels/bvh/bvh_statistics.h"

// DEFINE STATIC VALUES
RID WorldData::world_scenario;
Ref<Shader> WorldData::terrain_shader;

int WorldData::SEED;
real_t WorldData::HEIGHT_EXP;
uint8_t WorldData::STEP_EXP;
real_t WorldData::STEP_SIZE;
uint8_t WorldData::LENGTH_EXP;
real_t WorldData::LENGTH;
real_t WorldData::AMPLITUDE;
real_t WorldData::LOD_LIMIT;

Vector3 WorldData::WORLD_OFFSET;
int WorldData::H_RESOLUTION;

FastNoiseLite::NoiseType WorldData::noise_type;
FastNoiseLite::FractalType WorldData::fractal_type;
real_t WorldData::NOISE_FREQUENCY;
real_t WorldData::FRACTAL_OCTAVES;
real_t WorldData::FRACTAL_LACUNARITY;
real_t WorldData::FRACTAL_GAIN;


HeightMapData::HeightMapData() {
}
HeightMapData::HeightMapData(Vector3 new_pos, Callable p_callable) {
	_instantiate(new_pos, p_callable);
}
HeightMapData::~HeightMapData() {
}

void HeightMapData::setup_height_map(Size2 size, Vector3 position) {
	// maximum vertex count (subdivide_w * subdivide_d)
	subdivide_w = (WorldData::LENGTH / WorldData::STEP_SIZE) + 1.0;
	subdivide_d = (WorldData::LENGTH / WorldData::STEP_SIZE) + 1.0;

	// shifted by half chunk size
	world_position = (Vector3(size.x, 0, size.y) * -0.5) + (position * WorldData::LENGTH);
	// one step for padding (smooth normals)
	start_pos = world_position + Vector3(WorldData::STEP_SIZE, 0, WorldData::STEP_SIZE);
	end_pos = start_pos + Vector3(WorldData::LENGTH, 0, WorldData::LENGTH);

	/*
	* FORMAT_RH (16-bit float) -> a good balance between size and accuracy
	* FORMAT_R8 (8-bit float) -> way smaller size, and accuracy drop MIGHT be worth it
	*/
	if (height_map.is_null()) {
		height_map.instantiate(WorldData::H_RESOLUTION, WorldData::H_RESOLUTION, false, Image::Format::FORMAT_RF);
	}

	int j_start = 0;
	int j_last = 0;
	const int split = 128;
	const int subdiv = subdivide_d + 2;
	
	// NUMBER OF TASKS REQUIRED TO GENERATE HEIGHT MAP
	active_task_count.store(Math::division_round_up(subdiv,split), std::memory_order_acquire);

	for (int j_size = subdiv; j_size > 0; j_size -= split) {
		// if resolution is less than the split size, there's no point in pushing task into another thread
		if ((j_size - split) <= 0) {
			j_last += j_size;
			generate_height_map(j_start, j_last);
			return;
		}
		j_last += split;
		sub_task_ids.insert(
			WorkerThreadPool::get_singleton()->add_task_bind(
				callable_mp(this, &HeightMapData::generate_height_map).bind(j_start,j_last), true
			)
		);
		j_start = j_last;
	}
}

/*
* run FastNoiseLite at coordiantes, and set in heightmap
*/
void HeightMapData::generate_height_map(int j_begin, int j_end) {
	for (int j = j_begin; j < j_end; j++) {
		const real_t z = local_to_global_z(j);

		for (int i = 0; i <= (subdivide_w + 1); i++) {
			const real_t x = local_to_global_x(i);
			// FastNoiseLite -> computationally expensive, do before mutex
			/*
			float e = 
				1.0 * noise->get_noise_2d(1 * x, 1 * z) + 
				0.5 * noise->get_noise_2d(2 * x, 2 * z) + 
				0.25 * noise->get_noise_2d(4 * x, 4 * z);
			e /= (1.0 + 0.5 + 0.25);
			e = Math::pow(e, 2.0f);
			float r = e;
			*/
			float r = generate_normalized_height(x, z);

			MutexLock mutex_lock(height_map_mutex);
			height_map->set_pixel(i, j, Color(r, 0, 0));
		}
	}
	// atomically check if active_task_count > 0
	if (active_task_count.fetch_sub(1,std::memory_order_acq_rel) > 1) return;

	// multi-threaded tasks done, call_deferred so that task is called on main thread 
	post_generation && post_generation->is_valid() ?
		post_generation->call_deferred() :
		DEBUG_PRINT_ERROR("add_chunk TASK IS INVALID");

	post_generation.reset();
}

void HeightMapData::_bind_methods() {
}