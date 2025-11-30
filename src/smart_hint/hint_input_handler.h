/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Input Handler Module
 *
 * Implements Command pattern for input handling.
 * Separates input parsing from command execution.
 */

#ifndef HINT_INPUT_HANDLER_H
#define HINT_INPUT_HANDLER_H

#include "hint_state.h"
#include "../platform.h"

/**
 * Command types for hint input
 */
typedef enum {
	HINT_CMD_NONE,           /* No command / unknown input */
	HINT_CMD_EXIT,           /* Exit hint mode */
	HINT_CMD_SELECT,         /* Select highlighted hint */
	HINT_CMD_UNDO,           /* Undo last filter character */
	HINT_CMD_UNDO_ALL,       /* Clear all filters */
	HINT_CMD_FILTER_CHAR,    /* Add character to filter */
} hint_command_type_t;

/**
 * Input command structure
 */
typedef struct {
	hint_command_type_t type;
	char filter_char;         /* Character for HINT_CMD_FILTER_CHAR */
	int is_letter;            /* True if filter_char is a letter */
} hint_command_t;

/**
 * Parse input event into command
 *
 * @param ev Input event
 * @param state Current hint state (used for mode-specific parsing)
 * @return Parsed command
 */
hint_command_t hint_input_parse(struct input_event *ev, hint_state_t *state);

/**
 * Execute a hint command
 *
 * @param cmd Command to execute
 * @param state Hint state to modify
 * @param selected_hint Output: pointer to selected hint if CMD_SELECT, NULL otherwise
 * @return 1 to exit selection loop, 0 to continue
 */
int hint_input_execute(hint_command_t cmd, hint_state_t *state, struct hint **selected_hint);

#endif /* HINT_INPUT_HANDLER_H */
