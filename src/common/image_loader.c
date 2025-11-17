/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Image loader for cursor assets
 */

#include "image_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* We'll use stb_image for PNG/GIF loading - it's a single header library */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_NO_STDIO  /* We'll handle file I/O ourselves */
#include "stb_image.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

extern struct platform *platform;

/* Import config functions */
extern int config_get_int(const char *key);

/* Get current time in milliseconds */
static uint64_t get_time_ms(void)
{
#ifdef _WIN32
	return GetTickCount64();
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

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

	/* Decode image (PNG or GIF) */
	struct cursor_image *img = (struct cursor_image *)calloc(1, sizeof(struct cursor_image));
	if (!img) {
		free(buffer);
		return NULL;
	}

	/* Try to load as animated GIF first */
	int *delays_raw = NULL;
	int z_frames = 0;
	
	fprintf(stderr, "Loading image: %s...\n", filename);
	
	img->data = stbi_load_gif_from_memory(buffer, size, &delays_raw, 
	                                       &img->width, &img->height, &z_frames, &img->channels, 4);
	
	if (img->data && z_frames > 0) {
		/* Successfully loaded as GIF */
		fprintf(stderr, "✓ Loaded animated GIF: %s (%dx%d, %d frames)\n", 
		        filename, img->width, img->height, z_frames);
		
		/* Limit frame count for very large GIFs to improve performance */
		int max_frames = config_get_int("cursor_max_frames");
		if (max_frames <= 0) max_frames = 60;
		
		if (z_frames > max_frames) {
			fprintf(stderr, "Warning: GIF has %d frames, limiting to %d for performance\n", 
			        z_frames, max_frames);
			z_frames = max_frames;
		}
		
		img->frame_count = z_frames;
		img->delays = (int *)malloc(z_frames * sizeof(int));
		if (img->delays) {
			/* Convert delays from centiseconds to milliseconds */
			for (int i = 0; i < z_frames; i++) {
				img->delays[i] = delays_raw[i] * 10;  /* centiseconds to ms */
				if (img->delays[i] < 20) img->delays[i] = 100;  /* Min 100ms per frame */
			}
		}
		stbi_image_free(delays_raw);
	} else {
		/* Try loading as static image (PNG or single-frame GIF) */
		img->data = stbi_load_from_memory(buffer, size, &img->width, &img->height, &img->channels, 4);
		if (!img->data) {
			fprintf(stderr, "Warning: Failed to decode image: %s\n", filename);
			free(buffer);
			free(img);
			return NULL;
		}
		img->frame_count = 1;
		img->delays = NULL;
	}
	
	free(buffer);
	img->channels = 4;  /* Force RGBA */
	img->current_frame = 0;
	img->last_update = get_time_ms();
	
	/* Scale down if image is too large (max 32x32 for performance and buffer limits) */
	const int MAX_SIZE = 32;
	if (img->width > MAX_SIZE || img->height > MAX_SIZE) {
		fprintf(stderr, "Scaling %s from %dx%d to max %dx%d", 
		        filename, img->width, img->height, MAX_SIZE, MAX_SIZE);
		
		if (img->frame_count > 1) {
			fprintf(stderr, " (%d frames)...\n", img->frame_count);
		} else {
			fprintf(stderr, "...\n");
		}
		
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
		
		/* Scale all frames */
		int frame_size = img->width * img->height * 4;
		int new_frame_size = new_width * new_height * 4;
		unsigned char *scaled_data = (unsigned char *)malloc(new_frame_size * img->frame_count);
		
		if (scaled_data) {
			int progress_step = img->frame_count / 10;  /* Show progress every 10% */
			if (progress_step < 1) progress_step = 1;
			
			for (int frame = 0; frame < img->frame_count; frame++) {
				/* Show progress for large GIFs */
				if (img->frame_count > 20 && frame % progress_step == 0) {
					fprintf(stderr, "  Scaling frame %d/%d...\r", frame + 1, img->frame_count);
					fflush(stderr);
				}
				
				unsigned char *src_frame = img->data + (frame * frame_size);
				unsigned char *dst_frame = scaled_data + (frame * new_frame_size);
				
				for (int y = 0; y < new_height; y++) {
					for (int x = 0; x < new_width; x++) {
						int src_x = (x * img->width) / new_width;
						int src_y = (y * img->height) / new_height;
						int src_idx = (src_y * img->width + src_x) * 4;
						int dst_idx = (y * new_width + x) * 4;
						
						dst_frame[dst_idx + 0] = src_frame[src_idx + 0];
						dst_frame[dst_idx + 1] = src_frame[src_idx + 1];
						dst_frame[dst_idx + 2] = src_frame[src_idx + 2];
						dst_frame[dst_idx + 3] = src_frame[src_idx + 3];
					}
				}
			}
			
			if (img->frame_count > 20) {
				fprintf(stderr, "  Scaling complete!                    \n");
			}
			
			stbi_image_free(img->data);
			img->data = scaled_data;
			img->width = new_width;
			img->height = new_height;
		}
	}
	
	if (img->frame_count == 1) {
		fprintf(stderr, "✓ Loaded cursor image: %s (%dx%d)\n", filename, img->width, img->height);
	}
	return img;
}

/**
 * Free cursor image
 */
void free_cursor_image(struct cursor_image *img)
{
	if (!img)
		return;

	if (img->data) {
		if (img->frame_count > 1)
			free(img->data);  /* Animated GIF uses malloc */
		else
			stbi_image_free(img->data);  /* Static image uses stbi */
	}
	
	if (img->delays)
		free(img->delays);

	free(img);
}

/**
 * Draw cursor image at position (centered)
 * Optimized: draws horizontal runs of same-color pixels as single boxes
 * Handles animation automatically for GIFs
 */
void draw_cursor_image(screen_t scr, struct cursor_image *img, int x, int y)
{
	if (!img || !img->data)
		return;

	/* Update animation frame if needed */
	if (img->frame_count > 1 && img->delays) {
		uint64_t now = get_time_ms();
		int base_delay = img->delays[img->current_frame];
		
		/* Apply speed multiplier from config (100 = normal speed) */
		int speed_percent = config_get_int("cursor_animation_speed");
		if (speed_percent <= 0) speed_percent = 100;
		
		/* Calculate adjusted delay: higher speed = shorter delay */
		int adjusted_delay = (base_delay * 100) / speed_percent;
		if (adjusted_delay < 10) adjusted_delay = 10;  /* Min 10ms */
		
		if (now - img->last_update >= (uint64_t)adjusted_delay) {
			img->current_frame = (img->current_frame + 1) % img->frame_count;
			img->last_update = now;
		}
	}

	/* Get pointer to current frame data */
	int frame_size = img->width * img->height * 4;
	unsigned char *frame_data = img->data + (img->current_frame * frame_size);

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
				r = frame_data[idx + 0];
				g = frame_data[idx + 1];
				b = frame_data[idx + 2];
				a = frame_data[idx + 3];
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
