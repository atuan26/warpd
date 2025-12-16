/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Filtering Module Implementation
 */

#include "hint_filter.h"
#include "hint_sorter.h"
#include "hint_label_generator.h"
#include <stdio.h>
#include <string.h>

int hint_filter_fuzzy_match(const char *text, const char *pattern)
{
	if (!text || !pattern) {
		return 0;
	}

	const char *t = text;
	const char *p = pattern;

	while (*p) {
		/* Convert pattern char to lowercase */
		char pc = *p;
		if (pc >= 'A' && pc <= 'Z') {
			pc = pc - 'A' + 'a';
		}

		/* Search for pattern char in remaining text */
		int found = 0;
		while (*t) {
			char tc = *t;
			if (tc >= 'A' && tc <= 'Z') {
				tc = tc - 'A' + 'a';
			}

			t++;
			if (tc == pc) {
				found = 1;
				break;
			}
		}

		if (!found) {
			return 0;
		}

		p++;
	}

	return 1;
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

	/* Temporary array to store new matches */
	struct hint temp_matched[MAX_HINTS];
	size_t temp_count = 0;

	/* Filter hints */
	for (size_t i = 0; i < source_count; i++) {
		if (hint_matches_filters(&source[i], num_filter, text_filter,
		                          state->mode, state->is_opencv_result)) {
			temp_matched[temp_count] = source[i];
			temp_matched[temp_count].highlighted = 0;
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

	/* Copy temp results to state->matched */
	state->nr_matched = temp_count;
	for (size_t i = 0; i < temp_count; i++) {
		state->matched[i] = temp_matched[i];
	}

	if (is_numeric && (num_filter[0] || text_filter[0])) {
		fprintf(stderr, "DEBUG: Matched %zu hints\n", state->nr_matched);
	}

	/* In numeric mode with TEXT filtering active, sort by distance from center and reassign labels */
	/* Do NOT reassign labels when using numeric filtering - user wants to keep original numbers */
	int regenerated = 0;
	if (is_numeric && state->nr_matched > 0 && text_filter[0] && !num_filter[0]) {
		/* Sort matched hints by distance from screen center */
		hint_sorter_sort_by_center(state->matched, state->nr_matched, state->screen);

		/* Reassign numeric labels based on sorted order */
		hint_label_generate_numeric(state->matched, state->nr_matched);

		fprintf(stderr, "DEBUG: Reassigned labels based on distance from center\n");
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
