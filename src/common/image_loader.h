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
	unsigned char *data;  /* RGBA pixel data */
	int width;
	int height;
	int channels;         /* 4 for RGBA */
};

/* Load PNG image from file */
struct cursor_image *load_cursor_image(const char *filename);

/* Free cursor image */
void free_cursor_image(struct cursor_image *img);

/* Draw cursor image at position */
void draw_cursor_image(screen_t scr, struct cursor_image *img, int x, int y);

#endif /* IMAGE_LOADER_H */
