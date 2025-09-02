#pragma once

// For multi-threading
#include "custom_types/helper_types.h"
#include "custom_types/callable_lambda.h"

#include <optional>

class WorldData {
public:
    static RID world_scenario;
    static Ref<Shader> terrain_shader;
    
    static int SEED;
    /*
    * 64x64 is perfectly fine
    * 128x128 chunk resolution is standard
    * 256x256 is high fidelity
    * 512x512 is the practical maximum
    * anything above is overkill and a waste of compute
    * 
    * h_resolution = 2 ** (length_exp - step_exp) + 1 + padding
    */
    static real_t terrain_height_exp;
    static uint8_t step_exp;
    static real_t step_size;
    static uint8_t length_exp;
    static real_t length;
    static real_t terrain_amplitude;
    static real_t lod_limit;
    static int h_resolution;
    // Y ONLY OFFSET -> XZ NOT ACCOUNTED FOR CURRENTLY
    static Vector3 terrain_offset;

    static FastNoiseLite::NoiseType noise_type;
    static FastNoiseLite::FractalType fractal_type;
    static real_t noise_frequency;
    // number of noise calculations to run and add
    static real_t fractal_octaves;
    // change in frequency of successive octave
    static real_t fractal_lacunarity;
    // reduces strength of successive octaves
    static real_t fractal_gain;
};


class HeightMapData : public RefCounted {
	GDCLASS(HeightMapData, RefCounted);

    Ref<FastNoiseLite> noise;
    Ref<Image> height_map;
    Vector3 world_position;
    Vector3 start_pos;
    Vector3 end_pos;
    int subdivide_w;
    int subdivide_d;

	Mutex height_map_mutex;
    std::atomic_int active_task_count = 0;
    HashSet<u_int64_t> sub_task_ids;
    std::unique_ptr<Callable> post_generation;
public:
    double true_height(real_t h) const {
        return Math::pow(h * WorldData::terrain_amplitude, WorldData::terrain_height_exp) + WorldData::terrain_offset.y;
    }
    float generate_normalized_height(int x, int z) const { return (noise->get_noise_2d(x,z) + 1.0) / 2.0; }
    float get_height_local(int x, int z) const { return true_height(height_map->get_pixel(x,z).r); }   // local within image, not position

    real_t local_to_global_x(int i) { return start_pos.x + (i * WorldData::step_size); }
    real_t local_to_global_z(int j) { return start_pos.z + (j * WorldData::step_size); }

    void setup_height_map(Size2 size, Vector3 position);
    void generate_height_map(int j_begin, int j_end);

    // for collision mapping
    float generate_height(int x, int z) const {
        return (noise.is_valid() ? true_height(generate_normalized_height(x,z)) : 0.0);
    }
    Ref<Image> get_image() { 
        return height_map;
    }
    float get_height_global(Vector3 global) {
        Vector3 local = (world_position - global).abs().posmod(WorldData::length + WorldData::step_size);
        Vector3 scaled = (local / WorldData::step_size).round() + Vector3(1,0,1);
        return get_height_local(scaled.x, scaled.z);
    }
    bool in_bounds(Vector3 global_pos) {
        const bool x_bounds = (global_pos.x >= start_pos.x) && (global_pos.x < end_pos.x);
        const bool z_bounds = (global_pos.z >= start_pos.z) && (global_pos.z < end_pos.z);
        return x_bounds && z_bounds;
    }

    void update_noise_params() {
        noise->set_noise_type(WorldData::noise_type);
        noise->set_frequency(WorldData::noise_frequency);
        
        noise->set_fractal_type(WorldData::fractal_type);
        noise->set_fractal_octaves(WorldData::fractal_octaves);
        noise->set_fractal_lacunarity(WorldData::fractal_lacunarity);
        noise->set_fractal_gain(WorldData::fractal_gain);
        /*
        noise->set_domain_warp_enabled(true);
        noise->set_domain_warp_terrain_amplitude(50.0);
        noise->set_domain_warp_frequency(0.45);
        noise->set_domain_warp_type(FastNoiseLite::DOMAIN_WARP_SIMPLEX);
        noise->set_domain_warp_fractal_octaves();
        */
    }

    void _instantiate(Vector3 new_pos, Callable p_callable) {
        //float octave_total = 2.0 + (1.0 - (1.0 / lod));		// Partial Sum Formula (Geometric Series)
        if (noise.is_null()) {
            noise.instantiate();
            update_noise_params();
        }
        post_generation = std::make_unique<Callable>(p_callable); 
        setup_height_map(Size2(WorldData::length, WorldData::length), new_pos);
    }

	static void _bind_methods();

    HeightMapData();
    HeightMapData(Vector3 new_pos, Callable p_callable);
    ~HeightMapData();
};


class MeshData : public RefCounted {
	GDCLASS(MeshData, RefCounted);

    RID geometry_instance_rid;
    Ref<ShaderMaterial> shader_material;
    Ref<PlaneMesh> plane_mesh;
	Ref<ImageTexture> height_map_texture;

    Transform3D world_transform;
    Vector3 chunk_position;

public:
    void set_position(const Vector3 &new_pos) {
        chunk_position = new_pos;
        update_position();
    }
    void update_position() {
        world_transform.set_origin(chunk_position * WorldData::length + WorldData::terrain_offset);
        RS::get_singleton()->instance_set_transform(geometry_instance_rid, world_transform);
    }

    void update(Ref<Image> hmap_image, Vector3 new_pos) {
        if (height_map_texture.is_null()) {
            height_map_texture = ImageTexture::create_from_image(hmap_image);
            // set shader
            shader_material->set_shader_parameter("heightmap", height_map_texture);
            shader_material->set_shader_parameter("lod_limit", WorldData::lod_limit);
        }
        else {
            height_map_texture->update(hmap_image);
        }
        set_position(new_pos);
        set_visiblity(true);
    }

    Vector3 get_chunk_pos() const { return chunk_position; }
    void set_visiblity(bool p_visible) { RS::get_singleton()->instance_set_visible(geometry_instance_rid, p_visible); }

    void set_shader_material() {
        if (shader_material.is_null()) {
            shader_material.instantiate();
        }
        shader_material->set_shader(WorldData::terrain_shader);
    }

    // controls mesh culling distances
    void set_mesh_aabb(const real_t &aabb_factor) const {
        AABB aabb;
        aabb.grow_by(WorldData::length * aabb_factor);
        plane_mesh->set_custom_aabb(aabb);
    }

    MeshData();
    MeshData(LODS lod_factor, Vector3 grid_pos) : chunk_position(grid_pos) {
        set_shader_material();

        int lod = 1 << lod_factor[LODS::CENTER];
        int subdivide_w = (WorldData::length / (WorldData::step_size * lod)) - 1;
        int subdivide_d = (WorldData::length / (WorldData::step_size * lod)) - 1;

        plane_mesh.instantiate();
        plane_mesh->set_size(Size2(WorldData::length, WorldData::length));
        plane_mesh->set_subdivide_width(subdivide_w);
        plane_mesh->set_subdivide_depth(subdivide_d);
        plane_mesh->surface_set_material(0, shader_material);

        geometry_instance_rid = RS::get_singleton()->instance_create();
		RS::get_singleton()->instance_set_scenario(geometry_instance_rid, WorldData::world_scenario);
	    RS::get_singleton()->instance_set_base(geometry_instance_rid, plane_mesh->get_rid());

        // INF used for testing -> will use reasonable value after finalizing terrain generation design
        set_mesh_aabb(Math::INF);
        set_position(grid_pos);
    }
    ~MeshData() {
        if (geometry_instance_rid.is_valid()) {
            RS::get_singleton()->free(geometry_instance_rid);
        }
        shader_material.unref();
        WorldData::terrain_shader.unref();
    }
};