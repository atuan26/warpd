/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Hint Sorting Module
 *
 * Provides sorting strategies for hints (e.g., by distance from screen center)
 */

#ifndef HINT_SORTER_H
#define HINT_SORTER_H

#include "../platform.h"
#include <stddef.h>

/**
 * Sort hints by distance from screen center (closest first)
 *
 * This is used in numeric mode when text filtering to give lower
 * numbers to hints closer to the center of attention.
 *
 * @param hints Array of hints to sort
 * @param count Number of hints
 * @param scr Screen to get center coordinates
 */
void hint_sorter_sort_by_center(struct hint *hints, size_t count, screen_t scr);

#endif /* HINT_SORTER_H */
