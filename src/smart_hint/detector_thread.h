/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Detector Thread Module
 *
 * Cross-platform threading abstraction for UI element detection.
 * Uses Adapter pattern to hide Windows vs POSIX differences.
 */

#ifndef DETECTOR_THREAD_H
#define DETECTOR_THREAD_H

#include "../platform.h"

/* Opaque detector thread handle */
typedef struct detector_thread detector_thread_t;

/**
 * Create a new detector thread
 *
 * @return New thread handle, or NULL on failure
 */
detector_thread_t* detector_thread_create(void);

/**
 * Start UI detection in background thread
 *
 * @param thread Thread handle
 * @return 0 on success, -1 on failure
 */
int detector_thread_start(detector_thread_t *thread);

/**
 * Check if detection is complete
 *
 * Thread-safe check without blocking.
 *
 * @param thread Thread handle
 * @return 1 if done, 0 if still running
 */
int detector_thread_is_done(detector_thread_t *thread);

/**
 * Wait for detection to complete and get result
 *
 * Blocks until thread finishes. Thread handle is destroyed after this call.
 *
 * @param thread Thread handle (will be freed)
 * @return Detection result, or NULL on error
 */
struct ui_detection_result* detector_thread_join(detector_thread_t *thread);

/**
 * Destroy detector thread without waiting
 *
 * Only call if you haven't called detector_thread_join yet.
 *
 * @param thread Thread handle to destroy
 */
void detector_thread_destroy(detector_thread_t *thread);

#endif /* DETECTOR_THREAD_H */
