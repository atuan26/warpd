/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Linux UI Element Detector using AT-SPI with OpenCV fallback
 */

#include "../../platform.h"
#include "../../atspi-detector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <at-spi-2.0/atspi/atspi.h>
#include <glib-2.0/glib.h>

/* Forward declarations for OpenCV fallback (implemented separately) */
extern struct ui_detection_result *opencv_detect_ui_elements(void);
extern void opencv_free_ui_elements(struct ui_detection_result *result);
extern int opencv_is_available(void);

/**
 * Convert AT-SPI ElementInfo to platform ui_element
 */
static void convert_atspi_element(ElementInfo *src, struct ui_element *dest)
{
	dest->x = src->x;
	dest->y = src->y;
	dest->w = src->w;
	dest->h = src->h;
	dest->name = src->name ? strdup(src->name) : NULL;
	dest->role = src->role ? strdup(src->role) : NULL;
}

/**
 * Detect UI elements using AT-SPI
 */
static struct ui_detection_result *atspi_detect_ui_elements(void)
{
	struct ui_detection_result *result = calloc(1, sizeof(*result));
	if (!result)
		return NULL;

	/* Initialize AT-SPI */
	atspi_init_detector();

	/* Detect elements using AT-SPI */
	GSList *element_list = detect_elements();
	if (!element_list) {
		snprintf(result->error_msg, sizeof(result->error_msg),
		         "No active window or AT-SPI not available");
		result->error = -1;
		return result;
	}

	/* Convert GSList to array */
	size_t count = g_slist_length(element_list);
	if (count == 0) {
		result->error = -2;
		snprintf(result->error_msg, sizeof(result->error_msg),
		         "No interactive elements detected");
		free_detector_resources();
		return result;
	}

	result->elements = calloc(count, sizeof(struct ui_element));
	if (!result->elements) {
		result->error = -3;
		snprintf(result->error_msg, sizeof(result->error_msg),
		         "Memory allocation failed");
		free_detector_resources();
		return result;
	}

	/* Copy elements */
	GSList *iter = element_list;
	for (size_t i = 0; i < count && iter; i++, iter = iter->next) {
		ElementInfo *elem = (ElementInfo *)iter->data;
		if (elem) {
			convert_atspi_element(elem, &result->elements[i]);
		}
	}

	result->count = count;
	result->error = 0;

	/* Cleanup AT-SPI resources */
	free_detector_resources();

	return result;
}

/**
 * Detect UI elements with AT-SPI primary, OpenCV fallback
 */
struct ui_detection_result *linux_detect_ui_elements(void)
{
	/* Try AT-SPI first */
	struct ui_detection_result *result = atspi_detect_ui_elements();

	/* If AT-SPI failed and OpenCV is available, try OpenCV fallback */
	if (result && result->error != 0 && opencv_is_available()) {
		fprintf(stderr, "AT-SPI detection failed, trying OpenCV fallback...\n");
		free(result);
		result = opencv_detect_ui_elements();
	}

	return result;
}

/**
 * Free UI detection result
 */
void linux_free_ui_elements(struct ui_detection_result *result)
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

