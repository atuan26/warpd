/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - State Management Module Implementation
 */

#include "hint_state.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

hint_state_t* hint_state_create(struct hint *hints, size_t count,
                                 hint_mode_type_t mode, int is_opencv,
                                 screen_t scr)
{
	if (!hints || count == 0 || count > MAX_HINTS) {
		return NULL;
	}

	hint_state_t *state = calloc(1, sizeof(hint_state_t));
	if (!state) {
		return NULL;
	}

	state->hints = hints;
	state->nr_hints = count;
	state->nr_matched = 0;
	state->highlighted_index = 0;
	state->mode = mode;
	state->is_opencv_result = is_opencv;
	state->labels_regenerated = 0;
	state->screen = scr;

	/* Initialize filters as empty */
	state->num_filter[0] = '\0';
	state->text_filter[0] = '\0';

	return state;
}

void hint_state_destroy(hint_state_t *state)
{
	if (state) {
		/* NOTE: We don't free state->hints because we don't own it */
		free(state);
	}
}

void hint_state_reset_filters(hint_state_t *state)
{
	if (!state) {
		return;
	}

	state->num_filter[0] = '\0';
	state->text_filter[0] = '\0';
	state->labels_regenerated = 0;
}

void hint_state_reset_num_filter(hint_state_t *state)
{
	if (!state) {
		return;
	}

	state->num_filter[0] = '\0';
}

int hint_state_append_filter(hint_state_t *state, char c, int is_letter)
{
	if (!state) {
		return 0;
	}

	if (is_letter) {
		if (state->is_opencv_result) {
			fprintf(stderr, "DEBUG: Ignoring letter input '%c' - OpenCV detection cannot filter by text\n", c);
			return 0;
		}

		/* Append to text filter */
		size_t len = strlen(state->text_filter);
		if (len >= HINT_MAX_TEXT_FILTER - 1) {
			return 0; /* Buffer full */
		}
		state->text_filter[len] = c;
		state->text_filter[len + 1] = '\0';
	} else {
		/* Append to numeric filter */
		size_t len = strlen(state->num_filter);
		if (len >= HINT_MAX_NUM_FILTER - 1) {
			return 0; /* Buffer full */
		}
		state->num_filter[len] = c;
		state->num_filter[len + 1] = '\0';
	}

	return 1;
}

int hint_state_undo_filter(hint_state_t *state)
{
	if (!state) {
		return 0;
	}

	size_t text_len = strlen(state->text_filter);
	size_t num_len = strlen(state->num_filter);

	/* Priority: text filter first, then numeric filter */
	if (text_len > 0) {
		state->text_filter[text_len - 1] = '\0';
		return 1;
	} else if (num_len > 0) {
		state->num_filter[num_len - 1] = '\0';
		return 1;
	}

	return 0; /* Both buffers empty */
}

struct hint* hint_state_get_highlighted(hint_state_t *state)
{
	if (!state || state->nr_matched == 0) {
		return NULL;
	}

	if (state->highlighted_index < 0 ||
	    (size_t)state->highlighted_index >= state->nr_matched) {
		return NULL;
	}

	return &state->matched[state->highlighted_index];
}

int hint_state_has_filters(hint_state_t *state)
{
	if (!state) {
		return 0;
	}

	return state->num_filter[0] != '\0' || state->text_filter[0] != '\0';
}
