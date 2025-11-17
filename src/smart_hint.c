/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint Mode - Intelligently detect and hint interactive UI elements
 *
 * Inspired by the Vimium browser extension.
 */

#include "platform.h"
#include "warpd.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* Module state */
static struct hint *hints = NULL;
static struct hint matched[MAX_HINTS];
static size_t nr_hints = 0;
static size_t nr_matched = 0;

char s_last_selected_hint[32] = {0};

/**
 * Filter hints based on prefix match
 */
static void filter_hints(screen_t scr, const char *prefix)
{
	nr_matched = 0;

	for (size_t i = 0; i < nr_hints; i++) {
		if (prefix && strncasecmp(hints[i].label, prefix, strlen(prefix)) == 0) {
			matched[nr_matched++] = hints[i];
		}
	}

	platform->screen_clear(scr);
	platform->hint_draw(scr, matched, nr_matched);
	platform->commit();
}

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
 * Generate alphabetic labels for hints (A, B, C, ... Z, AA, AB, ...)
 */
static void generate_labels(struct hint *hints, size_t num_elements)
{
	int label_len = 1;
	size_t max_elements = 26;

	/* Calculate required label length */
	while (max_elements < num_elements) {
		label_len++;
		max_elements *= 26;
	}

	/* Generate labels */
	for (size_t i = 0; i < num_elements; i++) {
		int remaining = i;
		char label[16] = {0};

		/* Initialize with 'A's */
		for (int j = 0; j < label_len; j++) {
			label[j] = 'A';
		}

		/* Convert index to base-26 representation */
		for (int pos = 0; pos < label_len && remaining > 0; pos++) {
			int val = remaining % 26;
			label[pos] = 'A' + val;
			remaining /= 26;
		}

		strncpy(hints[i].label, label, sizeof(hints[i].label) - 1);
	}
}

/**
 * Convert detected UI elements to hint structures
 */
static struct hint *convert_elements_to_hints(struct ui_detection_result *result,
                                                int hint_w, int hint_h,
                                                size_t *out_count)
{
	if (!result || result->error != 0 || result->count == 0) {
		*out_count = 0;
		return NULL;
	}

	struct hint *hints = calloc(result->count, sizeof(struct hint));
	if (!hints) {
		*out_count = 0;
		return NULL;
	}

	/* Convert each element to a hint */
	for (size_t i = 0; i < result->count; i++) {
		struct ui_element *element = &result->elements[i];

		hints[i].x = element->x;
		hints[i].y = element->y;
		hints[i].w = hint_w;
		hints[i].h = hint_h;
	}

	generate_labels(hints, result->count);
	*out_count = result->count;

	return hints;
}

/**
 * Interactive hint selection loop
 */
static int hint_selection(screen_t scr, struct hint *_hints, size_t _nr_hints)
{
	hints = _hints;
	nr_hints = _nr_hints;

	if (nr_hints == 0) {
		fprintf(stderr, "No hints available\n");
		return -1;
	}

	filter_hints(scr, "");

	int rc = 0;
	char buf[32] = {0};

	platform->input_grab_keyboard();
	platform->mouse_hide();

	/* Whitelist allowed keys during selection */
	const char *keys[] = {
		"smart_hint_exit",
		"hint_undo_all",
		"hint_undo",
	};
	config_input_whitelist(keys, sizeof keys / sizeof keys[0]);

	/* Main selection loop */
	while (1) {
		struct input_event *ev = platform->input_next_event(0);

		if (!ev->pressed)
			continue;

		size_t len = strlen(buf);

		if (config_input_match(ev, "smart_hint_exit")) {
			rc = -1;
			break;
		} else if (config_input_match(ev, "hint_undo_all")) {
			buf[0] = 0;
		} else if (config_input_match(ev, "hint_undo")) {
			if (len > 0)
				buf[len - 1] = 0;
		} else {
			const char *name = input_event_tostr(ev);

			/* Only accept single character input */
			if (!name || name[1])
				continue;

			if (len < sizeof(buf) - 1)
				buf[len++] = name[0];
		}

		filter_hints(scr, buf);

		/* Single match - move mouse and exit */
		if (nr_matched == 1) {
			struct hint *h = &matched[0];

			platform->screen_clear(scr);

			int nx = h->x + h->w / 2;
			int ny = h->y + h->h / 2;

			/* Wiggle to accommodate text selection widgets */
			platform->mouse_move(scr, nx + 1, ny + 1);
			platform->mouse_move(scr, nx, ny);

			strncpy(s_last_selected_hint, buf, sizeof(s_last_selected_hint) - 1);
			break;
		} else if (nr_matched == 0) {
			/* No matches - reset or exit */
			break;
		}
	}

	platform->input_ungrab_keyboard();
	platform->screen_clear(scr);
	platform->mouse_show();
	platform->commit();

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

	show_message(scr, "Detecting...", hint_h);

	/* Lock keyboard during detection to prevent accidental typing
	 * 
	 * Platform support:
	 * - X11: ✓ Full system-level block (XIGrabDevice)
	 * - macOS: ✓ Event tap blocks keyboard (runs in separate thread)
	 * - Wayland: ⚠ Limited - only blocks when window is focused
	 * - Windows: ✓ Works with admin privileges (BlockInput API)
	 *            ⚠ Without admin: keys may be delayed but still processed
	 * 
	 * Note: Run warpd as administrator on Windows for best experience.
	 */
	platform->input_grab_keyboard();

	/* Detect UI elements using platform-specific method */
	struct ui_detection_result *result = platform->detect_ui_elements();
	
	/* Unlock keyboard */
	platform->input_ungrab_keyboard();
	
	/* Clear the detecting message */
	platform->screen_clear(scr);
	platform->commit();

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

	/* Convert elements to hints */
	size_t hint_count = 0;
	struct hint *hint_array = convert_elements_to_hints(result, hint_w, hint_h, &hint_count);

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
	int rc = hint_selection(scr, hint_array, hint_count);

	/* Cleanup */
	free(hint_array);

	return rc;
}
