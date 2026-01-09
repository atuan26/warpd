/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * © 2019 Raheman Vaiya (see: LICENSE).
 */

#include "warpd.h"

/*
 * Draw the Normal Mode indicator overlay.
 */
static void draw_normal_overlay(screen_t scr)
{
	int sw, sh;

	platform->screen_get_dimensions(scr, &sw, &sh);

	const int gap = 10;
	const int indicator_size = (config_get_int("normal_indicator_size") * sh) / 1080;
	const char *indicator_color = config_get("normal_indicator_color");
	const char *indicator = config_get("normal_indicator");

	platform->screen_clear(scr);

	/* Draw indicator if configured */
	if (strcmp(indicator, "none") != 0) {
		if (!strcmp(indicator, "bottomleft"))
			platform->screen_draw_box(scr, gap, sh-indicator_size-gap, indicator_size, indicator_size, indicator_color);
		else if (!strcmp(indicator, "topleft"))
			platform->screen_draw_box(scr, gap, gap, indicator_size, indicator_size, indicator_color);
		else if (!strcmp(indicator, "topright"))
			platform->screen_draw_box(scr, sw-indicator_size-gap, gap, indicator_size, indicator_size, indicator_color);
		else if (!strcmp(indicator, "bottomright"))
			platform->screen_draw_box(scr, sw-indicator_size-gap, sh-indicator_size-gap, indicator_size, indicator_size, indicator_color);
	}

	platform->commit();
}

/*
 * Handle continuous scroll while C-A + scroll key is held.
 * Returns 1 if modifiers are still held, 0 if released.
 */
static int handle_scroll_continuous(int direction, uint8_t required_mods)
{
	struct input_event *ev;
	int mods_still_held = 1;
	
	/* Initial scroll */
	platform->scroll(direction);
	
	/* Keep scrolling while key AND modifiers are held */
	scroll_accelerate(direction);
	
	while (1) {
		ev = platform->input_next_event(10);
		scroll_tick();
		
		if (ev) {
			/* Check if scroll key was released */
			if (!ev->pressed) {
				/* Check if modifiers are still held */
				mods_still_held = (ev->mods & required_mods) == required_mods;
				scroll_decelerate();
				break;
			}
		}
	}
	
	scroll_stop();
	return mods_still_held;
}

/*
 * Wait for next command while modifiers are held.
 * Returns the command event, or NULL if modifiers were released.
 */
static struct input_event *wait_for_command(const char *keys[], size_t nkeys, uint8_t required_mods)
{
	struct input_event *ev;
	
	while (1) {
		ev = platform->input_next_event(50);
		
		if (!ev)
			continue;
		
		/* If this is a key release, check if it's a modifier release */
		if (!ev->pressed) {
			/* Check if required modifiers are still held */
			if ((ev->mods & required_mods) != required_mods) {
				/* Modifiers released - exit command mode */
				return NULL;
			}
			continue;
		}
		
		/* Key press - check if it matches any command */
		config_input_whitelist(keys, nkeys);
		return ev;
	}
}

/*
 * Normal Mode - Passive overlay with keyboard passthrough.
 * 
 * Features:
 * - All hotkeys use C-A (Ctrl+Alt) prefix
 * - While C-A is held, multiple commands can be issued (e.g. j,j,j for scroll)
 * - Releasing C-A returns to keyboard passthrough
 */
struct input_event *normal_mode(void)
{
	screen_t scr;
	struct input_event *ev = NULL;
	const uint8_t PREFIX_MODS = PLATFORM_MOD_CONTROL | PLATFORM_MOD_ALT;

	platform->mouse_get_position(&scr, NULL, NULL);

	/* Draw Normal Mode indicator */
	draw_normal_overlay(scr);

	/* Normal Mode hotkeys - all use C-A prefix */
	const char *keys[] = {
		"normal_quit",          /* C-A-q - exit warpd */
		"normal_pointer",       /* C-A-c - switch to Pointer Mode */
		"normal_grid",          /* C-A-g - grid mode */
		"normal_hint",          /* C-A-x - hint mode */
		"normal_smart_hint",    /* C-A-f - smart hint */
		"normal_scroll_down",   /* C-A-j - scroll down */
		"normal_scroll_up",     /* C-A-k - scroll up */
		"normal_scroll_left",   /* C-A-h - scroll left */
		"normal_scroll_right",  /* C-A-l - scroll right */
		"normal_window_nav",    /* C-A-w - frame navigation */
	};
	const size_t nkeys = sizeof keys / sizeof keys[0];

	struct input_event activation_events[sizeof keys / sizeof keys[0]];
	size_t i;

	for (i = 0; i < nkeys; i++)
		input_parse_string(&activation_events[i], config_get(keys[i]));

	while (1) {
		/* Wait for any Normal Mode hotkey (keyboard NOT grabbed) */
		ev = platform->input_wait(activation_events, nkeys);

		if (!ev) {
			/* Config file changed, re-parse keys */
			for (i = 0; i < nkeys; i++)
				input_parse_string(&activation_events[i], config_get(keys[i]));
			draw_normal_overlay(scr);
			continue;
		}

		/* Keyboard is now grabbed - stay in "command mode" while C-A is held */
		config_input_whitelist(keys, nkeys);

command_loop:
		/* Handle mode transitions (exit normal mode) */
		if (config_input_match(ev, "normal_quit")) {
			platform->input_ungrab_keyboard();
			break;
		}
		
		if (config_input_match(ev, "normal_pointer")) {
			platform->input_ungrab_keyboard();
			break;
		}
		
		if (config_input_match(ev, "normal_grid") ||
		    config_input_match(ev, "normal_hint") ||
		    config_input_match(ev, "normal_smart_hint")) {
			platform->input_ungrab_keyboard();
			break;
		}

		/* Handle scroll - stays in command mode if modifiers held */
		if (config_input_match(ev, "normal_scroll_down")) {
			if (handle_scroll_continuous(SCROLL_DOWN, PREFIX_MODS)) {
				ev = wait_for_command(keys, nkeys, PREFIX_MODS);
				if (ev) goto command_loop;
			}
		} else if (config_input_match(ev, "normal_scroll_up")) {
			if (handle_scroll_continuous(SCROLL_UP, PREFIX_MODS)) {
				ev = wait_for_command(keys, nkeys, PREFIX_MODS);
				if (ev) goto command_loop;
			}
		} else if (config_input_match(ev, "normal_scroll_left")) {
			if (handle_scroll_continuous(SCROLL_LEFT, PREFIX_MODS)) {
				ev = wait_for_command(keys, nkeys, PREFIX_MODS);
				if (ev) goto command_loop;
			}
		} else if (config_input_match(ev, "normal_scroll_right")) {
			if (handle_scroll_continuous(SCROLL_RIGHT, PREFIX_MODS)) {
				ev = wait_for_command(keys, nkeys, PREFIX_MODS);
				if (ev) goto command_loop;
			}
		} else if (config_input_match(ev, "normal_window_nav")) {
			if (platform->window_navigation_mode) {
				platform->window_navigation_mode(scr);
			}
		}

		/* Release keyboard to return to passthrough */
		platform->input_ungrab_keyboard();

		/* Redraw overlay */
		platform->mouse_get_position(&scr, NULL, NULL);
		draw_normal_overlay(scr);
	}

	/* Cleanup */
	platform->screen_clear(scr);
	platform->commit();

	return ev;
}
