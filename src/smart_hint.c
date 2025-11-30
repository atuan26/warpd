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
#include <pthread.h>
#endif

/* Module state */
static struct hint *hints = NULL;
static struct hint matched[MAX_HINTS];
static size_t nr_hints = 0;

/* Detection thread state */
struct detection_context {
	volatile int done;
	struct ui_detection_result *result;
#ifdef _WIN32
	HANDLE thread;
	CRITICAL_SECTION lock;
#else
	pthread_t thread;
	pthread_mutex_t lock;
#endif
};
static size_t nr_matched = 0;
static int highlighted_index = 0;
static screen_t g_current_screen;  /* For center-based sorting */
static int is_opencv_result = 0;  /* Flag to indicate if results are from OpenCV (no text filtering) */
static int labels_were_regenerated = 0;  /* Flag to track if labels were regenerated (affects filtering source) */

char s_last_selected_hint[32] = {0};

/**
 * Detection thread function
 */
#ifdef _WIN32
static DWORD WINAPI detection_thread(LPVOID param)
#else
static void *detection_thread(void *param)
#endif
{
	struct detection_context *ctx = (struct detection_context *)param;
	
	/* Run detection */
	ctx->result = platform->detect_ui_elements();
	
	/* Mark as done */
#ifdef _WIN32
	EnterCriticalSection(&ctx->lock);
	ctx->done = 1;
	LeaveCriticalSection(&ctx->lock);
#else
	pthread_mutex_lock(&ctx->lock);
	ctx->done = 1;
	pthread_mutex_unlock(&ctx->lock);
#endif
	
	return 0;
}

/**
 * Fuzzy match: check if all characters in pattern appear in order in text
 */
static int fuzzy_match(const char *text, const char *pattern)
{
	if (!text || !pattern)
		return 0;

	const char *t = text;
	const char *p = pattern;

	while (*p) {
		char pc = *p;
		if (pc >= 'A' && pc <= 'Z')
			pc = pc - 'A' + 'a';

		int found = 0;
		while (*t) {
			char tc = *t;
			if (tc >= 'A' && tc <= 'Z')
				tc = tc - 'A' + 'a';

			t++;
			if (tc == pc) {
				found = 1;
				break;
			}
		}

		if (!found)
			return 0;

		p++;
	}

	return 1;
}

/**
 * Compare function for sorting hints by distance from screen center
 */
static int compare_distance_from_center(const void *a, const void *b)
{
	const struct hint *ha = (const struct hint *)a;
	const struct hint *hb = (const struct hint *)b;

	/* Get screen dimensions to find center */
	static int screen_center_x = -1;
	static int screen_center_y = -1;

	if (screen_center_x < 0) {
		int sw, sh;
		platform->screen_get_dimensions(g_current_screen, &sw, &sh);
		screen_center_x = sw / 2;
		screen_center_y = sh / 2;
	}

	/* Calculate distance from center for each hint (using center of hint) */
	int ax = ha->x + ha->w / 2;
	int ay = ha->y + ha->h / 2;
	int bx = hb->x + hb->w / 2;
	int by = hb->y + hb->h / 2;

	int dist_a = (ax - screen_center_x) * (ax - screen_center_x) +
	             (ay - screen_center_y) * (ay - screen_center_y);
	int dist_b = (bx - screen_center_x) * (bx - screen_center_x) +
	             (by - screen_center_y) * (by - screen_center_y);

	return dist_a - dist_b;  /* Closer to center = smaller value */
}

/**
 * Filter hints based on numeric and text filters
 * Returns 1 if labels were regenerated (num_buf should be reset), 0 otherwise
 */
static int filter_hints(screen_t scr, const char *num_filter, const char *text_filter)
{
	const char *mode = config_get("smart_hint_mode");
	int is_numeric_mode = strcmp(mode, "numeric") == 0;

	if (is_numeric_mode && (num_filter[0] || text_filter[0])) {
		fprintf(stderr, "DEBUG: Filtering - num='%s' text='%s'\n", num_filter, text_filter);
	}

	/* Determine source array for filtering */
	/* If labels were regenerated, filter from matched[] (which has new labels) */
	/* Otherwise, filter from original hints[] */
	struct hint *source = labels_were_regenerated ? matched : hints;
	size_t source_count = labels_were_regenerated ? nr_matched : nr_hints;

	/* Temporary array to store new matches */
	struct hint temp_matched[MAX_HINTS];
	size_t temp_count = 0;

	for (size_t i = 0; i < source_count; i++) {
		int matches = 1;

		if (is_numeric_mode) {
			if (num_filter && num_filter[0]) {
				if (strncmp(source[i].label, num_filter, strlen(num_filter)) != 0) {
					matches = 0;
				}
			}

			if (matches && text_filter && text_filter[0]) {
				int fuzzy_result = source[i].element_name ? fuzzy_match(source[i].element_name, text_filter) : 0;
				if (!fuzzy_result) {
					matches = 0;
				}
			}
		} else {
			if (num_filter && num_filter[0]) {
				if (strncasecmp(source[i].label, num_filter, strlen(num_filter)) != 0) {
					matches = 0;
				}
			}
		}

		if (matches) {
			temp_matched[temp_count] = source[i];
			temp_matched[temp_count].highlighted = 0;
			temp_count++;
		}
	}

	/* Copy temp results to matched */
	nr_matched = temp_count;
	for (size_t i = 0; i < temp_count; i++) {
		matched[i] = temp_matched[i];
	}

	if (is_numeric_mode && (num_filter[0] || text_filter[0])) {
		fprintf(stderr, "DEBUG: Matched %zu hints\n", nr_matched);
	}

	/* In numeric mode with TEXT filtering active, sort by distance from center and reassign labels */
	/* Do NOT reassign labels when using numeric filtering - user wants to keep original numbers */
	int regenerated = 0;
	if (is_numeric_mode && nr_matched > 0 && text_filter[0] && !num_filter[0]) {
		/* Sort matched hints by distance from screen center */
		qsort(matched, nr_matched, sizeof(struct hint), compare_distance_from_center);

		/* Reassign numeric labels based on sorted order */
		int label_len = 1;
		size_t max_with_len = 9;
		while (max_with_len < nr_matched) {
			label_len++;
			max_with_len = max_with_len * 10 + 9;
		}

		for (size_t i = 0; i < nr_matched; i++) {
			snprintf(matched[i].label, sizeof(matched[i].label), "%0*zu", label_len, i + 1);
		}

		fprintf(stderr, "DEBUG: Reassigned labels based on distance from center\n");
		labels_were_regenerated = 1;
		regenerated = 1;
	}

	if (nr_matched > 0) {
		highlighted_index = 0;
		matched[0].highlighted = 1;
	} else {
		highlighted_index = 0;
	}

	platform->screen_clear(scr);
	platform->hint_draw(scr, matched, nr_matched);
	platform->commit();

	/* Return 1 if labels were regenerated (caller should reset num_buf) */
	return regenerated;
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
static void generate_alphabetic_labels(struct hint *hints, size_t num_elements)
{
	int label_len = 1;
	size_t max_elements = 26;

	while (max_elements < num_elements) {
		label_len++;
		max_elements *= 26;
	}

	for (size_t i = 0; i < num_elements; i++) {
		int remaining = i;
		char label[16] = {0};

		for (int j = 0; j < label_len; j++) {
			label[j] = 'A';
		}

		for (int pos = 0; pos < label_len && remaining > 0; pos++) {
			int val = remaining % 26;
			label[pos] = 'A' + val;
			remaining /= 26;
		}

		strncpy(hints[i].label, label, sizeof(hints[i].label) - 1);
	}
}

/**
 * Generate numeric labels for hints with equal length based on total count
 */
static void generate_numeric_labels(struct hint *hints, size_t num_elements)
{
	int label_len = 1;
	size_t max_with_len = 9;

	while (max_with_len < num_elements) {
		label_len++;
		max_with_len = max_with_len * 10 + 9;
	}

	for (size_t i = 0; i < num_elements; i++) {
		snprintf(hints[i].label, sizeof(hints[i].label), "%0*zu", label_len, i + 1);
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

	const char *mode = config_get("smart_hint_mode");
	int is_numeric_mode = strcmp(mode, "numeric") == 0;

	// Check if this is an OpenCV result (all elements have no names)
	int all_no_names = 1;
	for (size_t i = 0; i < result->count; i++) {
		if (result->elements[i].name != NULL) {
			all_no_names = 0;
			break;
		}
	}
	is_opencv_result = all_no_names;

	for (size_t i = 0; i < result->count; i++) {
		struct ui_element *element = &result->elements[i];

		hints[i].x = element->x;
		hints[i].y = element->y;
		hints[i].w = hint_w;
		hints[i].h = hint_h;
		hints[i].original_index = i;
		hints[i].highlighted = 0;

		if (element->name) {
			hints[i].element_name = strdup(element->name);
		} else if (element->role) {
			hints[i].element_name = strdup(element->role);
		} else {
			hints[i].element_name = NULL;
		}
	}

	if (is_numeric_mode) {
		generate_numeric_labels(hints, result->count);
		fprintf(stderr, "DEBUG: Created %zu hints in numeric mode%s:\n",
			result->count, is_opencv_result ? " (OpenCV - text filtering disabled)" : "");
		for (size_t i = 0; i < result->count && i < 10; i++) {
			fprintf(stderr, "  Hint %zu: label='%s' name='%s' pos=(%d,%d)\n",
				i, hints[i].label,
				hints[i].element_name ? hints[i].element_name : "(null)",
				hints[i].x, hints[i].y);
		}
		if (result->count > 10) {
			fprintf(stderr, "  ... and %zu more hints\n", result->count - 10);
		}
	} else {
		generate_alphabetic_labels(hints, result->count);
	}

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
	g_current_screen = scr;  /* Store for center-based sorting */
	labels_were_regenerated = 0;  /* Reset flag at start */

	if (nr_hints == 0) {
		fprintf(stderr, "No hints available\n");
		return -1;
	}

	const char *mode = config_get("smart_hint_mode");
	int is_numeric_mode = strcmp(mode, "numeric") == 0;

	char num_buf[32] = {0};
	char text_buf[64] = {0};
	int last_input_was_letter = 0;

	filter_hints(scr, num_buf, text_buf);

	int rc = 0;

	platform->input_grab_keyboard();
	platform->mouse_hide();

	const char *keys[] = {
		"smart_hint_exit",
		"smart_hint_select",
		"hint_undo_all",
		"hint_undo",
	};
	config_input_whitelist(keys, sizeof keys / sizeof keys[0]);

	size_t prev_matched = nr_matched;

	while (1) {
		struct input_event *ev = platform->input_next_event(0);

		if (!ev->pressed)
			continue;

		if (config_input_match(ev, "smart_hint_exit")) {
			rc = -1;
			break;
		} else if (config_input_match(ev, "smart_hint_select")) {
			if (is_numeric_mode && nr_matched > 0 && highlighted_index < (int)nr_matched) {
				struct hint *h = &matched[highlighted_index];
				platform->screen_clear(scr);

				int nx = h->x + h->w / 2;
				int ny = h->y + h->h / 2;

				platform->mouse_move(scr, nx + 1, ny + 1);
				platform->mouse_move(scr, nx, ny);

				snprintf(s_last_selected_hint, sizeof(s_last_selected_hint), "%d", h->original_index + 1);
				break;
			}
		} else if (config_input_match(ev, "hint_undo_all")) {
			num_buf[0] = 0;
			text_buf[0] = 0;
		} else if (config_input_match(ev, "hint_undo")) {
			size_t text_len = strlen(text_buf);
			size_t num_len = strlen(num_buf);

			if (text_len > 0) {
				text_buf[text_len - 1] = 0;
			} else if (num_len > 0) {
				num_buf[num_len - 1] = 0;
			}
		} else {
			const char *name = input_event_tostr(ev);

			if (!name || name[1])
				continue;

			char c = name[0];

			// Save current buffer state in case we need to rollback
			char num_buf_backup[32];
			char text_buf_backup[64];
			strncpy(num_buf_backup, num_buf, sizeof(num_buf_backup));
			strncpy(text_buf_backup, text_buf, sizeof(text_buf_backup));

			if (is_numeric_mode) {
				if (c >= '0' && c <= '9') {
					size_t num_len = strlen(num_buf);
					if (num_len < sizeof(num_buf) - 1) {
						num_buf[num_len] = c;
						num_buf[num_len + 1] = '\0';
					} else {
						continue;
					}
					last_input_was_letter = 0;
				} else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
					// Ignore letter input for OpenCV results 
					if (is_opencv_result) {
						continue;
					}
					size_t text_len = strlen(text_buf);
					if (text_len < sizeof(text_buf) - 1) {
						text_buf[text_len] = c;
						text_buf[text_len + 1] = '\0';
					} else {
						continue;
					}
					last_input_was_letter = 1;
				} else {
					continue;
				}
			} else {
				size_t num_len = strlen(num_buf);
				if (num_len < sizeof(num_buf) - 1) {
					num_buf[num_len] = c;
					num_buf[num_len + 1] = '\0';
				} else {
					continue;
				}
			}

			// Test the filter with new input
			size_t before_filter = nr_matched;
			int labels_regenerated = filter_hints(scr, num_buf, text_buf);

			// If labels were regenerated, reset num_buf so user can type new labels
			if (labels_regenerated) {
				num_buf[0] = '\0';
				fprintf(stderr, "DEBUG: Labels regenerated, cleared num_buf\n");
			}

			// If filter results in 0 matches, rollback and ignore this input
			if (nr_matched == 0 && before_filter > 0) {
				strncpy(num_buf, num_buf_backup, sizeof(num_buf));
				strncpy(text_buf, text_buf_backup, sizeof(text_buf));
				// Restore previous matched state
				filter_hints(scr, num_buf, text_buf);
				continue;
			}

			// If we got a single match and auto-select is enabled
			if (nr_matched == 1 && (!is_numeric_mode || !last_input_was_letter)) {
				struct hint *h = &matched[0];

				platform->screen_clear(scr);

				int nx = h->x + h->w / 2;
				int ny = h->y + h->h / 2;

				platform->mouse_move(scr, nx + 1, ny + 1);
				platform->mouse_move(scr, nx, ny);

				if (is_numeric_mode) {
					snprintf(s_last_selected_hint, sizeof(s_last_selected_hint), "%d", h->original_index + 1);
				} else {
					strncpy(s_last_selected_hint, num_buf, sizeof(s_last_selected_hint) - 1);
				}
				break;
			}

			prev_matched = nr_matched;
			continue;
		}

		// For undo operations, filter normally
		size_t before_filter = nr_matched;
		filter_hints(scr, num_buf, text_buf);

		if (nr_matched == 1 && (!is_numeric_mode || !last_input_was_letter)) {
			struct hint *h = &matched[0];

			platform->screen_clear(scr);

			int nx = h->x + h->w / 2;
			int ny = h->y + h->h / 2;

			platform->mouse_move(scr, nx + 1, ny + 1);
			platform->mouse_move(scr, nx, ny);

			if (is_numeric_mode) {
				snprintf(s_last_selected_hint, sizeof(s_last_selected_hint), "%d", h->original_index + 1);
			} else {
				strncpy(s_last_selected_hint, num_buf, sizeof(s_last_selected_hint) - 1);
			}
			break;
		}

		prev_matched = nr_matched;
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

	platform->mouse_hide();
	
	show_message(scr, "Detecting...", hint_h);
	
	int mx, my;
	platform->mouse_get_position(&scr, &mx, &my);
	draw_loading_cursor(scr, mx, my);
	platform->commit();

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

	/* Initialize detection context */
	struct detection_context ctx = {0};
	ctx.done = 0;
	ctx.result = NULL;
	
#ifdef _WIN32
	InitializeCriticalSection(&ctx.lock);
	ctx.thread = CreateThread(NULL, 0, detection_thread, &ctx, 0, NULL);
	if (!ctx.thread) {
		fprintf(stderr, "Failed to create detection thread\n");
		platform->input_ungrab_keyboard();
		platform->mouse_show();
		DeleteCriticalSection(&ctx.lock);
		return -1;
	}
#else
	pthread_mutex_init(&ctx.lock, NULL);
	if (pthread_create(&ctx.thread, NULL, detection_thread, &ctx) != 0) {
		fprintf(stderr, "Failed to create detection thread\n");
		platform->input_ungrab_keyboard();
		platform->mouse_show();
		pthread_mutex_destroy(&ctx.lock);
		return -1;
	}
#endif

	/* Keep drawing animated cursor while detection runs */
	while (1) {
		/* Check if detection is done */
		int done;
#ifdef _WIN32
		EnterCriticalSection(&ctx.lock);
		done = ctx.done;
		LeaveCriticalSection(&ctx.lock);
#else
		pthread_mutex_lock(&ctx.lock);
		done = ctx.done;
		pthread_mutex_unlock(&ctx.lock);
#endif
		
		if (done)
			break;
		
		/* Redraw message and animated cursor */
		platform->mouse_get_position(&scr, &mx, &my);
		show_message(scr, "Detecting...", hint_h);
		draw_loading_cursor(scr, mx, my);
		platform->commit();
		
		/* Sleep a bit to avoid excessive CPU usage */
#ifdef _WIN32
		Sleep(16);  /* ~60 FPS */
#else
		usleep(16000);
#endif
	}

	/* Wait for thread to finish */
#ifdef _WIN32
	WaitForSingleObject(ctx.thread, INFINITE);
	CloseHandle(ctx.thread);
	DeleteCriticalSection(&ctx.lock);
#else
	pthread_join(ctx.thread, NULL);
	pthread_mutex_destroy(&ctx.lock);
#endif

	/* Get result */
	struct ui_detection_result *result = ctx.result;

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
	for (size_t i = 0; i < hint_count; i++) {
		if (hint_array[i].element_name)
			free(hint_array[i].element_name);
	}
	free(hint_array);

	return rc;
}
