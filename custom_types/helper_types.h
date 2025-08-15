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