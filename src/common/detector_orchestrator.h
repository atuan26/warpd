/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Common Detector Orchestrator
 *
 * Provides a unified Strategy Pattern for UI element detection with fallback.
 * Reduces code duplication across platform-specific implementations (Linux, Windows, macOS).
 *
 * Design Pattern: Strategy + Chain of Responsibility
 * - Each detector is a strategy (AT-SPI, UI Automation, OpenCV)
 * - Orchestrator chains them with fallback logic
 */

#ifndef DETECTOR_ORCHESTRATOR_H
#define DETECTOR_ORCHESTRATOR_H

#include "../platform.h"

/**
 * Detector strategy function signature
 */
typedef struct ui_detection_result* (*detector_fn)(void);
typedef int (*detector_available_fn)(void);
typedef void (*detector_free_fn)(struct ui_detection_result *result);

/**
 * Single detector strategy
 */
typedef struct {
	const char *name;              /* Human-readable name (e.g., "AT-SPI", "OpenCV") */
	detector_available_fn is_available;  /* Check if detector is available */
	detector_fn detect;            /* Detect function */
	detector_free_fn free_result;  /* Free result function */
	int min_elements;              /* Minimum elements threshold (0 = any) */
} detector_strategy_t;

/**
 * Run detection through chain of strategies
 *
 * Tries each detector in order until one succeeds.
 * If all detectors fail, returns error result.
 *
 * @param strategies Array of detector strategies
 * @param count Number of strategies
 * @param platform_name Name of platform (for debug output)
 * @return Detection result (always non-NULL, check result->error)
 */
struct ui_detection_result* detector_orchestrator_run(
	detector_strategy_t *strategies,
	size_t count,
	const char *platform_name
);

/**
 * Free detection result with proper cleanup
 *
 * @param result Result to free
 * @param free_fn Appropriate free function for the result
 */
void detector_orchestrator_free(
	struct ui_detection_result *result,
	detector_free_fn free_fn
);

#endif /* DETECTOR_ORCHESTRATOR_H */
