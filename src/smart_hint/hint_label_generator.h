/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Label Generation Module
 *
 * Generates labels for hints using different strategies:
 * - Alphabetic: A, B, C, ... Z, AA, AB, ...
 * - Numeric: 1, 2, 3, ... (zero-padded based on total count)
 */

#ifndef HINT_LABEL_GENERATOR_H
#define HINT_LABEL_GENERATOR_H

#include "../platform.h"
#include <stddef.h>

/**
 * Generate alphabetic labels for hints (A, B, C, ... Z, AA, AB, ...)
 *
 * @param hints Array of hints to label
 * @param count Number of hints
 */
void hint_label_generate_alphabetic(struct hint *hints, size_t count);

/**
 * Generate numeric labels for hints with equal length based on total count
 *
 * Examples:
 *   1-9 hints: "1", "2", ... "9"
 *   10-99 hints: "01", "02", ... "99"
 *   100-999 hints: "001", "002", ... "999"
 *
 * @param hints Array of hints to label
 * @param count Number of hints
 */
void hint_label_generate_numeric(struct hint *hints, size_t count);

#endif /* HINT_LABEL_GENERATOR_H */
