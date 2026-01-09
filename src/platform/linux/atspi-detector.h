#ifndef ATSPI_DETECTOR_H
#define ATSPI_DETECTOR_H

#include <at-spi-2.0/atspi/atspi.h>

typedef struct ElementInfo {
	int x;
	int y;
	int w;
	int h;
	char *name;
	char *role;
	int depth;
} ElementInfo;

// extern GSList *element_list;

void atspi_init_detector(void);
GSList *detect_elements();
void free_detector_resources(void);
void atspi_cleanup(void);
void print_info(ElementInfo *element);

/* Window navigation support */
typedef struct WindowInfo {
	char *name;
	char *app_name;
	int x, y, w, h;
	void *window_ref;  /* AtspiAccessible* - opaque for portability */
} WindowInfo;

GSList *get_all_windows(void);
void focus_window(WindowInfo *win);
void free_window_list(GSList *list);

#endif
