/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - State Management Module
 *
 * Manages the state of hint filtering and selection.
 * This module encapsulates all mutable state previously scattered
 * across global variables in smart_hint.c
 */

#ifndef HINT_STATE_H
#define HINT_STATE_H

#include "../platform.h"
#include <stddef.h>

#define HINT_MAX_NUM_FILTER 32
#define HINT_MAX_TEXT_FILTER 64

/**
 * Hint mode type
 */
typedef enum {
	HINT_MODE_NUMERIC,   /* Numeric labels with text filtering */
	HINT_MODE_ALPHABETIC /* Alphabetic labels */
} hint_mode_type_t;

/**
 * Central state for hint selection session
 *
 * Encapsulates all mutable state needed during hint filtering and selection.
 * Replaces global variables: hints, matched, nr_hints, nr_matched,
 * highlighted_index, is_opencv_result, labels_were_regenerated
 */
typedef struct {
	/* Hint arrays */
	struct hint *hints;          /* Original hints from detection */
	struct hint matched[MAX_HINTS]; /* Currently matched hints */
	size_t nr_hints;             /* Total number of original hints */
	size_t nr_matched;           /* Number of matched hints */

	/* Selection state */
	int highlighted_index;       /* Index of highlighted hint in matched[] */

	/* Filter buffers */
	char num_filter[HINT_MAX_NUM_FILTER];   /* Numeric filter input */
	char text_filter[HINT_MAX_TEXT_FILTER]; /* Text filter input */

	/* Mode flags */
	hint_mode_type_t mode;       /* Numeric or alphabetic mode */
	int is_opencv_result;        /* True if detection was OpenCV-based */
	int labels_regenerated;      /* True if labels were reassigned */

	/* Screen reference for sorting */
	screen_t screen;             /* Current screen for center-based sorting */
} hint_state_t;

/**
 * Create new hint state
 *
 * @param hints Array of hints (ownership NOT transferred - caller keeps ownership)
 * @param count Number of hints
 * @param mode Hint mode (numeric or alphabetic)
 * @param is_opencv True if detection was OpenCV-based (disables text filtering)
 * @param scr Screen reference for distance-based sorting
 * @return Newly allocated state, or NULL on failure
 */
hint_state_t* hint_state_create(struct hint *hints, size_t count,
                                 hint_mode_type_t mode, int is_opencv,
                                 screen_t scr);

/**
 * Destroy hint state
 *
 * @param state State to destroy (can be NULL)
 */
void hint_state_destroy(hint_state_t *state);

/**
 * Reset all filters to empty
 *
 * @param state State to reset
 */
void hint_state_reset_filters(hint_state_t *state);

/**
 * Reset only numeric filter
 *
 * @param state State to modify
 */
void hint_state_reset_num_filter(hint_state_t *state);

/**
 * Append character to appropriate filter buffer
 *
 * @param state State to modify
 * @param c Character to append
 * @param is_letter True if character is a letter (goes to text_filter)
 * @return 1 on success, 0 if buffer full
 */
int hint_state_append_filter(hint_state_t *state, char c, int is_letter);

/**
 * Remove last character from filters (backspace behavior)
 *
 * Priority: text_filter first, then num_filter
 *
 * @param state State to modify
 * @return 1 if character was removed, 0 if both buffers empty
 */
int hint_state_undo_filter(hint_state_t *state);

/**
 * Get current highlighted hint
 *
 * @param state State to query
 * @return Pointer to highlighted hint, or NULL if no matches
 */
struct hint* hint_state_get_highlighted(hint_state_t *state);

/**
 * Check if state has any active filters
 *
 * @param state State to check
 * @return 1 if any filter is active, 0 otherwise
 */
int hint_state_has_filters(hint_state_t *state);

#endif /* HINT_STATE_H */
