/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Image loader for cursor assets
 */

#include "image_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* We'll use stb_image for PNG loading - it's a single header library */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO  /* We'll handle file I/O ourselves */
#include "stb_image.h"

extern struct platform *platform;

/**
 * Try to open file from multiple possible locations
 */
static FILE *try_open_cursor_file(const char *filename)
{
	FILE *f;
	
	/* Try paths in order of likelihood */
	const char *paths[] = {
		filename,                    /* Current directory: assets/hourglass.png */
		"../assets/%s",              /* One level up (if running from bin/) */
		"../../assets/%s",           /* Two levels up */
		NULL
	};
	
	/* Try direct path first */
	f = fopen(filename, "rb");
	if (f) return f;
	
	/* Extract just the filename from the path */
	const char *basename = strrchr(filename, '/');
	if (!basename) basename = strrchr(filename, '\\');
	basename = basename ? basename + 1 : filename;
	
	/* Try other paths */
	char path_buffer[512];
	for (int i = 1; paths[i] != NULL; i++) {
		snprintf(path_buffer, sizeof(path_buffer), paths[i], basename);
		f = fopen(path_buffer, "rb");
		if (f) {
			fprintf(stderr, "Found cursor at: %s\n", path_buffer);
			return f;
		}
	}
	
	return NULL;
}

/**
 * Load PNG image from file
 */
struct cursor_image *load_cursor_image(const char *filename)
{
	FILE *f = try_open_cursor_file(filename);
	if (!f) {
		/* File not found - this is expected if user hasn't added images yet */
		return NULL;
	}

	/* Get file size */
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	/* Read file into memory */
	unsigned char *buffer = (unsigned char *)malloc(size);
	if (!buffer) {
		fclose(f);
		return NULL;
	}

	if (fread(buffer, 1, size, f) != (size_t)size) {
		free(buffer);
		fclose(f);
		fprintf(stderr, "Warning: Failed to read cursor image: %s\n", filename);
		return NULL;
	}
	fclose(f);

	/* Decode PNG */
	struct cursor_image *img = (struct cursor_image *)calloc(1, sizeof(struct cursor_image));
	if (!img) {
		free(buffer);
		return NULL;
	}

	img->data = stbi_load_from_memory(buffer, size, &img->width, &img->height, &img->channels, 4);
	free(buffer);

	if (!img->data) {
		fprintf(stderr, "Warning: Failed to decode PNG: %s\n", filename);
		free(img);
		return NULL;
	}

	img->channels = 4;  /* Force RGBA */
	
	/* Scale down if image is too large (max 32x32 for performance and buffer limits) */
	const int MAX_SIZE = 32;
	if (img->width > MAX_SIZE || img->height > MAX_SIZE) {
		fprintf(stderr, "Warning: Image %s is %dx%d, scaling down to max %dx%d\n", 
		        filename, img->width, img->height, MAX_SIZE, MAX_SIZE);
		
		/* Calculate new size maintaining aspect ratio */
		int new_width = img->width;
		int new_height = img->height;
		
		if (img->width > img->height) {
			new_width = MAX_SIZE;
			new_height = (img->height * MAX_SIZE) / img->width;
		} else {
			new_height = MAX_SIZE;
			new_width = (img->width * MAX_SIZE) / img->height;
		}
		
		/* Simple nearest-neighbor downscaling */
		unsigned char *scaled_data = (unsigned char *)malloc(new_width * new_height * 4);
		if (scaled_data) {
			for (int y = 0; y < new_height; y++) {
				for (int x = 0; x < new_width; x++) {
					int src_x = (x * img->width) / new_width;
					int src_y = (y * img->height) / new_height;
					int src_idx = (src_y * img->width + src_x) * 4;
					int dst_idx = (y * new_width + x) * 4;
					
					scaled_data[dst_idx + 0] = img->data[src_idx + 0];
					scaled_data[dst_idx + 1] = img->data[src_idx + 1];
					scaled_data[dst_idx + 2] = img->data[src_idx + 2];
					scaled_data[dst_idx + 3] = img->data[src_idx + 3];
				}
			}
			
			stbi_image_free(img->data);
			img->data = scaled_data;
			img->width = new_width;
			img->height = new_height;
		}
	}
	
	fprintf(stderr, "✓ Loaded cursor image: %s (%dx%d)\n", filename, img->width, img->height);
	return img;
}

/**
 * Free cursor image
 */
void free_cursor_image(struct cursor_image *img)
{
	if (!img)
		return;

	if (img->data)
		stbi_image_free(img->data);

	free(img);
}

/**
 * Draw cursor image at position (centered)
 * Optimized: draws horizontal runs of same-color pixels as single boxes
 */
void draw_cursor_image(screen_t scr, struct cursor_image *img, int x, int y)
{
	if (!img || !img->data)
		return;

	/* Center the image on the cursor position */
	int start_x = x - img->width / 2;
	int start_y = y - img->height / 2;

	/* Draw image row by row, combining consecutive pixels of same color */
	for (int py = 0; py < img->height; py++) {
		int run_start = -1;
		char run_color[8] = {0};
		unsigned char last_r = 0, last_g = 0, last_b = 0;
		
		for (int px = 0; px <= img->width; px++) {
			unsigned char r = 0, g = 0, b = 0, a = 0;
			int is_opaque = 0;
			
			if (px < img->width) {
				int idx = (py * img->width + px) * 4;
				r = img->data[idx + 0];
				g = img->data[idx + 1];
				b = img->data[idx + 2];
				a = img->data[idx + 3];
				is_opaque = (a >= 128);
			}
			
			/* Check if we should continue the current run */
			int same_color = (r == last_r && g == last_g && b == last_b);
			
			if (is_opaque && (run_start < 0 || same_color)) {
				/* Start or continue run */
				if (run_start < 0) {
					run_start = px;
					last_r = r;
					last_g = g;
					last_b = b;
					snprintf(run_color, sizeof(run_color), "#%02x%02x%02x", r, g, b);
				}
			} else {
				/* End current run if any */
				if (run_start >= 0) {
					int run_length = px - run_start;
					platform->screen_draw_box(scr, start_x + run_start, start_y + py, 
					                          run_length, 1, run_color);
					run_start = -1;
				}
				
				/* Start new run if current pixel is opaque */
				if (is_opaque) {
					run_start = px;
					last_r = r;
					last_g = g;
					last_b = b;
					snprintf(run_color, sizeof(run_color), "#%02x%02x%02x", r, g, b);
				}
			}
		}
	}
}
