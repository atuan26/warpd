/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#include <limits.h>
#include "wayland.h"

/* UI element detector functions (implemented in ui_detector.c) */
extern struct ui_detection_result *linux_detect_ui_elements(void);
extern void linux_free_ui_elements(struct ui_detection_result *result);

static void wayland_send_paste(void);

/* Insert text mode - uses zenity for text input */
static int wayland_insert_text_mode(screen_t scr)
{
	fprintf(stderr, "\n=== INSERT MODE (Wayland) ===\n");
	
	way_screen_clear(scr);
	way_commit();
	
	FILE *fp = popen("zenity --entry --title='Insert Text' --text='Type text and press OK:' 2>/dev/null", "r");
	if (!fp) {
		fprintf(stderr, "ERROR: zenity not found. Install: sudo apt install zenity\n");
		return 0;
	}
	
	char text_buffer[1024] = {0};
	if (fgets(text_buffer, sizeof(text_buffer), fp) != NULL) {
		size_t len = strlen(text_buffer);
		if (len > 0 && text_buffer[len-1] == '\n') {
			text_buffer[len-1] = '\0';
		}
		
		int status = pclose(fp);
		
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && text_buffer[0] != '\0') {
			FILE *clip = popen("wl-copy 2>/dev/null", "w");
			if (clip) {
				fputs(text_buffer, clip);
				pclose(clip);
				usleep(100000);
				wayland_send_paste();
				return 1;
			} else {
				fprintf(stderr, "ERROR: wl-clipboard not found. Install: sudo apt install wl-clipboard\n");
			}
		}
	} else {
		pclose(fp);
	}
	
	return 0;
}

/* Send paste key (Ctrl+V) - copy already exists as way_copy_selection */
static void wayland_send_paste()
{
	/* TODO: Implement using Wayland virtual keyboard protocol
	 * Requires zwp_virtual_keyboard_v1 protocol support
	 * Similar to how mouse events are sent via zwlr_virtual_pointer_v1
	 */
	fprintf(stderr, "Paste (Ctrl+V): Not yet implemented for Wayland\n");
	fprintf(stderr, "Workaround: Use system paste shortcut manually\n");
}

#define UNIMPLEMENTED { \
	fprintf(stderr, "FATAL: wayland: %s unimplemented\n", __func__); \
	exit(-1);							 \
}

static uint8_t btn_state[3] = {0};

static struct {
	const char *name;
	const char *xname;
} normalization_map[] = {
	{"esc", "Escape"},
	{",", "comma"},
	{".", "period"},
	{"-", "minus"},
	{"/", "slash"},
	{";", "semicolon"},
	{"$", "dollar"},
	{"backspace", "BackSpace"},
};

struct ptr ptr = {0};

/* Input */

uint8_t way_input_lookup_code(const char *name, int *shifted)
{
	size_t i;

	for (i = 0; i < sizeof normalization_map / sizeof normalization_map[0]; i++)
		if (!strcmp(normalization_map[i].name, name))
			name = normalization_map[i].xname;

	for (i = 0; i < 256; i++)
		if (!strcmp(keymap[i].name, name)) {
			*shifted = 0;
			return i;
		} else if (!strcmp(keymap[i].shifted_name, name)) {
			*shifted = 1;
			return i;
		}

	return 0;
}

const char *way_input_lookup_name(uint8_t code, int shifted)
{
	size_t i;
	const char *name = NULL;

	if (shifted && keymap[code].shifted_name[0])
		name = keymap[code].shifted_name;
	else if (!shifted && keymap[code].name[0])
		name = keymap[code].name;
	
	for (i = 0; i < sizeof normalization_map / sizeof normalization_map[0]; i++)
		if (name && !strcmp(normalization_map[i].xname, name))
			name = normalization_map[i].name;

	return name;
}

void way_mouse_move(struct screen *scr, int x, int y)
{
	size_t i;
	int maxx = INT_MIN;
	int maxy = INT_MIN;
	int minx = INT_MAX;
	int miny = INT_MAX;

	ptr.x = x;
	ptr.y = y;
	ptr.scr = scr;

	for (i = 0; i < nr_screens; i++) {
		int x = screens[i].x + screens[i].w;
		int y = screens[i].y + screens[i].h;

		if (screens[i].y < miny)
			miny = screens[i].y;
		if (screens[i].x < minx)
			minx = screens[i].x;

		if (y > maxy)
			maxy = y;
		if (x > maxx)
			maxx = x;
	}

	/*
	 * Virtual pointer space always beings at 0,0, while global compositor
	 * space may have a negative real origin :/.
	 */
	zwlr_virtual_pointer_v1_motion_absolute(wl.ptr, 0,
						wl_fixed_from_int(x+scr->x-minx),
						wl_fixed_from_int(y+scr->y-miny),
						wl_fixed_from_int(maxx-minx),
						wl_fixed_from_int(maxy-miny));
	zwlr_virtual_pointer_v1_frame(wl.ptr);

	wl_display_flush(wl.dpy);
}

#define normalize_btn(btn) \
	switch (btn) { \
		case 1: btn = 272;break; \
		case 2: btn = 274;break; \
		case 3: btn = 273;break; \
	}

void way_mouse_down(int btn)
{
	assert(btn < (int)(sizeof btn_state / sizeof btn_state[0]));
	btn_state[btn-1] = 1;
	normalize_btn(btn);
	zwlr_virtual_pointer_v1_button(wl.ptr, 0, btn, 1);
}

void way_mouse_up(int btn)
{
	assert(btn < (int)(sizeof btn_state / sizeof btn_state[0]));
	btn_state[btn-1] = 0;
	normalize_btn(btn);
	zwlr_virtual_pointer_v1_button(wl.ptr, 0, btn, 0);
}

void way_mouse_click(int btn)
{
	normalize_btn(btn);

	zwlr_virtual_pointer_v1_button(wl.ptr, 0, btn, 1);
	zwlr_virtual_pointer_v1_button(wl.ptr, 0, btn, 0);
	zwlr_virtual_pointer_v1_frame(wl.ptr);

	wl_display_flush(wl.dpy);
}

void way_mouse_get_position(struct screen **scr, int *x, int *y)
{
	if (scr)
		*scr = ptr.scr;
	if (x)
		*x = ptr.x;
	if (y)
		*y = ptr.y;
}

void way_mouse_show()
{
}

void way_mouse_hide()
{
	fprintf(stderr, "wayland: mouse hiding not implemented\n");
}

void way_scroll(int direction)
{
	int axis = 0;  /* 0 = vertical, 1 = horizontal */
	int dir = 1;
	
	switch (direction) {
	case SCROLL_DOWN:
		axis = 0;
		dir = 1;
		break;
	case SCROLL_UP:
		axis = 0;
		dir = -1;
		break;
	case SCROLL_RIGHT:
		axis = 1;
		dir = 1;
		break;
	case SCROLL_LEFT:
		axis = 1;
		dir = -1;
		break;
	}

	zwlr_virtual_pointer_v1_axis_discrete(wl.ptr, 0, axis,
					      wl_fixed_from_int(15*dir),
					      dir);

	zwlr_virtual_pointer_v1_frame(wl.ptr);

	wl_display_flush(wl.dpy);
}

void way_copy_selection() { UNIMPLEMENTED }
struct input_event *way_input_wait(struct input_event *events, size_t sz) { UNIMPLEMENTED }

void way_screen_list(struct screen *scr[MAX_SCREENS], size_t *n)
{
	size_t i;
	for (i = 0; i < nr_screens; i++)
		scr[i] = &screens[i];

	*n = nr_screens;
}

void way_monitor_file(const char *path) { UNIMPLEMENTED }

void way_commit()
{
}

static void cleanup()
{
	if (btn_state[0])
		zwlr_virtual_pointer_v1_button(wl.ptr, 0, 272, 0);
	if (btn_state[1])
		zwlr_virtual_pointer_v1_button(wl.ptr, 0, 274, 0);
	if (btn_state[2])
		zwlr_virtual_pointer_v1_button(wl.ptr, 0, 273, 0);
	wl_display_flush(wl.dpy);
}

void wayland_init(struct platform *platform)
{
	way_init();

	platform->monitor_file = way_monitor_file;

	atexit(cleanup);

	platform->commit = way_commit;
	platform->copy_selection = way_copy_selection;
	platform->hint_draw = way_hint_draw;
	platform->init_hint = way_init_hint;
	platform->input_grab_keyboard = way_input_grab_keyboard;
	platform->input_lookup_code = way_input_lookup_code;
	platform->input_lookup_name = way_input_lookup_name;
	platform->input_next_event = way_input_next_event;
	platform->input_ungrab_keyboard = way_input_ungrab_keyboard;
	platform->input_wait = way_input_wait;
	platform->mouse_click = way_mouse_click;
	platform->mouse_down = way_mouse_down;
	platform->mouse_get_position = way_mouse_get_position;
	platform->mouse_hide = way_mouse_hide;
	platform->mouse_move = way_mouse_move;
	platform->mouse_show = way_mouse_show;
	platform->mouse_up = way_mouse_up;
	platform->screen_clear = way_screen_clear;
	platform->screen_draw_box = way_screen_draw_box;
	platform->screen_get_dimensions = way_screen_get_dimensions;
	platform->screen_get_offset = way_screen_get_offset;
	platform->screen_list = way_screen_list;
	platform->scroll = way_scroll;

	/* UI element detection for smart hint mode */
	platform->detect_ui_elements = linux_detect_ui_elements;
	platform->free_ui_elements = linux_free_ui_elements;
	
	/* Insert text mode */
	platform->insert_text_mode = wayland_insert_text_mode;
	
	/* Paste key (copy already exists as way_copy_selection) */
	platform->send_paste = wayland_send_paste;
}
