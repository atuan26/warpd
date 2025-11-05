/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Windows UI Element Detector using UI Automation
 */

#include "../../platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Forward declarations for UI Automation functions (implemented in C++) */
extern struct ui_detection_result *uiautomation_detect_ui_elements(void);
extern void uiautomation_free_ui_elements(struct ui_detection_result *result);
extern int uiautomation_is_available(void);
extern void uiautomation_cleanup(void);

/* Forward declarations for OpenCV functions (implemented in C++) */
extern struct ui_detection_result *opencv_detect_ui_elements(void);
extern void opencv_free_ui_elements(struct ui_detection_result *result);
extern int opencv_is_available(void);

/**
 * Detect UI elements using Windows UI Automation with OpenCV fallback
 */
struct ui_detection_result *windows_detect_ui_elements(void)
{
    struct ui_detection_result *result = NULL;
    
    // Try UI Automation first
    if (uiautomation_is_available()) {
        result = uiautomation_detect_ui_elements();
        
        // Check if UI Automation found enough elements
        if (result && result->error == 0 && result->count >= 3) {
            fprintf(stderr, "Windows: UI Automation found %zu elements\n", result->count);
            /* Apply common overlap removal */
            remove_overlapping_elements(result);
            return result;
        }
        
        // UI Automation failed or found too few elements
        if (result) {
            fprintf(stderr, "Windows: UI Automation found only %zu elements (error: %d)\n", 
                    result->count, result->error);
            uiautomation_free_ui_elements(result);
            result = NULL;
        }
    }

    // Fallback to OpenCV if UI Automation failed
    if (opencv_is_available()) {
        fprintf(stderr, "Windows: Falling back to OpenCV detection\n");
        result = opencv_detect_ui_elements();
        
        if (result && result->error == 0) {
            fprintf(stderr, "Windows: OpenCV found %zu elements\n", result->count);
            /* Apply common overlap removal */
            remove_overlapping_elements(result);
            return result;
        }
        
        if (result) {
            fprintf(stderr, "Windows: OpenCV detection failed (error: %d)\n", result->error);
            opencv_free_ui_elements(result);
            result = NULL;
        }
    }

    // Both methods failed
    result = calloc(1, sizeof(*result));
    if (result) {
        result->error = -1;
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Both UI Automation and OpenCV detection failed");
    }
    return result;
}

/**
 * Free UI detection result
 */
void windows_free_ui_elements(struct ui_detection_result *result)
{
    if (!result)
        return;

    // Try to determine which detector was used and free accordingly
    // Since we don't have a way to track this, we'll use a safe approach
    
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

    // Default to UI Automation cleanup
    uiautomation_free_ui_elements(result);
}