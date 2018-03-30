#pragma once

#include <stdint.h>

// -- Globals --
extern int32_t windowWidth, windowHeight;
extern int32_t framebufferWidth, framebufferHeight;

extern float screen_ortho[4][4];
extern float* update_screen_ortho();

// -- Types --
typedef struct vec2 {
	float x, y;
} vec2;

float vec2_dist(const vec2 a, const vec2 b);
float vec2_len(const vec2 a);
vec2 vec2_add(const vec2 a, const vec2 b);
vec2 vec2_sub(const vec2 a, const vec2 b);

typedef union color32 {
	struct {
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;
	};
	uint32_t u;
} color32;

struct bezier_point {
	vec2 anchor;
	vec2 handles[2];
};

vec2 bezier_cubic(const struct bezier_point* a, const struct bezier_point* b, const float t);
float bezier_estimate_length(const struct bezier_point* a, const struct bezier_point* b);
uint16_t hyperbola_min_segments(const float length);

#define BEZIER_DISTANCE_CACHE_SIZE 512
float bezier_distance_update_cache(const struct bezier_point* a, const struct bezier_point* b);
float bezier_distance_closest_t(float dist_t);