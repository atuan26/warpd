#include "atspi-detector.h"
#include "platform.h"
#include "warpd.h"
#include <at-spi-2.0/atspi/atspi.h>
#include <glib-2.0/glib.h>
#include <glib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static struct hint *hints;
static struct hint matched[MAX_HINTS];

static size_t nr_hints = 0;
static size_t nr_matched;

char s_last_selected_hint[32];

static void filter(screen_t scr, const char *s)
{
	size_t i;

	nr_matched = 0;
	for (i = 0; i < nr_hints; i++) {
		if (s && strncasecmp(hints[i].label, s, strlen(s)) == 0)
			matched[nr_matched++] = hints[i];
	}

	platform->screen_clear(scr);
	platform->hint_draw(scr, matched, nr_matched);
	platform->commit();
}

static void get_hint_size(screen_t scr, int *w, int *h)
{
	int sw, sh;

	platform->screen_get_dimensions(scr, &sw, &sh);

	if (sw < sh) {
		int tmp = sw;
		sw = sh;
		sh = tmp;
	}

	*w = (sw * config_get_int("hint_size")) / 1000;
	*h = (sh * config_get_int("hint_size")) / 1000;
}

static void generate_labels(struct hint *hints, size_t num_elements)
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

static struct hint *convert_to_hints(int w, int h, size_t *nr_hints)
{
	GSList *element_list = detect_elements();
	if (!element_list) {
		*nr_hints = 0;
		return NULL;
	}

	*nr_hints = g_slist_length(element_list);
	if (*nr_hints == 0) {
		return NULL;
	}

	struct hint *hints = calloc(*nr_hints, sizeof(struct hint));
	if (!hints) {
		*nr_hints = 0;
		return NULL;
	}

	GSList *iter = element_list;
	for (size_t i = 0; i < *nr_hints; i++, iter = iter->next) {
		if (!iter || !iter->data) {
			free(hints);
			*nr_hints = 0;
			return NULL;
		}

		struct ElementInfo *element = iter->data;
		hints[i].x = element->x;
		hints[i].y = element->y;
		hints[i].w = w;
		hints[i].h = h;
	}
	free_detector_resources();
	generate_labels(hints, *nr_hints);
	return hints;
}

static int hint_selection(screen_t scr, struct hint *_hints, size_t _nr_hints)
{
	hints = _hints;
	nr_hints = _nr_hints;
	if (nr_hints == 0){
	    return -1;
	}

	filter(scr, "");

	int rc = 0;
	char buf[32] = {0};
	platform->input_grab_keyboard();

	platform->mouse_hide();

	const char *keys[] = {
	    "smart_hint_exit",
	    "hint_undo_all",
	    "hint_undo",
	};

	config_input_whitelist(keys, sizeof keys / sizeof keys[0]);

	while (1) {
		struct input_event *ev;
		ssize_t len;

		ev = platform->input_next_event(0);

		if (!ev->pressed)
			continue;

		len = strlen(buf);

		if (config_input_match(ev, "smart_hint_exit")) {
			rc = -1;
			break;
		} else if (config_input_match(ev, "hint_undo_all")) {
			buf[0] = 0;
		} else if (config_input_match(ev, "hint_undo")) {
		if (len)
			buf[len - 1] = 0;
		} else {
			const char *name = input_event_tostr(ev);

			if (!name || name[1])
				continue;

			buf[len++] = name[0];
		}

		filter(scr, buf);

		if (nr_matched == 1) {
			int nx, ny;
			struct hint *h = &matched[0];

			platform->screen_clear(scr);

			nx = h->x + h->w / 2;
			ny = h->y + h->h / 2;

			platform->mouse_move(scr, nx + 1, ny + 1);

			platform->mouse_move(scr, nx, ny);
			strcpy(s_last_selected_hint, buf);
			break;
		} else if (nr_matched == 0) {
			break;
		}
	}

	platform->input_ungrab_keyboard();
	platform->screen_clear(scr);
	platform->mouse_show();

	platform->commit();
	return rc;
}

int smart_hint_mode()
{
	atspi_init_detector();

	screen_t scr;
	struct hint *hints = NULL;
	int sw, sh;

	platform->mouse_get_position(&scr, NULL, NULL);
	platform->screen_get_dimensions(scr, &sw, &sh);

	int w, h;
	get_hint_size(scr, &w, &h);

	hints = convert_to_hints(w, h, &nr_hints);
	if (!hints || nr_hints == 0) {
		fprintf(stderr, "Smart hint mode: No interactive elements detected\n");
		if (hints)
			free(hints);
		return -1;
	}

	int result = hint_selection(scr, hints, nr_hints);
	free(hints);
	return result;
}
