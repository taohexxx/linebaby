#include "strokes.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include "gl.h"
#include "util.h"

#define VERTICES_CAPACITY 2048
#define MAX_STROKE_VERTICES 128

static struct {
	struct bezier_point vertices[VERTICES_CAPACITY];
	size_t vertices_len;
	
	struct lb_stroke strokes[64];
	uint8_t strokes_len;
} data;

// Timeline
enum lb_draw_mode lb_strokes_drawMode = DRAW_REALTIME;
bool lb_strokes_playing = false;
float lb_strokes_timelineDuration = 10.0f;
float lb_strokes_timelinePosition = 5.0f;
bool lb_strokes_draggingPlayhead = false;
static float draw_start_time;
enum lb_input_mode input_mode = INPUT_DRAW;
static bool drawing = false;
static uint16_t drawing_stroke_idx;

float lb_strokes_setTimelinePosition(float pos) {
	pos = (pos < 0.0f ? 0.0f : pos);
	pos = (pos > lb_strokes_timelineDuration ? lb_strokes_timelineDuration : pos);
	return lb_strokes_timelinePosition = pos;
}

void lb_strokes_updateTimeline(float dt) {
	if((!drawing && !lb_strokes_playing) || lb_strokes_draggingPlayhead) return;
	lb_strokes_timelinePosition += dt;

	if(lb_strokes_timelinePosition > lb_strokes_timelineDuration) {
		lb_strokes_timelinePosition = 0;
	}
}

// Drawing
static struct {
	GLuint vao;
	GLuint vbo;
} gl_lines;

static struct shaderProgram line_shader;
#include "../build/assets/shaders/line.frag.c"
#include "../build/assets/shaders/line.vert.c"
enum line_shader_uniform {
	LINE_UNIFORM_PROJECTION = 0,
	LINE_UNIFORM_COLOR,
	LINE_UNIFORM_POINT_SIZE
};

static struct shaderProgram brush_shader;
#include "../build/assets/shaders/brush.frag.c"
#include "../build/assets/shaders/brush.vert.c"
enum brush_shader_uniform {
	BRUSH_UNIFORM_PROJECTION = 0,
	BRUSH_UNIFORM_TRANSLATION,
	BRUSH_UNIFORM_SCALE,
	BRUSH_UNIFORM_ROTATION,
	// BRUSH_UNIFORM_MASK_TEXTURE,
	BRUSH_UNIFORM_BRUSH_TEXTURE
};

static GLuint mask_texture;
static GLuint brush_texture;

#define RADIAL_GRADIENT_SIZE 64

void upload_texture() {

	static uint8_t pix[RADIAL_GRADIENT_SIZE][RADIAL_GRADIENT_SIZE];
	const uint8_t midpoint = RADIAL_GRADIENT_SIZE / 2;
	const float scale = 2.5f;

	for(uint8_t y = 0; y < RADIAL_GRADIENT_SIZE; y++) {
		for(uint8_t x = 0; x < RADIAL_GRADIENT_SIZE; x++) {
			double a = sqrt(pow(midpoint - x, 2) + pow(midpoint - y, 2));

			a = (a - midpoint) / (a - RADIAL_GRADIENT_SIZE) * scale;
			
			if(a > 1) a = 1;
			else if(a < 0) a = 0;

			pix[y][x] = a * 255;
		}
	}

	glGenTextures(1, &mask_texture);
	glBindTexture(GL_TEXTURE_2D, mask_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, RADIAL_GRADIENT_SIZE, RADIAL_GRADIENT_SIZE, 0, GL_RED, GL_UNSIGNED_BYTE, pix);

	uint32_t brush_width, brush_height;
	bool brush_alpha;
	GLubyte* brush_pix;
	if(!loadPNG("src/assets/images/pencil.png", &brush_width, &brush_height, &brush_alpha, &brush_pix)) {
		assert(false);
		return;
	}
	
	glGenTextures(1, &brush_texture);
	glBindTexture(GL_TEXTURE_2D, brush_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, brush_width, brush_height, 0, GL_RED, GL_UNSIGNED_BYTE, brush_pix);

	free(brush_pix);
}

static GLuint plane_vao;
static GLuint plane_vbo;

void upload_plane() {
	static float vertices[] = {
	//  Position   // Texcoords
		-0.5f,  0.5f, 0.0f, 0.0f, // Top-left
		 0.5f,  0.5f, 1.0f, 0.0f, // Top-right
		 0.5f, -0.5f, 1.0f, 1.0f, // Bottom-right
		-0.5f,  0.5f, 0.0f, 0.0f, // Top-left
		 0.5f, -0.5f, 1.0f, 1.0f, // Bottom-right
		-0.5f, -0.5f, 0.0f, 1.0f  // Bottom-left
	};

	glGenVertexArrays(1, &plane_vao);
	glBindVertexArray(plane_vao);
	glCheckError();

	glGenBuffers(1, &plane_vbo);
	glCheckError();

	size_t vertex_stride = 0;
	vertex_stride += sizeof(GLfloat) * 2; // XY
	vertex_stride += sizeof(GLfloat) * 2; // UV

	glBindBuffer(GL_ARRAY_BUFFER, plane_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizei) sizeof(GLfloat) * 4 * 6, vertices, GL_STATIC_DRAW);
	glCheckError();

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, vertex_stride, (char*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vertex_stride, (char*)8);
	glEnableVertexAttribArray(1);
	glCheckError();
}

void lb_strokes_init() {
	
	// Line shader
	{
		static const char* uniformNames[] = {
			"projection",
			"color",
			"pointSize"
		};

		buildProgram(
			loadShader(GL_VERTEX_SHADER, (char*)src_assets_shaders_line_vert, (int*)&src_assets_shaders_line_vert_len),
			loadShader(GL_FRAGMENT_SHADER, (char*)src_assets_shaders_line_frag, (int*)&src_assets_shaders_line_frag_len),
			uniformNames, sizeof(uniformNames)/sizeof(uniformNames[0]), &line_shader);
	}

	// Brush shader
	{
		static const char* uniformNames[] = {
			"projection",
			"translation",
			"scale",
			"rotation",
			"maskTex",
			"brushTex"
		};

		buildProgram(
			loadShader(GL_VERTEX_SHADER, (char*)src_assets_shaders_brush_vert, (int*)&src_assets_shaders_brush_vert_len),
			loadShader(GL_FRAGMENT_SHADER, (char*)src_assets_shaders_brush_frag, (int*)&src_assets_shaders_brush_frag_len),
			uniformNames, sizeof(uniformNames)/sizeof(uniformNames[0]), &brush_shader);
	}
	

	glGenVertexArrays(1, &gl_lines.vao);
	glBindVertexArray(gl_lines.vao);
	glCheckError();
	
	glGenBuffers(1, &gl_lines.vbo);
	glCheckError();
	
	size_t vertexStride = 0;
	vertexStride += sizeof(GLfloat) * 2;
	
	glBindBuffer(GL_ARRAY_BUFFER, gl_lines.vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizei) sizeof(vec2) * MAX_STROKE_VERTICES, NULL, GL_DYNAMIC_DRAW);
	glCheckError();
	
	// Enable vertex attributes
	void* attrib_offset = 0;
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, vertexStride, attrib_offset);
	attrib_offset += sizeof(GLfloat) * 2;
	glEnableVertexAttribArray(0);
	glCheckError();



	upload_plane();
	upload_texture();

	// Seed data
	data.vertices_len = 2;
	data.vertices[0] = (struct bezier_point){
		.anchor = (vec2){100.0f, 100.0f},
		.handles = {(vec2){50.0f, 100.0f}, (vec2){150.0f, 100.0f}}
	};
	data.vertices[1] = (struct bezier_point){
		.anchor = (vec2){350.0f, 350.0f},
		.handles = {(vec2){300.0f, 350.0f}, (vec2){400.0f, 350.0f}}
	};
	data.strokes_len = 1;
	data.strokes[0] = (struct lb_stroke){
		.vertices = &data.vertices[0],
		.global_start_time = 0,
		.global_duration = 5.0f,
		.vertices_len = 2
	};
}

void lb_strokes_render() {

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);

	// Draw brushes
	glUseProgram(brush_shader.program);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	float scale = 4.0f;

	glUniformMatrix4fv(brush_shader.uniforms[BRUSH_UNIFORM_PROJECTION], 1, GL_FALSE, (const GLfloat*) screen_ortho);
	glUniform2f(brush_shader.uniforms[BRUSH_UNIFORM_SCALE], scale, scale); // TODO: Configurable
	for(size_t i = 0; i < data.strokes_len; i++) {
		if(!data.strokes[i].vertices_len) continue;
		if(data.strokes[i].global_start_time > lb_strokes_timelinePosition) continue;
		if(data.strokes[i].global_start_time + data.strokes[i].global_duration < lb_strokes_timelinePosition) continue;
		
		float percent_drawn = (lb_strokes_timelinePosition - data.strokes[i].global_start_time) / data.strokes[i].global_duration;
		
		float total_length = 0.0f;
		for(size_t v = 0; v < data.strokes[i].vertices_len-1; v++) {
			total_length += bezier_distance_update_cache(&data.strokes[i].vertices[v], &data.strokes[i].vertices[v+1]);
		}
		float total_length_drawn = total_length*percent_drawn;
		//TODO: Optimize out the double calculation of length, cache the total length if possible
		
		// Brush
		float length_accum = 0.0f;
		for(size_t v = 0; v < data.strokes[i].vertices_len-1; v++) {
			struct bezier_point* a = &data.strokes[i].vertices[v];
			struct bezier_point* b = &data.strokes[i].vertices[v+1];
			float segment_length = bezier_distance_update_cache(a,b);
			
			float percent_segment_drawn = (total_length_drawn - length_accum) / segment_length;
			if(percent_segment_drawn <= 0) break;
			if(percent_segment_drawn > 1) percent_segment_drawn = 1;
			
			unsigned int total_equidistant_points_len = (unsigned int)ceil(segment_length / scale);
			unsigned int drawn_points_len = (unsigned int)ceil(percent_segment_drawn * total_equidistant_points_len);
			length_accum += segment_length;

			//TODO: Instanced drawing
			vec2 equidistant_points[drawn_points_len];
			for(size_t p = 0; p < drawn_points_len; p++) {
				equidistant_points[p] = bezier_cubic(a,b,bezier_distance_closest_t(p/(float)total_equidistant_points_len));
				glUniform1f(brush_shader.uniforms[BRUSH_UNIFORM_ROTATION], (float)p); //rotation
				glUniform2f(brush_shader.uniforms[BRUSH_UNIFORM_TRANSLATION], equidistant_points[p].x, equidistant_points[p].y);
				
				// glUniform1i(brush_shader.uniforms[BRUSH_UNIFORM_MASK_TEXTURE], 0);
				// glActiveTexture(GL_TEXTURE0);
				// glBindTexture(GL_TEXTURE_2D, mask_texture);
				
				glUniform1i(brush_shader.uniforms[BRUSH_UNIFORM_BRUSH_TEXTURE], 1);
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, brush_texture);

				glBindVertexArray(plane_vao);
				glDrawArrays(GL_TRIANGLES, 0, 6);
			}
			
			// glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vec2)*10, &equidistant_points);
			// glDrawArrays(GL_POINTS, 0, 10);
			
			if(percent_segment_drawn < 1.0f) break;
		}
	}

	// Draw lines
	glUseProgram(line_shader.program);
	glEnable(GL_PROGRAM_POINT_SIZE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	glUniformMatrix4fv(line_shader.uniforms[LINE_UNIFORM_PROJECTION], 1, GL_FALSE, (const GLfloat*) screen_ortho);
	glUniform3f(line_shader.uniforms[LINE_UNIFORM_COLOR], 1.0f, 0.0f, 0.0f);
	glUniform1f(line_shader.uniforms[LINE_UNIFORM_POINT_SIZE], 5.0f);

	glBindVertexArray(gl_lines.vao);
	glBindBuffer(GL_ARRAY_BUFFER, gl_lines.vbo);
	
	// -- Curves
	for(size_t i = 0; i < data.strokes_len; i++) {
		for(size_t v = 0; v < data.strokes[i].vertices_len-1; v++) {
			struct bezier_point* a = &data.strokes[i].vertices[v];
			struct bezier_point* b = &data.strokes[i].vertices[v+1];
			float len = bezier_estimate_length(a, b);
			uint16_t segments = hyperbola_min_segments(len);
			float step = 1.0f / (float)segments;
			size_t lines = 0;
			for(float t = 0; t <= 1; t += step) {
				vec2 loc = bezier_cubic(a, b, t);
				glBufferSubData(GL_ARRAY_BUFFER, lines*sizeof(vec2), sizeof(vec2), &loc);
				lines++;
			}
			glDrawArrays(GL_LINE_STRIP, 0, lines);
		}
	}
	
	// -- Handle lines
	for(size_t i =  0; i < data.strokes_len; i++) {
		glUniform3f(line_shader.uniforms[LINE_UNIFORM_COLOR], 1.0f, 0.0f, 0.0f);
		for(size_t v = 0; v < data.strokes[i].vertices_len; v++) {
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vec2), &data.strokes[i].vertices[v].handles[0]);
			glBufferSubData(GL_ARRAY_BUFFER, sizeof(vec2), sizeof(vec2), &data.strokes[i].vertices[v].anchor);
			glBufferSubData(GL_ARRAY_BUFFER, sizeof(vec2)*2, sizeof(vec2), &data.strokes[i].vertices[v].handles[1]);
			glDrawArrays(GL_LINE_STRIP, 0, 3);
		}
	}
	
	// -- Control points
	for(size_t i = 0; i < data.strokes_len; i++) {
		glUniform3f(line_shader.uniforms[LINE_UNIFORM_COLOR], 1.0f, 0.0f, 0.0f);
		glUniform1f(line_shader.uniforms[LINE_UNIFORM_POINT_SIZE], 5.0f);

		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vec2) * 3 * data.strokes[i].vertices_len, data.strokes[i].vertices);
		glDrawArrays(GL_POINTS, 0, 3 * data.strokes[i].vertices_len);
		
		glUniform3f(line_shader.uniforms[LINE_UNIFORM_COLOR], 1.0f, 1.0f, 1.0f);
		glUniform1f(line_shader.uniforms[LINE_UNIFORM_POINT_SIZE], 3.0f);
		
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vec2) * 3 * data.strokes[i].vertices_len, data.strokes[i].vertices);
		glDrawArrays(GL_POINTS, 0, 3 * data.strokes[i].vertices_len);
	}

	glCheckError();
}

bool lb_strokes_isDrawing() {
	return drawing;
}

struct lb_stroke* lb_strokes_getSelectedStroke() {
	if(data.strokes_len == 0) return NULL;
	return &data.strokes[data.strokes_len-1];
}

static enum drag_mode {
	DRAG_NONE,
	DRAG_ANCHOR,
	DRAG_HANDLE
} drag_mode = DRAG_NONE;

static vec2* drag_vec = NULL;

void lb_strokes_handleMouseDown(vec2 point, float time) {
	switch(input_mode) {
		case INPUT_SELECT: {
			// Search all control points
			struct lb_stroke* selected = lb_strokes_getSelectedStroke();
			if(!selected) return;

			static float select_tolerance_dist = 5.0f;
			for(size_t i = 0; i < selected->vertices_len; i++) {
				if(vec2_dist(point, selected->vertices[i].anchor) <= select_tolerance_dist) {
					drag_mode = DRAG_ANCHOR;
					drag_vec = &selected->vertices[i].anchor;
				} else if(vec2_dist(point, selected->vertices[i].handles[0]) <= select_tolerance_dist) {
					drag_mode = DRAG_HANDLE;
					drag_vec = &selected->vertices[i].handles[0];
				} else if(vec2_dist(point, selected->vertices[i].handles[1]) <= select_tolerance_dist) {
					drag_mode = DRAG_HANDLE;
					drag_vec = &selected->vertices[i].handles[1];
				}
			}
			break;
		}

		case INPUT_DRAW: {
			struct lb_stroke* selected = lb_strokes_getSelectedStroke();
			if(!selected) {
				selected = &data.strokes[data.strokes_len];
				data.strokes_len++;

				selected->vertices = &data.vertices[data.vertices_len];
				selected->vertices_len = 0;
				selected->global_start_time = lb_strokes_timelinePosition;
				selected->global_duration = 1.0f;
			}
			
			assert(selected->vertices_len < MAX_STROKE_VERTICES);
			assert(data.vertices_len < VERTICES_CAPACITY);
			
			selected->vertices[selected->vertices_len].anchor = point;
			selected->vertices[selected->vertices_len].handles[0] = (vec2){point.x - 20, point.y};
			selected->vertices[selected->vertices_len].handles[1] = (vec2){point.x + 20, point.y};

			selected->vertices_len++;
			data.vertices_len++;
			break;
		}
	}
}

void lb_strokes_handleMouseMove(vec2 point, float time) {
	switch(drag_mode) {
		case DRAG_NONE:
			return;
		case DRAG_ANCHOR:
		case DRAG_HANDLE:
			*drag_vec = point;
			break;
	}
}

void lb_strokes_handleMouseUp() {
	drag_mode = DRAG_NONE;
}