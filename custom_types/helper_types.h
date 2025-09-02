#pragma once

#include "core/core_bind.h"
#include "scene/3d/node_3d.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"

#include "servers/rendering_server.h"
#include "core/math/aabb.h"
#include "core/math/math_funcs.h"
#include "core/templates/ring_buffer.h"

#include "modules/noise/fastnoise_lite.h"	// inherits from noise class in noise.h
#include "scene/resources/image_texture.h"
#include "scene/resources/3d/primitive_meshes.h"


#include <atomic>
#include <memory>
#include <queue>

// debug messages that print often
#ifdef DEBUG_ENABLED
//#define DEBUG_PRINT_OFTEN(...) print_line(__VA_ARGS__)
#define DEBUG_PRINT_OFTEN(...) void()
#else
#define DEBUG_PRINT_OFTEN(...) void()
#endif

// debug messages that print rarely
#ifdef DEBUG_ENABLED
#define DEBUG_PRINT_RARE(...) print_line(__VA_ARGS__)
#else
#define DEBUG_PRINT_RARE(...) void()
#endif

#ifdef DEBUG_ENABLED
#define DEBUG_PRINT_ERROR(...) print_error(stringify_variants(__VA_ARGS__))
#else
#define DEBUG_PRINT_ERROR(...) void()
#endif

// purely for keeping code cleaner
/*
-> if flip is false, incrementing iteration
-> if flip is true, decrementing iteration
*/
struct range_flip {
    range_flip(int min, int max, bool flip = false): last(max), iter(min) {
        if (flip) {
            iter = max;
            last = min;
            step = -1;
        }
    }

    // Iterable functions
    _FORCE_INLINE_ const range_flip& begin() const { return *this; }
    _FORCE_INLINE_ const range_flip& end() const { return *this; }

    // Iterator functions
    _FORCE_INLINE_ bool operator==(const range_flip&) const { return iter == last; }
    _FORCE_INLINE_ bool operator!=(const range_flip&) const { return iter != last; }
    _FORCE_INLINE_ void operator++() { iter += step; }
    _FORCE_INLINE_ int operator*() const { return iter; }

private:
    int last;
    int iter;
    int step = 1;
};



struct LODS {
    uint8_t C = 0;
    uint8_t N = 0;
    uint8_t S = 0;
    uint8_t W = 0;
    uint8_t E = 0;

    LODS() {}
    LODS(int x, int z) {
        C = MAX(abs(x), abs(z));
		W = MAX(abs(x+1), abs(z));
		E = MAX(abs(x-1), abs(z));
		N = MAX(abs(x), abs(z+1));
		S = MAX(abs(x), abs(z-1));
    }

    LODS(int x, int z, int limit) {
        C = MAX(abs(x), abs(z));
		W = MAX(abs(x+1), abs(z));
		E = MAX(abs(x-1), abs(z));
		N = MAX(abs(x), abs(z+1));
		S = MAX(abs(x), abs(z-1));

        C = MIN(C, limit);
        W = MIN(W, limit);
        E = MIN(E, limit);
        N = MIN(N, limit);
        S = MIN(S, limit);
    }
    _FORCE_INLINE_ bool operator==(const LODS c) const {
        return (C == c.C && N == c.N && S == c.S && W == c.W && E == c.E);
    }
    _FORCE_INLINE_ bool operator!=(const LODS c) const {
        return (C != c.C || N != c.N || S != c.S || W != c.W || E != c.E);
    }

    enum : uint8_t { CENTER, NORTH, SOUTH, WEST, EAST };
    _FORCE_INLINE_ uint8_t operator[](const uint8_t adj) const {
        switch (adj) {
        case CENTER: return C;
        case NORTH: return N;
        case SOUTH: return S;
        case WEST: return W;
        case EAST: return E;
        } return 0;
    }
};


#define CREATE_PROPERTY_VAL(type, name) 		\
	void set_##name(type new_##name) {          \
        if (name == new_##name) return;         \
        name = new_##name;                      \
    } 	\
	type get_##name() const { return name; }

// setter input is const reference
#define CREATE_PROPERTY_REF(type, name) 		\
	void set_##name(const type &new_##name) {   \
        if (name == new_##name) return;         \
        name = new_##name;                      \
    } 	\
	type get_##name() const { return name; }

// setter input is const reference
#define CREATE_PROPERTY_REF_STATIC(type, name, static_class) 	   \
	void set_##name(const type &new_##name) {                      \
        if (static_class::name == new_##name) return;              \
        static_class::name = new_##name;                           \
    } 	\
	type get_##name() const { return static_class::name; }

// setter is undefined
#define CREATE_PROPERTY_REF_CUSTOM(type, name) 	\
	type get_##name() const { return name; } 	\
	void set_##name(const type &new_##name)

#define CREATE_PROPERTY_REF_CUSTOM_STATIC(type, name, static_class) 	\
	type get_##name() const { return static_class::name; } 	\
	void set_##name(const type &new_##name)

// value requires mutex
#define CREATE_PROPERTY_REF_MUTEX(type, name, p_mutex) 	\
	void set_##name(const type &new_##name) { MutexLock mutex_lock(p_mutex); name = new_##name; } 	\
	type get_##name() const { MutexLock mutex_lock(p_mutex); return name; }

// bind method to button property
#define BIND_BUTTON_PROP(class, name, editor_name)	\
	ClassDB::bind_method(D_METHOD(#name), &class::name);	\
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, #name, PROPERTY_HINT_TOOL_BUTTON, editor_name, PROPERTY_USAGE_EDITOR), "", #name)

// bind set/get methods
#define BIND_SET_GET(class, name)	\
	ClassDB::bind_method(D_METHOD("set_"#name, "new_"#name), &class::set_##name); 	\
	ClassDB::bind_method(D_METHOD("get_"#name), &class::get_##name)

// bind set/get methods and add as property
#define BIND_SET_GET_PROP(class, name, type, ...) 	\
	BIND_SET_GET(class, name);	\
	ADD_PROPERTY(PropertyInfo(type, #name, __VA_ARGS__), "set_"#name, "get_"#name)