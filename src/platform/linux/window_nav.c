/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * © 2019 Raheman Vaiya (see: LICENSE).
 *
 * Window navigation mode for Linux (AT-SPI based).
 */

#include "../../platform.h"
#include "atspi-detector.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>

/* Import warpd functions */
extern const char *config_get(const char *key);
extern int config_get_int(const char *key);
extern void config_input_whitelist(const char *names[], size_t n);
extern int config_input_match(struct input_event *ev, const char *str);

/* Platform instance (set by main) */
extern struct platform *platform;

/*
 * Draw outline around a window for visual selection feedback.
 */
static void draw_window_outline(screen_t scr, int x, int y, int w, int h)
{
	const char *outline_color = config_get("window_outline_color");
	const int outline_width = config_get_int("window_outline_width");
	int scr_x, scr_y;

	/* Get screen offset for multi-monitor */
	platform->screen_get_offset(scr, &scr_x, &scr_y);

	/* Convert window coords to screen-relative coords */
	int rel_x = x - scr_x;
	int rel_y = y - scr_y;

	/* Draw 4 rectangles to form an outline */
	platform->screen_draw_box(scr, rel_x, rel_y, w, outline_width, outline_color);  /* Top */
	platform->screen_draw_box(scr, rel_x, rel_y + h - outline_width, w, outline_width, outline_color);  /* Bottom */
	platform->screen_draw_box(scr, rel_x, rel_y, outline_width, h, outline_color);  /* Left */
	platform->screen_draw_box(scr, rel_x + w - outline_width, rel_y, outline_width, h, outline_color);  /* Right */
}

/*
 * Window navigation sub-mode.
 * Lists all visible windows, allows Tab cycling, Enter to focus.
 */
void linux_window_navigation_mode(screen_t scr)
{
	GSList *windows = get_all_windows();
	if (!windows) {
		fprintf(stderr, "No windows found for navigation\n");
		return;
	}

	guint window_count = g_slist_length(windows);
	if (window_count == 0) {
		free_window_list(windows);
		return;
	}

	guint current_index = 0;
	struct input_event *ev;

	const char *nav_keys[] = {
		"window_next",
		"window_prev",
		"window_select",
		"exit",
	};

	platform->input_grab_keyboard();

	while (1) {
		/* Draw outline around current window */
		WindowInfo *current = g_slist_nth_data(windows, current_index);
		if (current) {
			platform->screen_clear(scr);
			draw_window_outline(scr, current->x, current->y, current->w, current->h);
			platform->commit();
		}

		config_input_whitelist(nav_keys, sizeof nav_keys / sizeof nav_keys[0]);
		ev = platform->input_next_event(0);

		if (!ev || !ev->pressed)
			continue;

		if (config_input_match(ev, "window_next")) {
			current_index = (current_index + 1) % window_count;
		} else if (config_input_match(ev, "window_prev")) {
			current_index = (current_index + window_count - 1) % window_count;
		} else if (config_input_match(ev, "window_select")) {
			WindowInfo *selected = g_slist_nth_data(windows, current_index);
			if (selected) {
				focus_window(selected);
			}
			break;
		} else if (config_input_match(ev, "exit")) {
			break;
		}
	}

	platform->input_ungrab_keyboard();
	platform->screen_clear(scr);
	platform->commit();

	free_window_list(windows);
}
