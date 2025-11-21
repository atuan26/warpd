/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Common UI element utilities for all detectors
 */

#include "../platform.h"
#include "image_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Import config functions */
extern const char *config_get(const char *key);
extern int config_get_int(const char *key);

/* Import platform */
extern struct platform *platform;

/* Cached cursor images */
static struct cursor_image *target_cursor = NULL;
static struct cursor_image *hourglass_cursor = NULL;
static int images_load_attempted = 0;

/**
 * Load cursor images from config or default paths
 */
static void load_cursor_images(void)
{
	/* Only try loading once to avoid repeated error messages */
	if (images_load_attempted)
		return;
	
	images_load_attempted = 1;
	
	/* Get paths from config (empty string = don't use images) */
	const char *loading_path = config_get("cursor_image_loading");
	const char *target_path = config_get("cursor_image");
	
	/* Load loading cursor if path is specified */
	if (loading_path && loading_path[0] != '\0') {
		hourglass_cursor = load_cursor_image(loading_path);
	}
	
	if (target_path && target_path[0] != '\0') {
		target_cursor = load_cursor_image(target_path);
	}

}

/**
 * Draw loading cursor at the mouse position (for waiting/detecting)
 * Uses hourglass.png
 * 
 * @param scr Screen to display on
 * @param x Mouse X position
 * @param y Mouse Y position
 */
void draw_loading_cursor(screen_t scr, int x, int y)
{
	load_cursor_images();
	
	if (hourglass_cursor) {
		draw_cursor_image(scr, hourglass_cursor, x, y);
	}
}

/**
 * Draw target cursor at the mouse position (for normal mode)
 * Uses target.png
 * 
 * @param scr Screen to display on
 * @param x Mouse X position
 * @param y Mouse Y position
 */
void draw_target_cursor(screen_t scr, int x, int y)
{
	load_cursor_images();
	
	if (target_cursor) {
		draw_cursor_image(scr, target_cursor, x, y);
	}
}

/**
 * Show a centered message on screen
 * 
 * @param scr Screen to display on
 * @param message Message text to display
 * @param hint_h Height of the hint box
 */
void show_message(screen_t scr, const char *message, int hint_h)
{
	int screen_w, screen_h;
	platform->screen_get_dimensions(scr, &screen_w, &screen_h);
	
	struct hint msg_hint = {0};
	msg_hint.x = (screen_w - 250) / 2;  // Center horizontally
	msg_hint.y = 50;  // Near top
	msg_hint.w = 250;
	msg_hint.h = hint_h;
	snprintf(msg_hint.label, sizeof(msg_hint.label), "%s", message);
	
	platform->screen_clear(scr);
	platform->hint_draw(scr, &msg_hint, 1);
	platform->commit();
}

/**
 * Calculate distance between two points
 */
static double calculate_distance(int x1, int y1, int x2, int y2)
{
    int dx = x2 - x1;
    int dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

/**
 * Calculate area overlap ratio between two rectangles
 */
static double calculate_overlap_ratio(struct ui_element *a, struct ui_element *b)
{
    int left = (a->x > b->x) ? a->x : b->x;
    int top = (a->y > b->y) ? a->y : b->y;
    int right = ((a->x + a->w) < (b->x + b->w)) ? (a->x + a->w) : (b->x + b->w);
    int bottom = ((a->y + a->h) < (b->y + b->h)) ? (a->y + a->h) : (b->y + b->h);
    
    if (left >= right || top >= bottom) {
        return 0.0; /* No overlap */
    }
    
    int overlap_area = (right - left) * (bottom - top);
    int area_a = a->w * a->h;
    int area_b = b->w * b->h;
    int smaller_area = (area_a < area_b) ? area_a : area_b;
    
    if (smaller_area <= 0) {
        return 0.0;
    }
    
    return (double)overlap_area / smaller_area;
}

/**
 * Remove overlapping UI elements based on configurable thresholds
 * This function modifies the result in-place, removing overlapping elements
 * Uses hint size for overlap detection instead of actual element size
 */
void remove_overlapping_elements(struct ui_detection_result *result)
{
    if (!result || !result->elements || result->count <= 1) {
        return;
    }

    /* Get configuration values */
    int distance_threshold = 30;  /* Default */
    double area_threshold = 0.7;  /* Default */

    distance_threshold = config_get_int("ui_overlap_threshold");

    const char *area_str = config_get("ui_overlap_area_threshold");
    if (area_str) {
        area_threshold = atof(area_str);
    }

    /* Get hint size for overlap detection */
    int hint_w = 20;  /* Default hint size */
    int hint_h = 20;  /* Default hint size */

    /* Try to get actual hint size from config */
    int hint_size = config_get_int("hint_size");
    if (hint_size > 0) {
        hint_w = hint_size;
        hint_h = hint_size;
    }

    /* Mark elements for removal */
    char *keep = calloc(result->count, sizeof(char));
    if (!keep) {
        return; /* Memory allocation failed */
    }

    /* Initially mark all elements to keep */
    for (size_t i = 0; i < result->count; i++) {
        keep[i] = 1;
    }

    /* Check each pair of elements for hint overlap */
    for (size_t i = 0; i < result->count; i++) {
        if (!keep[i]) continue;

        for (size_t j = i + 1; j < result->count; j++) {
            if (!keep[j]) continue;

            struct ui_element *elem_i = &result->elements[i];
            struct ui_element *elem_j = &result->elements[j];

            /* Calculate hint positions (top-left corner of element) */
            int hint_i_x = elem_i->x;
            int hint_i_y = elem_i->y;
            int hint_j_x = elem_j->x;
            int hint_j_y = elem_j->y;

            /* Check distance between hint positions */
            double distance = calculate_distance(hint_i_x, hint_i_y, hint_j_x, hint_j_y);

            if (distance < distance_threshold) {
                /* Hints are too close, remove one */
                int area_i = elem_i->w * elem_i->h;
                int area_j = elem_j->w * elem_j->h;

                if (area_i < area_j) {
                    keep[i] = 0;
                } else {
                    keep[j] = 0;
                }
            } else {
                /* Check hint overlap areas */
                struct ui_element hint_i = {hint_i_x, hint_i_y, hint_w, hint_h, NULL, NULL};
                struct ui_element hint_j = {hint_j_x, hint_j_y, hint_w, hint_h, NULL, NULL};

                double overlap = calculate_overlap_ratio(&hint_i, &hint_j);

                if (overlap > area_threshold) {
                    /* Hints overlap significantly, remove one */
                    int area_i = elem_i->w * elem_i->h;
                    int area_j = elem_j->w * elem_j->h;

                    if (area_i < area_j) {
                        keep[i] = 0;
                    } else {
                        keep[j] = 0;
                    }
                }
            }
        }
    }
    
    /* Count remaining elements */
    size_t new_count = 0;
    for (size_t i = 0; i < result->count; i++) {
        if (keep[i]) {
            new_count++;
        }
    }
    
    /* Create new array with only kept elements */
    if (new_count < result->count) {
        struct ui_element *new_elements = calloc(new_count, sizeof(struct ui_element));
        if (new_elements) {
            size_t new_index = 0;
            for (size_t i = 0; i < result->count; i++) {
                if (keep[i]) {
                    new_elements[new_index] = result->elements[i];
                    new_index++;
                } else {
                    /* Free memory for removed elements */
                    if (result->elements[i].name) {
                        free(result->elements[i].name);
                    }
                    if (result->elements[i].role) {
                        free(result->elements[i].role);
                    }
                }
            }
            
            free(result->elements);
            result->elements = new_elements;
            result->count = new_count;
        }
    }
    
    free(keep);
}