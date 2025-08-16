shader_type spatial;
//render_mode wireframe;
global uniform sampler2D grass_texture;
global uniform sampler2D rock_texture;
global uniform sampler2D sand_texture;

global uniform vec3 clipmap_position;
global uniform float amplitude;
global uniform float vert_step_size;
global uniform float clipmap_partition_length;
global uniform float height_exp;
uniform float lod_limit = 6.0;

uniform sampler2D heightmap;

uniform float min_rock_slope:hint_range(0.0,1.0) = 0.5;
uniform float max_grass_slope:hint_range(0.0,1.0) = 0.9;
uniform float min_rockgrass_height = 1.0;
uniform float max_sand_height = 5.0;

varying vec2 uv;
varying vec3 dev_albedo;
varying vec3 world_vertex;
varying vec3 vert;


float true_round(float value) {
	return floor(value + 0.5);
}

// heightmap has extra pixels on all sides for normal mapping -> use ratio to account for padding
float get_height(vec3 vertex) {
	float hmap_length = float(textureSize(heightmap,0).x);
	float ratio = (hmap_length-3.0) / hmap_length;
	vec2 heightmap_position = (vertex.xz / clipmap_partition_length) * ratio + 0.5;

	float height = pow(texture(heightmap, heightmap_position).r * amplitude, height_exp); 		// sample red channel
	return height;
}
vec3 get_normal(vec3 vertex) {
	vec3 west_vert = vertex + vec3(vert_step_size, 0.0, 0.0);
	west_vert.y = get_height(west_vert);
	vec3 north_vert = vertex - vec3(0.0, 0.0, vert_step_size);
	north_vert.y = get_height(north_vert);
	return normalize(
		cross(
			north_vert - vertex,
			west_vert - vertex
		)
	);
}

// consistent normals -> require adjacent vertices in the same direction
vec3 get_consitent_normal(vec3 vertex) {
	vec3 west_vert = vertex - vec3(vert_step_size, 0.0, 0.0);
	west_vert.y = get_height(west_vert);
	vec3 north_vert = vertex - vec3(0.0, 0.0, vert_step_size);
	north_vert.y = get_height(north_vert);
	return normalize(
		cross(
			north_vert - vertex,
			west_vert - vertex
		)
	);
}

vec3 to_normalmap(vec3 n) {
	n *= vec3(1.0, 1.0, 1.0);
	n = n / 2.0 + 0.5;
	n = vec3(n.x, n.z, n.y);
	return n;
}

void vertex() {
    world_vertex = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
    vert = VERTEX;
	
	vec3 clipmap_vertex = world_vertex - clipmap_position;
	float lod_exp = true_round(max(abs(clipmap_vertex.x), abs(clipmap_vertex.z)) / clipmap_partition_length);
	float lod = pow(2, min(lod_exp, lod_limit));
	float subdivision_length = min(lod, clipmap_partition_length) * vert_step_size;
	vec3 fraction = fract((vert + clipmap_partition_length / 2.0) / subdivision_length);

	VERTEX.y = mix(
		mix(
			get_height(vert - vec3(fraction.x * subdivision_length, 0, 0)), 
			get_height(vert + vec3((1.0-fraction.x) * subdivision_length, 0, 0)), 
			fraction.x
		), 
		mix(
			get_height(vert - vec3(0, 0, fraction.z * subdivision_length)), 
			get_height(vert + vec3(0, 0, (1.0-fraction.z) * subdivision_length)), 
			fraction.z
		), 
		ceil(fraction.z)
	);
	vert.y = VERTEX.y;
	
	// debug -> lod edge seam stitching using lerp
	//if (fraction.x == 0.5) COLOR = vec4(0, 0, 1, 1);
	//if (fraction.z == 0.5) COLOR = vec4(0, 0, 1, 1);
}

void fragment() {
	vec3 interpolated_vert = vert;
	interpolated_vert.y = get_height(vert);
	vec3 normal = get_normal(interpolated_vert);
	NORMAL_MAP = to_normalmap(normal);
	
	//Albedo Values
	vec3 grass_albedo = texture(grass_texture,UV*16.0).xyz;
	vec3 rock_albedo = texture(rock_texture,UV*16.0).xyz;
	vec3 sand_albedo = texture(sand_texture,UV*16.0).xyz;
	//Weights
	float rock_grass_weight = get_consitent_normal(interpolated_vert).y;
	float sand_rockgrass_weight = interpolated_vert.y;
	//Calculating Rock/Grass Weight
	rock_grass_weight = max(min_rock_slope, rock_grass_weight);
	rock_grass_weight = min(max_grass_slope, rock_grass_weight);
	rock_grass_weight -= min_rock_slope;
	rock_grass_weight /= max_grass_slope - min_rock_slope;
	//Calculating Sand/RockGrass Weight
	sand_rockgrass_weight = max(min_rockgrass_height, sand_rockgrass_weight);
	sand_rockgrass_weight = min(max_sand_height, sand_rockgrass_weight);
	sand_rockgrass_weight -= min_rockgrass_height;
	sand_rockgrass_weight /= max_sand_height - min_rockgrass_height;
	//Mixing and Assigning Albedo
	vec3 rockgrass_albedo = mix(rock_albedo, grass_albedo, rock_grass_weight);
	ALBEDO = mix(sand_albedo, rockgrass_albedo, sand_rockgrass_weight);
}

//void light() {
//	// Called for every pixel for every light affecting the material.
//	// Uncomment to replace the default light processing function with this one.
//}