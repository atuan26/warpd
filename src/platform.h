/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * © 2019 Raheman Vaiya (see: LICENSE).
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stdlib.h>

#define PLATFORM_MOD_CONTROL 1
#define PLATFORM_MOD_SHIFT 2
#define PLATFORM_MOD_META 4
#define PLATFORM_MOD_ALT 8

#define SCROLL_DOWN 1
#define SCROLL_RIGHT 2
#define SCROLL_LEFT 3
#define SCROLL_UP 4

#define MAX_HINTS 2048
#define MAX_SCREENS 32
#define MAX_UI_ELEMENTS 512

struct input_event {
	uint8_t code;
	uint8_t mods;
	uint8_t pressed;
};

struct hint {
	int x;
	int y;

	int w;
	int h;

	char label[16];
};

/* UI element detected by accessibility APIs or computer vision */
struct ui_element {
	int x;           /* X coordinate on screen */
	int y;           /* Y coordinate on screen */
	int w;           /* Width of element */
	int h;           /* Height of element */
	char *name;      /* Element name/label (may be NULL) */
	char *role;      /* Element role/type (may be NULL) */
};

/* Result of UI element detection */
struct ui_detection_result {
	struct ui_element *elements;
	size_t count;
	int error;               /* 0 = success, negative = error */
	char error_msg[256];     /* Human-readable error message */
};

/* Forward declarations */
struct screen;
typedef struct screen *screen_t;

/* Common UI utility functions */
void remove_overlapping_elements(struct ui_detection_result *result);
void show_message(screen_t scr, const char *message, int hint_h);
void draw_loading_cursor(screen_t scr, int x, int y);
void draw_target_cursor(screen_t scr, int x, int y);

struct platform {
	/* Input */

	void (*input_grab_keyboard)();
	void (*input_ungrab_keyboard)();

	struct input_event *(*input_next_event)(int timeout);
	uint8_t (*input_lookup_code)(const char *name, int *shifted);
	const char *(*input_lookup_name)(uint8_t code, int shifted);

	/*
	 * Efficiently listen for one or more input events before
	 * grabbing the keyboard (including the event itself)
	 * and returning the matched event.
	 */
	struct input_event *(*input_wait)(struct input_event *events, size_t sz);

	void (*mouse_move)(screen_t scr, int x, int y);
	void (*mouse_down)(int btn);

	void (*mouse_up)(int btn);
	void (*mouse_click)(int btn);

	void (*mouse_get_position)(screen_t *scr, int *x, int *y);
	void (*mouse_show)();
	void (*mouse_hide)();

	void (*screen_get_dimensions)(screen_t scr, int *w, int *h);
	void (*screen_draw_box)(screen_t scr, int x, int y, int w, int h, const char *color);
	void (*screen_clear)(screen_t scr);
	void (*screen_list)(screen_t scr[MAX_SCREENS], size_t *n);

	void (*init_hint)(const char *bg, const char *fg, int border_radius, const char *font_family);

	/* 
	 * Modifications to files passed into this function will interrupt
	 * input_wait (which returns NULL).
	 */
	void (*monitor_file)(const char *path);

	/* Hints are centered around the provided x,y coordinates. */
	void (*hint_draw)(struct screen *scr, struct hint *hints, size_t n);

	void (*scroll)(int direction);

	void (*copy_selection)();

	/*
	 * UI Element Detection for Smart Hint Mode
	 *
	 * Detect interactive UI elements in the active window.
	 * Implementation can use:
	 *   - Linux: AT-SPI (primary) or OpenCV (fallback)
	 *   - macOS: Accessibility API
	 *   - Windows: UI Automation
	 *
	 * Returns: ui_detection_result with elements array (must be freed with free_ui_elements)
	 *          NULL if detection not supported on this platform
	 */
	struct ui_detection_result *(*detect_ui_elements)();
	
	/*
	 * Insert text mode - shows dialog, allows editing, and pastes result
	 * 
	 * Workflow:
	 * 1. Copies current selection to clipboard
	 * 2. Shows text input dialog pre-filled with clipboard content
	 * 3. If user submits (Enter), pastes the edited text
	 * 4. If user cancels (Escape), does nothing
	 * 
	 * Returns 1 if text was inserted, 0 if cancelled
	 */
	int (*insert_text_mode)(screen_t scr);
	
	void (*send_paste)();

	/*
	 * Free UI detection result returned by detect_ui_elements()
	 */
	void (*free_ui_elements)(struct ui_detection_result *result);

	/*
	* Draw operations may (or may not) be queued until this function
	* is called.
	*/
	void (*commit)();
};

void platform_run(int (*main) (struct platform *platform));
#endif
