/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Windows UI Element Detector using UI Automation with OpenCV fallback
 *
 * REFACTORED: Uses common detector orchestrator to reduce duplication
 */

#include "../../platform.h"
#include "../../common/detector_orchestrator.h"
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
#include "../../common/opencv_detector.h"

/**
 * Detect UI elements using Windows UI Automation with OpenCV fallback
 */
struct ui_detection_result *windows_detect_ui_elements(void)
{
	/* Define detection strategies in order of preference */
	detector_strategy_t strategies[] = {
		{
			.name = "UI Automation",
			.is_available = uiautomation_is_available,
			.detect = uiautomation_detect_ui_elements,
			.free_result = uiautomation_free_ui_elements,
			.min_elements = 3,  /* UI Automation should find at least 3 elements */
		},
		{
			.name = "OpenCV",
			.is_available = opencv_is_available,
			.detect = opencv_detect_ui_elements,
			.free_result = opencv_free_ui_elements,
			.min_elements = 0,  /* Accept any number of elements from OpenCV */
		},
	};

	/* Run detection through strategy chain */
	return detector_orchestrator_run(strategies, 2, "Windows");
}

/**
 * Free UI detection result
 */
void windows_free_ui_elements(struct ui_detection_result *result)
{
	if (!result)
		return;

	if (result->elements) {
		for (size_t i = 0; i < result->count; i++) {
			if (result->elements[i].name)
				free(result->elements[i].name);
			if (result->elements[i].role)
				free(result->elements[i].role);
		}
		free(result->elements);
	}

	free(result);
}
