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
 * Returns:
 *  1 if labels were regenerated (caller should reset num_filter)
 *  0 if filter applied normally
 * -1 if filter was rejected (would result in 0 matches)
 *
 * @param state Hint state containing filters and hints
 * @return 1 if labels regenerated, 0 if applied normally, -1 if rejected
 */
int hint_filter_apply(hint_state_t *state);

/**
 * Fuzzy match: check if all characters in pattern appear in order in text
 *
 * Case-insensitive matching with Unicode normalization.
 *
 * @param text Text to search in
 * @param pattern Pattern to match
 * @return 1 if matches, 0 otherwise
 */
int hint_filter_fuzzy_match(const char *text, const char *pattern);

/**
 * Fuzzy match with scoring for ranking
 *
 * Calculates match quality based on:
 *   - Start position (earlier = better)
 *   - Contiguity (consecutive chars = better)  
 *   - Span (tighter match = better)
 *
 * Includes Unicode normalization for Vietnamese diacritics.
 *
 * Score formula: (start_pos × 100) + span + (contiguous ? 0 : 50)
 * Lower score = better match
 *
 * @param text Text to search in
 * @param pattern Pattern to match
 * @return -1 if no match, otherwise score (lower is better, 0 = perfect)
 */
int hint_filter_fuzzy_match_score(const char *text, const char *pattern);

#endif /* HINT_FILTER_H */
