/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Hint Sorting Module Implementation
 */

#include "hint_sorter.h"
#include <stdlib.h>

/* Context for qsort comparison */
static int screen_center_x = -1;
static int screen_center_y = -1;

/**
 * Compare function for sorting hints by distance from screen center
 */
static int compare_distance_from_center(const void *a, const void *b)
{
	const struct hint *ha = (const struct hint *)a;
	const struct hint *hb = (const struct hint *)b;

	/* Calculate center of each hint */
	int ax = ha->x + ha->w / 2;
	int ay = ha->y + ha->h / 2;
	int bx = hb->x + hb->w / 2;
	int by = hb->y + hb->h / 2;

	/* Calculate squared distance from screen center */
	int dist_a = (ax - screen_center_x) * (ax - screen_center_x) +
	             (ay - screen_center_y) * (ay - screen_center_y);
	int dist_b = (bx - screen_center_x) * (bx - screen_center_x) +
	             (by - screen_center_y) * (by - screen_center_y);

	return dist_a - dist_b;  /* Closer to center = smaller value */
}

void hint_sorter_sort_by_center(struct hint *hints, size_t count, screen_t scr)
{
	if (!hints || count == 0 || !scr) {
		return;
	}

	/* Get screen dimensions to find center */
	int sw, sh;
	extern struct platform *platform;
	platform->screen_get_dimensions(scr, &sw, &sh);

	screen_center_x = sw / 2;
	screen_center_y = sh / 2;

	/* Sort using standard library qsort */
	qsort(hints, count, sizeof(struct hint), compare_distance_from_center);

	/* Reset static variables to detect bugs if used incorrectly */
	screen_center_x = -1;
	screen_center_y = -1;
}
