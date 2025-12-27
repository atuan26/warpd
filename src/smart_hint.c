#include "platform.h"
#include "warpd.h"
#include "smart_hint/hint_state.h"
#include "smart_hint/hint_filter.h"
#include "smart_hint/hint_label_generator.h"
#include "smart_hint/hint_sorter.h"
#include "smart_hint/hint_renderer.h"
#include "smart_hint/hint_input_handler.h"
#include "smart_hint/detector_thread.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* Last selected hint (for external use) */
char s_last_selected_hint[32] = {0};

/**
 * Get hint size based on screen dimensions
 */
static void get_hint_size(screen_t scr, int *w, int *h)
{
	int sw, sh;

	platform->screen_get_dimensions(scr, &sw, &sh);

	/* Normalize to landscape orientation for consistent sizing */
	if (sw < sh) {
		int tmp = sw;
		sw = sh;
		sh = tmp;
	}

	*w = (sw * config_get_int("hint_size")) / 1000;
	*h = (sh * config_get_int("hint_size")) / 1000;
}

/**
 * Convert detected UI elements to hint structures
 * 
 * Note: UI elements have absolute screen coordinates, but hints need
 * screen-relative coordinates for proper rendering and mouse movement.
 * The screen_x and screen_y parameters are the screen's origin in
 * virtual screen space.
 */
static struct hint *convert_elements_to_hints(struct ui_detection_result *result,
                                                int hint_w, int hint_h,
                                                int screen_x, int screen_y,
                                                int screen_w, int screen_h,
                                                size_t *out_count,
                                                int *out_is_opencv)
{
	if (!result || result->error != 0 || result->count == 0) {
		*out_count = 0;
		*out_is_opencv = 0;
		return NULL;
	}

	struct hint *hints = calloc(result->count, sizeof(struct hint));
	if (!hints) {
		*out_count = 0;
		*out_is_opencv = 0;
		return NULL;
	}

	const char *mode = config_get("smart_hint_mode");
	int is_numeric_mode = strcmp(mode, "numeric") == 0;

	/* Check if this is an OpenCV result (all elements have no names) */
	int all_no_names = 1;
	for (size_t i = 0; i < result->count; i++) {
		if (result->elements[i].name != NULL) {
			all_no_names = 0;
			break;
		}
	}
	*out_is_opencv = all_no_names;

	if (all_no_names) {
		const char *opencv_mode = config_get("opencv_hint_mode");
		if (strcmp(opencv_mode, "inherit") != 0) {
			is_numeric_mode = strcmp(opencv_mode, "numeric") == 0;
			fprintf(stderr, "DEBUG: OpenCV detected, using opencv_hint_mode='%s'\n", opencv_mode);
		}
	}

	/* Convert UI elements to hints */
	size_t valid_count = 0;
	for (size_t i = 0; i < result->count; i++) {
		struct ui_element *element = &result->elements[i];

		/* Convert absolute coordinates to screen-relative coordinates */
		int rel_x = element->x - screen_x;
		int rel_y = element->y - screen_y;

		/* Skip elements that are outside the current screen bounds */
		if (rel_x < 0 || rel_y < 0 || 
		    rel_x >= screen_w || rel_y >= screen_h) {
			continue;
		}

		hints[valid_count].x = rel_x;
		hints[valid_count].y = rel_y;
		hints[valid_count].w = hint_w;
		hints[valid_count].h = hint_h;
		hints[valid_count].original_index = i;
		hints[valid_count].highlighted = 0;

		if (element->name) {
			hints[valid_count].element_name = strdup(element->name);
		} else if (element->role) {
			hints[valid_count].element_name = strdup(element->role);
		} else {
			hints[valid_count].element_name = NULL;
		}
		valid_count++;
	}

	/* Generate labels based on mode */
	if (is_numeric_mode) {
		hint_label_generate_numeric(hints, valid_count);
		fprintf(stderr, "DEBUG: Created %zu hints in numeric mode%s (screen offset: %d,%d):\n",
			valid_count, all_no_names ? " (OpenCV - text filtering disabled)" : "",
			screen_x, screen_y);
		for (size_t i = 0; i < valid_count && i < 10; i++) {
			fprintf(stderr, "  Hint %zu: label='%s' name='%s' pos=(%d,%d)\n",
				i, hints[i].label,
				hints[i].element_name ? hints[i].element_name : "(null)",
				hints[i].x, hints[i].y);
		}
		if (valid_count > 10) {
			fprintf(stderr, "  ... and %zu more hints\n", valid_count - 10);
		}
	} else {
		hint_label_generate_alphabetic(hints, valid_count);
	}

	*out_count = valid_count;

	return hints;
}

/**
 * Move mouse to hint position
 */
static void navigate_to_hint(struct hint *h, screen_t scr)
{
	if (!h) {
		return;
	}

	hint_renderer_clear(scr);

	int nx = h->x + h->w / 2;
	int ny = h->y + h->h / 2;

	platform->mouse_move(scr, nx + 1, ny + 1);
	platform->mouse_move(scr, nx, ny);
}

/**
 * Interactive hint selection loop
 */
static int hint_selection_loop(screen_t scr, struct hint *hints, size_t nr_hints)
{
	if (nr_hints == 0) {
		fprintf(stderr, "No hints available\n");
		return -1;
	}

	/* Determine mode */
	const char *mode = config_get("smart_hint_mode");
	hint_mode_type_t hint_mode = (strcmp(mode, "numeric") == 0) ?
	                              HINT_MODE_NUMERIC : HINT_MODE_ALPHABETIC;

	/* Check if OpenCV result */
	int is_opencv = 1;
	for (size_t i = 0; i < nr_hints; i++) {
		if (hints[i].element_name != NULL) {
			is_opencv = 0;
			break;
		}
	}

	if (is_opencv) {
		const char *opencv_mode = config_get("opencv_hint_mode");
		if (strcmp(opencv_mode, "inherit") != 0) {
			hint_mode = (strcmp(opencv_mode, "numeric") == 0) ?
			            HINT_MODE_NUMERIC : HINT_MODE_ALPHABETIC;
		}
	}

	/* Create state */
	hint_state_t *state = hint_state_create(hints, nr_hints, hint_mode, is_opencv, scr);
	if (!state) {
		fprintf(stderr, "Failed to create hint state\n");
		return -1;
	}

	/* Apply initial filter (shows all hints) */
	hint_filter_apply(state);
	hint_renderer_draw_state(state);

	int rc = 0;
	int last_input_was_letter = 0;

	/* Setup input */
	platform->input_grab_keyboard();
	platform->mouse_hide();

	const char *keys[] = {
		"exit",
		"smart_hint_select",
		"hint_undo_all",
		"hint_undo",
	};
	config_input_whitelist(keys, sizeof keys / sizeof keys[0]);

	size_t prev_matched = state->nr_matched;

	/* Main input loop */
	while (1) {
		struct input_event *ev = platform->input_next_event(0);

		if (!ev->pressed)
			continue;

		/* Parse input to command */
		hint_command_t cmd = hint_input_parse(ev, state);

		/* Handle special case: filter character input */
		if (cmd.type == HINT_CMD_FILTER_CHAR) {
			/* Apply filter character */
			int append_success = hint_state_append_filter(state, cmd.filter_char, cmd.is_letter);
			if (!append_success) {
				/* Buffer full or OpenCV text filter - ignore input */
				continue;
			}
			last_input_was_letter = cmd.is_letter;

			/* Filter and check result */
			int filter_result = hint_filter_apply(state);
			
			/* If filter rejected the change (would result in 0 matches), skip rendering */
			if (filter_result == -1) {
				/* Filter function already reverted the state */
				continue;
			}
			
			/* If labels were regenerated, clear numeric filter */
			if (filter_result == 1) {
				hint_state_reset_num_filter(state);
				fprintf(stderr, "DEBUG: Labels regenerated, cleared num_buf\n");
			}

			/* Render updated hints */
			hint_renderer_draw_state(state);

			/* Auto-select if single match (but not during text filtering in numeric mode) */
			if (state->nr_matched == 1 &&
			    !(hint_mode == HINT_MODE_NUMERIC && last_input_was_letter)) {
				struct hint *h = &state->matched[0];
				navigate_to_hint(h, scr);

				if (hint_mode == HINT_MODE_NUMERIC) {
					snprintf(s_last_selected_hint, sizeof(s_last_selected_hint),
					         "%d", h->original_index + 1);
				} else {
					strncpy(s_last_selected_hint, state->num_filter,
					        sizeof(s_last_selected_hint) - 1);
				}
				break;
			}

			prev_matched = state->nr_matched;
			continue;
		}

		/* Execute command */
		struct hint *selected = NULL;
		int should_exit = hint_input_execute(cmd, state, &selected);

		if (should_exit) {
			if (selected) {
				/* Navigate to selected hint */
				navigate_to_hint(selected, scr);

				if (hint_mode == HINT_MODE_NUMERIC) {
					snprintf(s_last_selected_hint, sizeof(s_last_selected_hint),
					         "%d", selected->original_index + 1);
				} else {
					strncpy(s_last_selected_hint, state->num_filter,
					        sizeof(s_last_selected_hint) - 1);
				}
			} else {
				rc = -1; /* Exit without selection */
			}
			break;
		}

		/* Re-filter and render after undo operations */
		if (cmd.type == HINT_CMD_UNDO || cmd.type == HINT_CMD_UNDO_ALL) {
			size_t before_filter = state->nr_matched;
			hint_filter_apply(state);
			hint_renderer_draw_state(state);

			/* Check for auto-select after undo */
			if (state->nr_matched == 1 &&
			    !(hint_mode == HINT_MODE_NUMERIC && last_input_was_letter)) {
				struct hint *h = &state->matched[0];
				navigate_to_hint(h, scr);

				if (hint_mode == HINT_MODE_NUMERIC) {
					snprintf(s_last_selected_hint, sizeof(s_last_selected_hint),
					         "%d", h->original_index + 1);
				} else {
					strncpy(s_last_selected_hint, state->num_filter,
					        sizeof(s_last_selected_hint) - 1);
				}
				break;
			}

			prev_matched = state->nr_matched;
		}
	}

	/* Cleanup */
	platform->input_ungrab_keyboard();
	hint_renderer_clear(scr);
	platform->mouse_show();
	platform->commit();

	hint_state_destroy(state);

	return rc;
}

/**
 * Main smart hint mode entry point
 */
int smart_hint_mode(void)
{
	/* Check if platform supports UI element detection */
	if (!platform->detect_ui_elements) {
		fprintf(stderr, "Smart hint mode not supported on this platform\n");
		return -1;
	}

	/* Get current screen info */
	screen_t scr;
	platform->mouse_get_position(&scr, NULL, NULL);

	int hint_w, hint_h;
	get_hint_size(scr, &hint_w, &hint_h);

	platform->mouse_hide();

	show_message(scr, "Detecting...", hint_h);

	int mx, my;
	platform->mouse_get_position(&scr, &mx, &my);
	draw_loading_cursor(scr, mx, my);
	platform->commit();

	/* Lock keyboard during detection */
	platform->input_grab_keyboard();

	/* Start detection in background thread */
	detector_thread_t *thread = detector_thread_create();
	if (!thread) {
		fprintf(stderr, "Failed to create detection thread\n");
		platform->input_ungrab_keyboard();
		platform->mouse_show();
		return -1;
	}

	if (detector_thread_start(thread) != 0) {
		fprintf(stderr, "Failed to start detection thread\n");
		detector_thread_destroy(thread);
		platform->input_ungrab_keyboard();
		platform->mouse_show();
		return -1;
	}

	/* Keep drawing animated cursor while detection runs */
	while (!detector_thread_is_done(thread)) {
		/* Redraw message and animated cursor */
		platform->mouse_get_position(&scr, &mx, &my);
		show_message(scr, "Detecting...", hint_h);
		draw_loading_cursor(scr, mx, my);
		platform->commit();

		/* Sleep to avoid excessive CPU usage (~60 FPS) */
#ifdef _WIN32
		Sleep(16);
#else
		usleep(16000);
#endif
	}

	/* Wait for thread and get result */
	struct ui_detection_result *result = detector_thread_join(thread);

	/* Unlock keyboard */
	platform->input_ungrab_keyboard();

	platform->screen_clear(scr);
	platform->commit();

	platform->mouse_show();

	if (!result) {
		fprintf(stderr, "Failed to detect UI elements\n");
		return -1;
	}

	/* Handle detection errors */
	if (result->error != 0) {
		fprintf(stderr, "Detection error: %s\n", result->error_msg);
		platform->free_ui_elements(result);
		return -1;
	}

	/* Get screen offset for coordinate conversion */
	int screen_x = 0, screen_y = 0;
	int screen_w, screen_h;
	if (platform->screen_get_offset) {
		platform->screen_get_offset(scr, &screen_x, &screen_y);
	}
	platform->screen_get_dimensions(scr, &screen_w, &screen_h);

	/* Convert elements to hints (converting absolute to screen-relative coordinates) */
	size_t hint_count = 0;
	int is_opencv = 0;
	struct hint *hint_array = convert_elements_to_hints(result, hint_w, hint_h,
	                                                      screen_x, screen_y,
	                                                      screen_w, screen_h,
	                                                      &hint_count, &is_opencv);

	/* Free detection result (we've copied what we need) */
	platform->free_ui_elements(result);

	if (!hint_array || hint_count == 0) {
		fprintf(stderr, "No interactive elements found\n");
		show_message(scr, "No elements found", hint_h);

		/* Wait a moment so user can see the message */
		#ifdef _WIN32
			Sleep(1000);
		#else
			usleep(1000000);
		#endif

		platform->screen_clear(scr);
		platform->commit();

		if (hint_array)
			free(hint_array);
		return -1;
	}

	/* Run hint selection */
	int rc = hint_selection_loop(scr, hint_array, hint_count);

	/* Cleanup */
	for (size_t i = 0; i < hint_count; i++) {
		if (hint_array[i].element_name)
			free(hint_array[i].element_name);
	}
	free(hint_array);

	return rc;
}
