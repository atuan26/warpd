/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Input Handler Module Implementation
 */

#include "hint_input_handler.h"
#include "../warpd.h"
#include <stdio.h>

extern struct platform *platform;

hint_command_t hint_input_parse(struct input_event *ev, hint_state_t *state)
{
	hint_command_t cmd = {0};
	cmd.type = HINT_CMD_NONE;

	if (!ev || !ev->pressed || !state) {
		return cmd;
	}

	if (config_input_match(ev, "exit")) {
		cmd.type = HINT_CMD_EXIT;
		return cmd;
	}

	/* Check for select command (numeric mode only) */
	if (config_input_match(ev, "smart_hint_select")) {
		if (state->mode == HINT_MODE_NUMERIC) {
			cmd.type = HINT_CMD_SELECT;
		}
		return cmd;
	}

	/* Check for undo all command */
	if (config_input_match(ev, "hint_undo_all")) {
		cmd.type = HINT_CMD_UNDO_ALL;
		return cmd;
	}

	/* Check for undo command */
	if (config_input_match(ev, "hint_undo")) {
		cmd.type = HINT_CMD_UNDO;
		return cmd;
	}

	/* Try to parse as character filter */
	const char *name = input_event_tostr(ev);
	if (!name || name[1]) {
		/* Not a single character */
		return cmd;
	}

	char c = name[0];

	/* Check if it's a valid filter character */
	int is_numeric = (state->mode == HINT_MODE_NUMERIC);
	int is_digit = (c >= '0' && c <= '9');
	int is_letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');

	if (is_numeric) {
		/* Numeric mode: accept digits always, letters only if not OpenCV */
		if (is_digit) {
			cmd.type = HINT_CMD_FILTER_CHAR;
			cmd.filter_char = c;
			cmd.is_letter = 0;
		} else if (is_letter && !state->is_opencv_result) {
			/* Text filtering only available with native detection (not OpenCV) */
			cmd.type = HINT_CMD_FILTER_CHAR;
			cmd.filter_char = c;
			cmd.is_letter = 1;
		}
		/* Silently ignore letters when OpenCV (no text filtering possible) */
	} else {
		/* Alphabetic mode: accept any character */
		cmd.type = HINT_CMD_FILTER_CHAR;
		cmd.filter_char = c;
		cmd.is_letter = 0;
	}

	return cmd;
}

int hint_input_execute(hint_command_t cmd, hint_state_t *state, struct hint **selected_hint)
{
	if (!state) {
		return 0;
	}

	/* Initialize output */
	if (selected_hint) {
		*selected_hint = NULL;
	}

	switch (cmd.type) {
	case HINT_CMD_EXIT:
		return 1; /* Exit loop */

	case HINT_CMD_SELECT:
		/* Select currently highlighted hint */
		if (state->nr_matched > 0 &&
		    state->highlighted_index >= 0 &&
		    (size_t)state->highlighted_index < state->nr_matched) {
			if (selected_hint) {
				*selected_hint = &state->matched[state->highlighted_index];
			}
			return 1; /* Exit loop */
		}
		break;

	case HINT_CMD_UNDO:
		hint_state_undo_filter(state);
		break;

	case HINT_CMD_UNDO_ALL:
		hint_state_reset_filters(state);
		break;

	case HINT_CMD_FILTER_CHAR:
		/* Append character to filter */
		hint_state_append_filter(state, cmd.filter_char, cmd.is_letter);
		break;

	case HINT_CMD_NONE:
	default:
		/* Do nothing */
		break;
	}

	return 0; /* Continue loop */
}
