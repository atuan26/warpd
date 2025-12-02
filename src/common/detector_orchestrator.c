/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Common Detector Orchestrator Implementation
 *
 * Implements Strategy + Chain of Responsibility pattern for UI detection.
 */

#include "detector_orchestrator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ui_detection_result* detector_orchestrator_run(
	detector_strategy_t *strategies,
	size_t count,
	const char *platform_name)
{
	if (!strategies || count == 0) {
		struct ui_detection_result *err = calloc(1, sizeof(*err));
		if (err) {
			err->error = -1;
			snprintf(err->error_msg, sizeof(err->error_msg), "No detection strategies available");
		}
		return err;
	}

	/* Try each detector strategy in order */
	for (size_t i = 0; i < count; i++) {
		detector_strategy_t *strategy = &strategies[i];

		/* Check if detector is available */
		if (!strategy->is_available) {
			fprintf(stderr, "%s: %s not available (no availability check)\n",
				platform_name, strategy->name);
			continue;
		}

		if (!strategy->is_available()) {
			fprintf(stderr, "%s: %s not available\n", platform_name, strategy->name);
			continue;
		}

		fprintf(stderr, "%s: Trying %s detection\n", platform_name, strategy->name);

		/* Run detection */
		struct ui_detection_result *result = strategy->detect();
		if (!result) {
			fprintf(stderr, "%s: %s detection returned NULL\n", platform_name, strategy->name);
			continue;
		}

		/* Check for success */
		if (result->error == 0) {
			size_t count_found = result->count;

			/* Check minimum elements threshold */
			if (strategy->min_elements > 0 && count_found < (size_t)strategy->min_elements) {
				fprintf(stderr, "%s: %s found only %zu elements (minimum: %d), trying next...\n",
					platform_name, strategy->name, count_found, strategy->min_elements);

				/* Free result and try next strategy */
				if (strategy->free_result) {
					strategy->free_result(result);
				} else {
					free(result);
				}
				continue;
			}

			fprintf(stderr, "%s: %s found %zu elements\n",
				platform_name, strategy->name, count_found);

			/* Apply common overlap removal if detection succeeded */
			remove_overlapping_elements(result);

			return result;
		}

		/* Detection failed */
		fprintf(stderr, "%s: %s detection failed (error: %d, %s)\n",
			platform_name, strategy->name, result->error, result->error_msg);

		/* Free result and try next strategy */
		if (strategy->free_result) {
			strategy->free_result(result);
		} else {
			free(result);
		}
	}

	/* All detectors failed */
	struct ui_detection_result *error_result = calloc(1, sizeof(*error_result));
	if (error_result) {
		error_result->error = -1;
		snprintf(error_result->error_msg, sizeof(error_result->error_msg),
			"All detection strategies failed");
	}

	return error_result;
}

void detector_orchestrator_free(
	struct ui_detection_result *result,
	detector_free_fn free_fn)
{
	if (!result) {
		return;
	}

	/* Use provided free function if available */
	if (free_fn) {
		free_fn(result);
	} else {
		/* Fallback generic free */
		if (result->elements) {
			for (size_t i = 0; i < result->count; i++) {
				if (result->elements[i].name) {
					free(result->elements[i].name);
				}
				if (result->elements[i].role) {
					free(result->elements[i].role);
				}
			}
			free(result->elements);
		}
		free(result);
	}
}
