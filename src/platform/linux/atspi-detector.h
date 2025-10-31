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
void print_info(ElementInfo *element);

#endif
