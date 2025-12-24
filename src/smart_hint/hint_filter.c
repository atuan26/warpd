/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Filtering Module Implementation
 */

#include "hint_filter.h"
#include "hint_sorter.h"
#include "hint_label_generator.h"
#include "../common/unicode_normalize.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* External platform interface */
extern struct platform *platform;

#define NORMALIZE_BUFFER_SIZE 512

/**
 * Fuzzy match with scoring - core implementation
 */
int hint_filter_fuzzy_match_score(const char *text, const char *pattern)
{
	if (!text || !pattern) {
		return -1;
	}
	
	if (!pattern[0]) {
		return 0;
	}
	
	static char normalized_text[NORMALIZE_BUFFER_SIZE];
	static char normalized_pattern[NORMALIZE_BUFFER_SIZE];
	
	unicode_normalize(text, normalized_text, sizeof(normalized_text));
	unicode_normalize(pattern, normalized_pattern, sizeof(normalized_pattern));
	
	const char *t = normalized_text;
	const char *p = normalized_pattern;
	
	int start_pos = -1;
	int last_pos = -1;
	int prev_pos = -1;
	int contiguous = 1;
	
	/* Find all pattern characters in order */
	while (*p) {
		/* Convert to lowercase for case-insensitive matching */
		char pc = tolower((unsigned char)*p);
		int found = 0;
		
		while (*t) {
			char tc = tolower((unsigned char)*t);
			int current_pos = (int)(t - normalized_text);
			t++;
			
			if (tc == pc) {
				/* First match sets the start position */
				if (start_pos == -1) {
					start_pos = current_pos;
				} else if (current_pos != prev_pos + 1) {
					/* Gap detected - not contiguous */
					contiguous = 0;
				}
				
				prev_pos = current_pos;
				last_pos = current_pos;
				found = 1;
				break;
			}
		}
		
		if (!found) {
			return -1;  /* Pattern not found in text */
		}
		
		p++;
	}
	
	/* Calculate score: lower is better */
	int span = last_pos - start_pos;
	int contiguity_penalty = contiguous ? 0 : 50;
	int score = (start_pos * 100) + span + contiguity_penalty;
	
	return score;
}

/**
 * Simple boolean fuzzy match (backward compatibility)
 * Wraps the scoring function for easier migration
 */
int hint_filter_fuzzy_match(const char *text, const char *pattern)
{
	return hint_filter_fuzzy_match_score(text, pattern) >= 0 ? 1 : 0;
}

/**
 * Check if a single hint matches the current filters
 */
static int hint_matches_filters(const struct hint *hint,
                                  const char *num_filter,
                                  const char *text_filter,
                                  hint_mode_type_t mode,
                                  int is_opencv)
{
	int matches = 1;

	if (mode == HINT_MODE_NUMERIC) {
		/* Numeric filter: prefix match on label */
		if (num_filter && num_filter[0]) {
			if (strncmp(hint->label, num_filter, strlen(num_filter)) != 0) {
				matches = 0;
			}
		}

		/* Text filter: fuzzy match on element name (only if not OpenCV) */
		if (matches && text_filter && text_filter[0] && !is_opencv) {
			int fuzzy_result = hint->element_name ?
			                   hint_filter_fuzzy_match(hint->element_name, text_filter) : 0;
			if (!fuzzy_result) {
				matches = 0;
			}
		}
	} else {
		/* Alphabetic mode: case-insensitive prefix match on label */
		if (num_filter && num_filter[0]) {
			if (strncasecmp(hint->label, num_filter, strlen(num_filter)) != 0) {
				matches = 0;
			}
		}
	}

	return matches;
}

int hint_filter_apply(hint_state_t *state)
{
	if (!state) {
		return 0;
	}

	const char *num_filter = state->num_filter;
	const char *text_filter = state->text_filter;
	int is_numeric = (state->mode == HINT_MODE_NUMERIC);

	/* Debug output for numeric mode with filters */
	if (is_numeric && (num_filter[0] || text_filter[0])) {
		fprintf(stderr, "DEBUG: Filtering - num='%s' text='%s'%s\n",
		        num_filter, text_filter,
		        state->is_opencv_result ? " (OpenCV - text filter ignored)" : "");
	}

	/* Save previous filter state for potential rollback */
	static char prev_num_filter[HINT_MAX_NUM_FILTER] = {0};
	static char prev_text_filter[HINT_MAX_TEXT_FILTER] = {0};
	static int prev_labels_regenerated = 0;

	/* Determine source array for filtering */
	/* If labels were regenerated, filter from matched[] (which has new labels) */
	/* Otherwise, filter from original hints[] */
	struct hint *source = state->labels_regenerated ? state->matched : state->hints;
	size_t source_count = state->labels_regenerated ? state->nr_matched : state->nr_hints;

	/* Temporary arrays to store new matches and their scores */
	struct hint temp_matched[MAX_HINTS];
	int temp_scores[MAX_HINTS];  /* Fuzzy match scores for sorting */
	size_t temp_count = 0;

	/* Filter hints and calculate scores */
	for (size_t i = 0; i < source_count; i++) {
		if (hint_matches_filters(&source[i], num_filter, text_filter,
		                          state->mode, state->is_opencv_result)) {
			temp_matched[temp_count] = source[i];
			temp_matched[temp_count].highlighted = 0;
			
			/* Calculate fuzzy match score for text filtering (used for sorting) */
			if (is_numeric && text_filter[0] && source[i].element_name) {
				temp_scores[temp_count] = hint_filter_fuzzy_match_score(
					source[i].element_name, text_filter);
			} else {
				temp_scores[temp_count] = 0;  /* No text filter or no element name */
			}
			
			temp_count++;
		}
	}

	/* If filtering results in 0 matches and we had matches before, revert to previous filter state */
	if (temp_count == 0 && state->nr_matched > 0) {
		fprintf(stderr, "DEBUG: Filter would result in 0 matches, reverting to previous filter\n");
		strncpy(state->num_filter, prev_num_filter, HINT_MAX_NUM_FILTER);
		strncpy(state->text_filter, prev_text_filter, HINT_MAX_TEXT_FILTER);
		state->labels_regenerated = prev_labels_regenerated;
		return -1; /* Indicate rejection */
	}

	/* Save current filter state for next time */
	strncpy(prev_num_filter, state->num_filter, HINT_MAX_NUM_FILTER);
	strncpy(prev_text_filter, state->text_filter, HINT_MAX_TEXT_FILTER);
	prev_labels_regenerated = state->labels_regenerated;

	/* Copy temp results to state->matched (including scores) */
	state->nr_matched = temp_count;
	for (size_t i = 0; i < temp_count; i++) {
		state->matched[i] = temp_matched[i];
	}

	if (is_numeric && (num_filter[0] || text_filter[0])) {
		fprintf(stderr, "DEBUG: Matched %zu hints\n", state->nr_matched);
	}

	/* In numeric mode with TEXT filtering active, sort by fuzzy match score then distance */
	/* Do NOT reassign labels when using numeric filtering - user wants to keep original numbers */
	int regenerated = 0;
	if (is_numeric && state->nr_matched > 0 && text_filter[0] && !num_filter[0]) {
		/* Sort by fuzzy match score (lower = better), then by distance from center */
		/* Simple bubble sort - acceptable for small arrays (typically < 100 hints) */
		/* Performance: O(n^2) worst case, but n is usually < 50 */
		
		/* Pre-calculate screen center (outside loop for performance) */
		int screen_w, screen_h;
		platform->screen_get_dimensions(state->screen, &screen_w, &screen_h);
		int cx = screen_w / 2;
		int cy = screen_h / 2;
		
		for (size_t i = 0; i < state->nr_matched - 1; i++) {
			for (size_t j = 0; j < state->nr_matched - i - 1; j++) {
				/* Compare scores first */
				int score_a = temp_scores[j];
				int score_b = temp_scores[j + 1];
				
				int should_swap = 0;
				if (score_a > score_b) {
					should_swap = 1;
				} else if (score_a == score_b) {
					/* Tie-breaker: distance from screen center */
					int dist_a = (state->matched[j].x - cx) * (state->matched[j].x - cx) +
					             (state->matched[j].y - cy) * (state->matched[j].y - cy);
					int dist_b = (state->matched[j + 1].x - cx) * (state->matched[j + 1].x - cx) +
					             (state->matched[j + 1].y - cy) * (state->matched[j + 1].y - cy);
					
					if (dist_a > dist_b) {
						should_swap = 1;
					}
				}
				
				if (should_swap) {
					/* Swap hints */
					struct hint temp_hint = state->matched[j];
					state->matched[j] = state->matched[j + 1];
					state->matched[j + 1] = temp_hint;
					
					/* Swap scores */
					int temp_score = temp_scores[j];
					temp_scores[j] = temp_scores[j + 1];
					temp_scores[j + 1] = temp_score;
				}
			}
		}

		/* Reassign numeric labels based on sorted order */
		hint_label_generate_numeric(state->matched, state->nr_matched);

		fprintf(stderr, "DEBUG: Sorted by fuzzy match score, then reassigned labels\n");
		state->labels_regenerated = 1;
		regenerated = 1;
	}

	/* Update highlighted index */
	if (state->nr_matched > 0) {
		state->highlighted_index = 0;
		state->matched[0].highlighted = 1;
	} else {
		state->highlighted_index = 0;
	}

	return regenerated;
}
