#include "warpd.h"

int mode_loop(int initial_mode, int oneshot, int record_history)
{
	int mode = initial_mode;
	int rc = 0;
	struct input_event *ev = NULL;

	while (1) {
		int btn = 0;
		config_input_whitelist(NULL, 0);

		switch (mode) {
		case MODE_HISTORY:
			history_hint_mode();
			ev = NULL;
			mode = MODE_POINTER;
			break;
		case MODE_HINTSPEC:
			hintspec_mode();
			break;
		case MODE_NORMAL:
			/* Passive overlay mode with keyboard passthrough */
			ev = normal_mode();

			/* Normal Mode uses normal_* prefixed keys */
			if (config_input_match(ev, "normal_quit")) {
				rc = 0;
				goto exit;
			} else if (config_input_match(ev, "normal_pointer"))
				mode = MODE_POINTER;
			else if (config_input_match(ev, "normal_hint"))
				mode = MODE_HINT;
			else if (config_input_match(ev, "normal_grid"))
				mode = MODE_GRID;
			else if (config_input_match(ev, "normal_smart_hint"))
				mode = MODE_SMART_HINT;

			break;
		case MODE_POINTER:
			/* Active cursor control mode with full keyboard grab */
			ev = pointer_mode(ev, oneshot);

			if (config_input_match(ev, "history"))
				mode = MODE_HISTORY;
			else if (config_input_match(ev, "hint"))
				mode = MODE_HINT;
			else if (config_input_match(ev, "hint2"))
				mode = MODE_HINT2;
			else if (config_input_match(ev, "grid"))
				mode = MODE_GRID;
			else if (config_input_match(ev, "screen"))
				mode = MODE_SCREEN_SELECTION;
			else if (config_input_match(ev, "smart_hint"))
				mode = MODE_SMART_HINT;
			else if ((rc = config_input_match(ev, "oneshot_buttons")) || !ev) {
				goto exit;
			}
			else if (config_input_match(ev, "exit") || !ev) {
				/* Escape returns to Normal Mode */
				mode = MODE_NORMAL;
				ev = NULL;
			}

			break;
		case MODE_HINT2:
		case MODE_HINT:
			full_hint_mode(mode == MODE_HINT2);
			ev = NULL;
			mode = MODE_POINTER;
			break;
		case MODE_GRID:
			ev = grid_mode();
			if (config_input_match(ev, "exit"))
				ev = NULL;
			mode = MODE_POINTER;
			break;
		case MODE_SCREEN_SELECTION:
			screen_selection_mode();
			mode = MODE_POINTER;
			ev = NULL;
			break;
		case MODE_SMART_HINT:
			smart_hint_mode();
			mode = MODE_POINTER;
			ev = NULL;
			break;
		}

		if (oneshot && (initial_mode != MODE_POINTER || (btn = config_input_match(ev, "buttons")))) {
			int x, y;
			screen_t scr;

			platform->mouse_get_position(&scr, NULL, NULL);
			platform->mouse_get_position(NULL, &x, &y);

			if (record_history)
				histfile_add(x, y);

			if (mode == MODE_HINTSPEC)
				printf("%d %d %s\n", x, y, last_selected_hint);
			else
				printf("%d %d\n", x, y);

			return btn;
		}
	}

exit:
	return rc;
}
