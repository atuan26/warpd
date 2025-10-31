/*
gcc -o ./atspi-detector atspi-detector.c -O0 -g -Wall -I/usr/include
-I/usr/X11R6/include `pkg-config --cflags glib-2.0 gobject-2.0 atk-bridge-2.0
atspi-2` -pthread -L/usr/X11R6/lib -lm `pkg-config --libs glib-2.0 gobject-2.0
atk-bridge-2.0 atspi-2`
*/
#include "atspi-detector.h"
#include <at-spi-2.0/atspi/atspi.h>
#include <glib-2.0/glib.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

GSList *element_list = NULL;

static gchar *get_label(AtspiAccessible *accessible)
{
	GArray *relations;
	AtspiRelation *relation;
	guint i;
	gchar *result = "";

	relations = atspi_accessible_get_relation_set(accessible, NULL);
	if (relations == NULL) {
		return "";
	}

	for (i = 0; i < relations->len; i++) {
		relation = g_array_index(relations, AtspiRelation *, i);

		if (atspi_relation_get_relation_type(relation) ==
		    ATSPI_RELATION_LABELLED_BY) {
			result = atspi_accessible_get_name(
			    atspi_relation_get_target(relation, 0), NULL);
		}
	}

	if (relations != NULL)
		g_array_free(relations, TRUE);

	return result;
}

static void get_rect(AtspiAccessible *accessible, gint *x, gint *y, gint *w,
		     gint *h)
{
	AtspiComponent *component = atspi_accessible_get_component(accessible);
	if (component != NULL) {
		AtspiRect *rect = atspi_component_get_extents(
		    component, ATSPI_COORD_TYPE_SCREEN, NULL);
		if (rect != NULL) {
			*x = rect->x;
			*y = rect->y;
			*w = rect->width;
			*h = rect->height;
			g_free(rect);
		}
		g_object_unref(component);
	}
}

static gboolean check_is_visible(AtspiStateSet *states)
{
	return atspi_state_set_contains(states, ATSPI_STATE_SHOWING) &&
	       atspi_state_set_contains(states, ATSPI_STATE_VISIBLE);
}

gboolean validate_role(char *role)
{
	if (strcmp(role, "panel") == 0 || strcmp(role, "section") == 0 ||
	    strcmp(role, "html container") == 0 || strcmp(role, "frame") == 0 ||
	    strcmp(role, "menu bar") == 0 || strcmp(role, "tool bar") == 0 ||
	    strcmp(role, "list") == 0 || strcmp(role, "page tab list") == 0 ||
	    strcmp(role, "description list") == 0 ||
	    strcmp(role, "scroll pane") == 0 || strcmp(role, "table") == 0 ||
	    strcmp(role, "grouping") == 0) {
		return FALSE;
	}
	return TRUE;
}

void print_info(ElementInfo *element)
{
	gchar *padding = g_strnfill(element->depth * 2, ' ');
	const char *role = element->role ? element->role : "unknown";
	const char *name = element->name ? element->name : "unknown";
	printf("%s%s - %s (x=%d y=%d w=%d h=%d)\n", padding, role, name,
	       element->x, element->y, element->w, element->h);
	g_free(padding);
}

static void collect_element_info(AtspiAccessible *accessible, gint depth,
				 gint x, gint y, gint w, gint h)
{
	gchar *raw_name = atspi_accessible_get_name(accessible, NULL);
	gchar *name = NULL;
	if (raw_name == NULL || g_strcmp0(raw_name, "") == 0) {
		raw_name = get_label(accessible);
		name = raw_name ? g_strdup(raw_name) : g_strdup("NULL");
	} else {
		name = g_strdup(raw_name);
	}

	gchar *raw_role = atspi_accessible_get_role_name(accessible, NULL);
	gchar *role_name = raw_role ? g_strdup(raw_role) : g_strdup("");

	if (validate_role(role_name) && x > 0 && y > 0) {
		ElementInfo *element = g_new0(ElementInfo, 1);
		element->x = x;
		element->y = y;
		element->w = w;
		element->h = h;
		element->name = name;
		element->role = role_name;
		element->depth = depth;
		element_list = g_slist_append(element_list, element);
	}
}

static void dump_node_content(AtspiAccessible *node, gint dept)
{
	AtspiAccessible *inner_node = NULL;
	gint c;
	gint x = -1, y = -1, w = -1, h = -1;

	if (node == NULL) {
		return;
	}

	AtspiStateSet *states = atspi_accessible_get_state_set(node);
	if (states == NULL) {
		return;
	}

	gboolean is_visible = check_is_visible(states);
	g_object_unref(states);
	if (!is_visible) {
		return;
	}

	get_rect(node, &x, &y, &w, &h);
	if (x == -1 && y == -1 && w == -1 && h == -1)
		return;

	collect_element_info(node, dept, x, y, w, h);

	for (c = 0; c < atspi_accessible_get_child_count(node, NULL); c++) {
		inner_node = atspi_accessible_get_child_at_index(node, c, NULL);
		dump_node_content(inner_node, dept + 1);
		g_object_unref(inner_node);
	}
}

static void free_element(ElementInfo *element)
{
	if (!element)
		return;
	g_free(element->name);
	g_free(element->role);
	g_free(element);
}

static void free_element_list()
{
	g_slist_free_full(element_list, (GDestroyNotify)free_element);
	element_list = NULL;
}

static AtspiAccessible *get_active_window(void)
{
	gint i;
	AtspiAccessible *desktop = NULL;
	AtspiAccessible *active_window = NULL;

	desktop = atspi_get_desktop(0);
	if (!desktop)
		return NULL;

	for (i = 0; i < atspi_accessible_get_child_count(desktop, NULL); i++) {
		AtspiAccessible *app =
		    atspi_accessible_get_child_at_index(desktop, i, NULL);
		if (!app)
			continue;

		gint window_count = atspi_accessible_get_child_count(app, NULL);

		for (gint j = 0; j < window_count; j++) {
			AtspiAccessible *window =
			    atspi_accessible_get_child_at_index(app, j, NULL);
			if (!window)
				continue;

			AtspiStateSet *states =
			    atspi_accessible_get_state_set(window);
			if (states && atspi_state_set_contains(states,
						     ATSPI_STATE_ACTIVE)) {
				active_window = g_object_ref(window);
				g_object_unref(states);
				g_object_unref(window);
				g_object_unref(app);
				g_object_unref(desktop);
				return active_window;
			}
			if (states)
				g_object_unref(states);
			g_object_unref(window);
		}
		g_object_unref(app);
	}
	g_object_unref(desktop);
	return NULL;
}

void deduplicate_elements_by_position(GSList **element_list_ptr)
{
	if (!element_list_ptr || !*element_list_ptr)
		return;

	// Use a safer approach that builds a new list from valid elements only
	GHashTable *position_map =
	    g_hash_table_new(g_direct_hash, g_direct_equal);
	GSList *last_occurrence_map = NULL;
	GSList *iter;

	// First pass: identify the last occurrence of each position
	for (iter = *element_list_ptr; iter; iter = iter->next) {
		ElementInfo *element = iter->data;
		if (!element)
			continue;

		guint64 pos_key =
		    ((guint64)element->x << 32) | (guint64)element->y;
		g_hash_table_insert(position_map, GUINT_TO_POINTER(pos_key),
				    element);
	}

	// Second pass: collect elements in original order, but only keep the
	// ones that match the last occurrence of their position
	for (iter = *element_list_ptr; iter; iter = iter->next) {
		ElementInfo *element = iter->data;
		if (!element)
			continue;

		guint64 pos_key =
		    ((guint64)element->x << 32) | (guint64)element->y;
		if (g_hash_table_lookup(position_map,
					GUINT_TO_POINTER(pos_key)) == element) {
			last_occurrence_map =
			    g_slist_append(last_occurrence_map, element);
		}
	}

	// Replace the original list
	g_slist_free(*element_list_ptr);
	*element_list_ptr = last_occurrence_map;

	g_hash_table_destroy(position_map);
}

void atspi_init_detector(void) { atspi_init(); }

void free_detector_resources(void) { free_element_list(); }

GSList *detect_elements()
{
	AtspiAccessible *active_window = get_active_window();
	if (!active_window) {
		fprintf(stderr, "Warning: No active window found for smart hint detection\n");
		return NULL;
	}

	dump_node_content(active_window, 0);
	g_object_unref(active_window);

	deduplicate_elements_by_position(&element_list);
	return element_list;
}
