/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Windows UI Element Detector using UI Automation
 */

#include "../../platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations for UI Automation functions (implemented in C++) */
extern struct ui_detection_result *uiautomation_detect_ui_elements(void);
extern void uiautomation_free_ui_elements(struct ui_detection_result *result);
extern int uiautomation_is_available(void);

/**
 * Detect UI elements using Windows UI Automation
 */
struct ui_detection_result *windows_detect_ui_elements(void)
{
    if (!uiautomation_is_available()) {
        struct ui_detection_result *result = calloc(1, sizeof(*result));
        if (result) {
            result->error = -1;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "UI Automation not available");
        }
        return result;
    }

    return uiautomation_detect_ui_elements();
}

/**
 * Free UI detection result
 */
void windows_free_ui_elements(struct ui_detection_result *result)
{
    uiautomation_free_ui_elements(result);
}