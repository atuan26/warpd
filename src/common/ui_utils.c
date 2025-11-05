/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Common UI element utilities for all detectors
 */

#include "../platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Import config functions */
extern const char *config_get(const char *key);
extern int config_get_int(const char *key);

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
    
    /* Mark elements for removal */
    char *keep = calloc(result->count, sizeof(char));
    if (!keep) {
        return; /* Memory allocation failed */
    }
    
    /* Initially mark all elements to keep */
    for (size_t i = 0; i < result->count; i++) {
        keep[i] = 1;
    }
    
    /* Check each pair of elements */
    for (size_t i = 0; i < result->count; i++) {
        if (!keep[i]) continue;
        
        for (size_t j = i + 1; j < result->count; j++) {
            if (!keep[j]) continue;
            
            struct ui_element *elem_i = &result->elements[i];
            struct ui_element *elem_j = &result->elements[j];
            
            /* Calculate center points */
            int center_i_x = elem_i->x + elem_i->w / 2;
            int center_i_y = elem_i->y + elem_i->h / 2;
            int center_j_x = elem_j->x + elem_j->w / 2;
            int center_j_y = elem_j->y + elem_j->h / 2;
            
            /* Check distance threshold */
            double distance = calculate_distance(center_i_x, center_i_y, center_j_x, center_j_y);
            
            if (distance < distance_threshold) {
                /* Elements are too close, remove the smaller one */
                int area_i = elem_i->w * elem_i->h;
                int area_j = elem_j->w * elem_j->h;
                
                if (area_i < area_j) {
                    keep[i] = 0;
                    break; /* Element i is removed, no need to check further */
                } else {
                    keep[j] = 0;
                }
            } else {
                /* Check area overlap */
                double overlap = calculate_overlap_ratio(elem_i, elem_j);
                
                if (overlap > area_threshold) {
                    /* Significant overlap, remove the smaller element */
                    int area_i = elem_i->w * elem_i->h;
                    int area_j = elem_j->w * elem_j->h;
                    
                    if (area_i < area_j) {
                        keep[i] = 0;
                        break; /* Element i is removed, no need to check further */
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
            
            fprintf(stderr, "UI Utils: Removed %zu overlapping elements, %zu remaining (threshold: %dpx, area: %.1f)\n", 
                    result->count - new_count, new_count, distance_threshold, area_threshold);
        }
    }
    
    free(keep);
}