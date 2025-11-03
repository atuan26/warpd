/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * macOS UI Element Detector using Accessibility API with OpenCV fallback
 */

#include "../../platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Forward declarations for Accessibility API functions (implemented in Objective-C) */
extern struct ui_detection_result *accessibility_detect_ui_elements(void);
extern void accessibility_free_ui_elements(struct ui_detection_result *result);
extern int accessibility_is_available(void);

/* Forward declarations for OpenCV functions (implemented in C++) */
extern struct ui_detection_result *opencv_detect_ui_elements(void);
extern void opencv_free_ui_elements(struct ui_detection_result *result);
extern int opencv_is_available(void);

/**
 * Detect UI elements using macOS Accessibility API with OpenCV fallback
 */
struct ui_detection_result *macos_detect_ui_elements(void)
{
    struct ui_detection_result *result = NULL;
    
    // Try Accessibility API first
    if (accessibility_is_available()) {
        result = accessibility_detect_ui_elements();
        
        // Check if Accessibility API found enough elements
        if (result && result->error == 0 && result->count >= 3) {
            fprintf(stderr, "macOS: Accessibility API found %zu elements\n", result->count);
            return result;
        }
        
        // Accessibility API failed or found too few elements
        if (result) {
            fprintf(stderr, "macOS: Accessibility API found only %zu elements (error: %d)\n", 
                    result->count, result->error);
            accessibility_free_ui_elements(result);
            result = NULL;
        }
    }

    // Fallback to OpenCV if Accessibility API failed
    if (opencv_is_available()) {
        fprintf(stderr, "macOS: Falling back to OpenCV detection\n");
        result = opencv_detect_ui_elements();
        
        if (result && result->error == 0) {
            fprintf(stderr, "macOS: OpenCV found %zu elements\n", result->count);
            return result;
        }
        
        if (result) {
            fprintf(stderr, "macOS: OpenCV detection failed (error: %d)\n", result->error);
            opencv_free_ui_elements(result);
            result = NULL;
        }
    }

    // Both methods failed
    result = calloc(1, sizeof(*result));
    if (result) {
        result->error = -1;
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Both Accessibility API and OpenCV detection failed");
    }
    return result;
}

/**
 * Free UI detection result
 */
void macos_free_ui_elements(struct ui_detection_result *result)
{
    if (!result)
        return;

    // Try to determine which detector was used and free accordingly
    // Check if this looks like an OpenCV result (no names, generic roles)
    if (result->elements && result->count > 0) {
        bool looks_like_opencv = true;
        for (size_t i = 0; i < result->count; i++) {
            if (result->elements[i].name != NULL) {
                looks_like_opencv = false;
                break;
            }
        }
        
        if (looks_like_opencv) {
            opencv_free_ui_elements(result);
            return;
        }
    }

    // Default to Accessibility API cleanup
    accessibility_free_ui_elements(result);
}