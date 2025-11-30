/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Filtering Module
 *
 * Implements filtering strategies for hint matching.
 * Uses Strategy pattern to support different filtering modes.
 */

#ifndef HINT_FILTER_H
#define HINT_FILTER_H

#include "hint_state.h"
#include "../platform.h"
#include <stddef.h>

/**
 * Filter and update matched hints based on current state
 *
 * Returns 1 if labels were regenerated (caller should reset num_filter), 0 otherwise
 *
 * @param state Hint state containing filters and hints
 * @return 1 if labels were regenerated, 0 otherwise
 */
int hint_filter_apply(hint_state_t *state);

/**
 * Fuzzy match: check if all characters in pattern appear in order in text
 *
 * Case-insensitive matching.
 *
 * @param text Text to search in
 * @param pattern Pattern to match
 * @return 1 if matches, 0 otherwise
 */
int hint_filter_fuzzy_match(const char *text, const char *pattern);

#endif /* HINT_FILTER_H */
