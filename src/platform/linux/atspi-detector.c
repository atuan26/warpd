/*
gcc -o ./atspi-detector atspi-detector.c -O0 -g -Wall -I/usr/include
-I/usr/X11R6/include `pkg-config --cflags glib-2.0 gobject-2.0 atk-bridge-2.0
atspi-2` -pthread -L/usr/X11R6/lib -lm `pkg-config --libs glib-2.0 gobject-2.0
atk-bridge-2.0 atspi-2`
*/
#include "atspi-detector.h"
#include "../../platform.h"
#include <at-spi-2.0/atspi/atspi.h>
#include <glib-2.0/glib.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Import config functions */
extern const char *config_get(const char *key);
extern int config_get_int(const char *key);

/* Helper macros */
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

GSList *element_list = NULL;
static gint max_depth_reached = 0;

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

/**
 * Check if element is actually visible within the active window bounds
 * This handles clipping by parent containers and window boundaries
 */
static gboolean check_is_actually_visible(AtspiAccessible *accessible, AtspiAccessible *window)
{
	gint elem_x, elem_y, elem_w, elem_h;
	gint win_x, win_y, win_w, win_h;
	
	/* Get element coordinates */
	get_rect(accessible, &elem_x, &elem_y, &elem_w, &elem_h);
	if (elem_w <= 0 || elem_h <= 0) {
		return FALSE;
	}
	
	/* Get window coordinates */
	get_rect(window, &win_x, &win_y, &win_w, &win_h);
	if (win_w <= 0 || win_h <= 0) {
		return TRUE; /* If we can't get window bounds, assume visible */
	}
	
	/* Check if element is completely outside window bounds */
	if (elem_x >= win_x + win_w ||  /* Element starts after window ends */
	    elem_y >= win_y + win_h ||  /* Element starts below window */
	    elem_x + elem_w <= win_x || /* Element ends before window starts */
	    elem_y + elem_h <= win_y) { /* Element ends above window */
		return FALSE;
	}
	
	/* Check if element has reasonable overlap with window */
	gint overlap_x = MAX(elem_x, win_x);
	gint overlap_y = MAX(elem_y, win_y);
	gint overlap_w = MIN(elem_x + elem_w, win_x + win_w) - overlap_x;
	gint overlap_h = MIN(elem_y + elem_h, win_y + win_h) - overlap_y;
	
	/* Require at least 50% of element to be visible, or configurable minimum area */
	gint visible_area = overlap_w * overlap_h;
	gint total_area = elem_w * elem_h;
	gint min_visible_area = 100;  /* Default */
	
	/* Get configurable minimum visible area */
	min_visible_area = config_get_int("ui_min_visible_area");
	
	return (visible_area >= total_area / 2) || (visible_area >= min_visible_area);
}

/* Optimized role validation using hash table for faster lookup */
static GHashTable *excluded_roles = NULL;

static void init_excluded_roles(void)
{
	if (excluded_roles)
		return;
		
	excluded_roles = g_hash_table_new(g_str_hash, g_str_equal);
	g_hash_table_insert(excluded_roles, "panel", GINT_TO_POINTER(1));
	g_hash_table_insert(excluded_roles, "section", GINT_TO_POINTER(1));
	g_hash_table_insert(excluded_roles, "html container", GINT_TO_POINTER(1));
	g_hash_table_insert(excluded_roles, "frame", GINT_TO_POINTER(1));
	g_hash_table_insert(excluded_roles, "menu bar", GINT_TO_POINTER(1));
	g_hash_table_insert(excluded_roles, "tool bar", GINT_TO_POINTER(1));
	g_hash_table_insert(excluded_roles, "list", GINT_TO_POINTER(1));
	g_hash_table_insert(excluded_roles, "page tab list", GINT_TO_POINTER(1));
	g_hash_table_insert(excluded_roles, "description list", GINT_TO_POINTER(1));
	g_hash_table_insert(excluded_roles, "scroll pane", GINT_TO_POINTER(1));
	g_hash_table_insert(excluded_roles, "table", GINT_TO_POINTER(1));
	g_hash_table_insert(excluded_roles, "grouping", GINT_TO_POINTER(1));
}

gboolean validate_role(char *role)
{
	if (!role)
		return FALSE;
		
	init_excluded_roles();
	return !g_hash_table_contains(excluded_roles, role);
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

static FILE *g_dump_fp = NULL;

static void dump_element_tree(AtspiAccessible *node, gint depth, gint max_depth)
{
	if (!node || !g_dump_fp || depth > max_depth)
		return;
	
	for (gint i = 0; i < depth * 2; i++)
		fprintf(g_dump_fp, " ");
	
	gchar *name = atspi_accessible_get_name(node, NULL);
	gchar *role = atspi_accessible_get_role_name(node, NULL);
	gchar *desc = atspi_accessible_get_description(node, NULL);
	
	AtspiComponent *component = atspi_accessible_get_component(node);
	gint x = 0, y = 0, w = 0, h = 0;
	if (component) {
		AtspiRect *rect = atspi_component_get_extents(component, ATSPI_COORD_TYPE_SCREEN, NULL);
		if (rect) {
			x = rect->x;
			y = rect->y;
			w = rect->width;
			h = rect->height;
			g_free(rect);
		}
		g_object_unref(component);
	}
	
	fprintf(g_dump_fp, "[%s] name='%s' x=%d y=%d w=%d h=%d",
		role ? role : "unknown",
		name ? name : "",
		x, y, w, h);
	
	if (desc && desc[0])
		fprintf(g_dump_fp, " desc='%s'", desc);
	
	fprintf(g_dump_fp, "\n");
	
	if (name) g_free(name);
	if (role) g_free(role);
	if (desc) g_free(desc);
	
	gint child_count = atspi_accessible_get_child_count(node, NULL);
	for (gint i = 0; i < child_count; i++) {
		AtspiAccessible *child = atspi_accessible_get_child_at_index(node, i, NULL);
		if (child) {
			dump_element_tree(child, depth + 1, max_depth);
			g_object_unref(child);
		}
	}
}

static void collect_element_info(AtspiAccessible *accessible, gint depth,
				 gint x, gint y, gint w, gint h)
{
	gchar *raw_name = atspi_accessible_get_name(accessible, NULL);
	gchar *label = get_label(accessible);
	gchar *description = atspi_accessible_get_description(accessible, NULL);
	
	static int debug_count = 0;
	if (debug_count < 5) {
		// fprintf(stderr, "DEBUG RAW [Linux AT-SPI]: Name='%s' Label='%s' Description='%s'\n",
		// 	raw_name ? raw_name : "(null)",
		// 	label ? label : "(null)",
		// 	description ? description : "(null)");
		debug_count++;
	}
	
	gchar *name = NULL;
	if (raw_name == NULL || g_strcmp0(raw_name, "") == 0) {
		name = label ? g_strdup(label) : g_strdup("NULL");
	} else {
		name = g_strdup(raw_name);
	}

	gchar *raw_role = atspi_accessible_get_role_name(accessible, NULL);
	gchar *role_name = raw_role ? g_strdup(raw_role) : g_strdup("");
	
	if (description) g_free(description);

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

static void dump_node_content(AtspiAccessible *node, gint dept, gint max_depth, gint max_elements, AtspiAccessible *window)
{
	AtspiAccessible *inner_node = NULL;
	gint c;
	gint x = -1, y = -1, w = -1, h = -1;

	if (node == NULL || dept > max_depth) {
		return;
	}
	
	/* Track maximum depth actually reached */
	if (dept > max_depth_reached) {
		max_depth_reached = dept;
	}
	
	/* Early termination if we have enough elements */
	if (g_slist_length(element_list) >= max_elements) {
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
	
	/* Additional check for actual visibility within window bounds */
	if (!check_is_actually_visible(node, window)) {
		/* Uncomment for debugging clipped elements:
		gint x, y, w, h;
		get_rect(node, &x, &y, &w, &h);
		fprintf(stderr, "AT-SPI: Skipping clipped element at (%d,%d) %dx%d\n", x, y, w, h);
		*/
		return;
	}

	get_rect(node, &x, &y, &w, &h);
	if (x == -1 && y == -1 && w == -1 && h == -1)
		return;

	collect_element_info(node, dept, x, y, w, h);

	for (c = 0; c < atspi_accessible_get_child_count(node, NULL); c++) {
		/* Early termination check */
		if (g_slist_length(element_list) >= max_elements) {
			break;
		}
		
		inner_node = atspi_accessible_get_child_at_index(node, c, NULL);
		dump_node_content(inner_node, dept + 1, max_depth, max_elements, window);
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

void free_detector_resources(void) 
{ 
	free_element_list(); 
	
	/* Cleanup hash table */
	if (excluded_roles) {
		g_hash_table_destroy(excluded_roles);
		excluded_roles = NULL;
	}
}

void atspi_cleanup(void)
{
	fprintf(stderr, "AT-SPI: Cleaning up resources\n");
	free_detector_resources();
}

GSList *detect_elements()
{
	AtspiAccessible *active_window = get_active_window();
	if (!active_window) {
		fprintf(stderr, "Warning: No active window found for smart hint detection\n");
		return NULL;
	}

	/* Add timing for performance monitoring */
	GTimer *timer = g_timer_new();
	g_timer_start(timer);
	
	/* Reset depth tracking */
	max_depth_reached = 0;
	
	/* Get configurable values from config system */
	gint max_depth;  /* Default value */
	gint max_elements;  /* Default value */
	
	/* Use config system to get actual values */
	max_depth = config_get_int("ui_max_depth");
	max_elements = config_get_int("ui_max_elements");
	
	dump_node_content(active_window, 0, max_depth, max_elements, active_window);
	g_object_unref(active_window);

	g_timer_stop(timer);
	gdouble elapsed = g_timer_elapsed(timer, NULL);
	fprintf(stderr, "AT-SPI: Collection took %.2f ms (depth: %d/%d, elements: %d, limit: %d)\n", 
	        elapsed * 1000, max_depth_reached, max_depth, g_slist_length(element_list), max_elements);
	
	/* Suggest increasing depth if we hit the limit */
	if (max_depth_reached >= max_depth) {
		fprintf(stderr, "AT-SPI: Hit max depth limit! Consider increasing ui_max_depth for more hints\n");
	}
	g_timer_destroy(timer);

	deduplicate_elements_by_position(&element_list);
	return element_list;
}

/* Frame/Area Navigation Support
 * Detects scrollable areas and focusable frames WITHIN the active window.
 * This is different from window switching - it navigates between panes/frames.
 */

/* Check if element is a scrollable/focusable frame */
static gboolean is_focusable_frame(AtspiAccessible *element)
{
	AtspiRole role = atspi_accessible_get_role(element, NULL);
	
	/* Roles that typically represent scrollable/focusable areas */
	switch (role) {
		case ATSPI_ROLE_SCROLL_PANE:
		case ATSPI_ROLE_VIEWPORT:
		case ATSPI_ROLE_PANEL:
		case ATSPI_ROLE_FRAME:
		case ATSPI_ROLE_DOCUMENT_FRAME:
		case ATSPI_ROLE_DOCUMENT_WEB:
		case ATSPI_ROLE_TEXT:
		case ATSPI_ROLE_TERMINAL:
		case ATSPI_ROLE_LIST:
		case ATSPI_ROLE_TREE:
		case ATSPI_ROLE_TABLE:
		case ATSPI_ROLE_TREE_TABLE:
		case ATSPI_ROLE_CANVAS:
		case ATSPI_ROLE_INTERNAL_FRAME:
			return TRUE;
		default:
			break;
	}
	
	/* Also check if element is focusable and has significant size */
	AtspiStateSet *states = atspi_accessible_get_state_set(element);
	if (states) {
		gboolean focusable = atspi_state_set_contains(states, ATSPI_STATE_FOCUSABLE);
		g_object_unref(states);
		if (focusable)
			return TRUE;
	}
	
	return FALSE;
}

/* Recursively collect focusable frames within element */
static void collect_frames(AtspiAccessible *element, GSList **frame_list, int depth, int max_depth)
{
	if (depth > max_depth || !element)
		return;

	/* Check if this element is a focusable frame */
	if (is_focusable_frame(element)) {
		gint x = 0, y = 0, w = 0, h = 0;
		get_rect(element, &x, &y, &w, &h);
		
		/* Only include frames with reasonable size */
		if (w >= 50 && h >= 50) {
			WindowInfo *info = g_new0(WindowInfo, 1);
			info->name = atspi_accessible_get_name(element, NULL);
			
			/* Get role as app name for display */
			AtspiRole role = atspi_accessible_get_role(element, NULL);
			info->app_name = g_strdup(atspi_role_get_name(role));
			
			info->x = x;
			info->y = y;
			info->w = w;
			info->h = h;
			info->window_ref = g_object_ref(element);
			
			*frame_list = g_slist_append(*frame_list, info);
		}
	}
	
	/* Recurse into children */
	gint child_count = atspi_accessible_get_child_count(element, NULL);
	for (gint i = 0; i < child_count && i < 50; i++) {
		AtspiAccessible *child = atspi_accessible_get_child_at_index(element, i, NULL);
		if (child) {
			collect_frames(child, frame_list, depth + 1, max_depth);
			g_object_unref(child);
		}
	}
}

GSList *get_all_windows(void)
{
	GSList *frame_list = NULL;
	
	/* Get the active window and collect frames within it */
	AtspiAccessible *active = get_active_window();
	if (!active) {
		fprintf(stderr, "No active window found for frame detection\n");
		return NULL;
	}
	
	/* Collect focusable frames within the active window (max depth 8) */
	collect_frames(active, &frame_list, 0, 8);
	g_object_unref(active);
	
	/* Remove duplicate/overlapping frames */
	/* TODO: deduplicate by position */
	
	return frame_list;
}

void focus_window(WindowInfo *win)
{
	if (!win || !win->window_ref)
		return;

	AtspiAccessible *element = (AtspiAccessible *)win->window_ref;
	
	/* First try AT-SPI grab_focus */
	AtspiComponent *component = atspi_accessible_get_component(element);
	if (component) {
		atspi_component_grab_focus(component, NULL);
		g_object_unref(component);
	}
	
	/* For i3/tiling WMs: also click to focus using coordinates */
	int center_x = win->x + win->w / 2;
	int center_y = win->y + win->h / 2;
	
	/* Use xdotool to move mouse and click (works with i3) */
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "xdotool mousemove %d %d click 1 2>/dev/null", center_x, center_y);
	system(cmd);
}

static void free_window_info(WindowInfo *info)
{
	if (!info)
		return;
	if (info->name)
		g_free(info->name);
	if (info->app_name)
		g_free(info->app_name);
	if (info->window_ref)
		g_object_unref(info->window_ref);
	g_free(info);
}

void free_window_list(GSList *list)
{
	g_slist_free_full(list, (GDestroyNotify)free_window_info);
}
