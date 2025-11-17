/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Image loader for cursor assets
 */

#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include "../platform.h"

/* Image structure */
struct cursor_image {
	unsigned char *data;  /* RGBA pixel data (all frames concatenated) */
	int width;
	int height;
	int channels;         /* 4 for RGBA */
	int frame_count;      /* Number of frames (1 for static, >1 for animated) */
	int *delays;          /* Delay for each frame in milliseconds */
	int current_frame;    /* Current frame index for animation */
	uint64_t last_update; /* Last frame update time */
};

/* Load PNG/GIF image from file */
struct cursor_image *load_cursor_image(const char *filename);

/* Free cursor image */
void free_cursor_image(struct cursor_image *img);

/* Draw cursor image at position (handles animation automatically) */
void draw_cursor_image(screen_t scr, struct cursor_image *img, int x, int y);

#endif /* IMAGE_LOADER_H */
