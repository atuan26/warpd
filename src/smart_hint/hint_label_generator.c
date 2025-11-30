/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Label Generation Module Implementation
 */

#include "hint_label_generator.h"
#include <stdio.h>
#include <string.h>

void hint_label_generate_alphabetic(struct hint *hints, size_t count)
{
	if (!hints || count == 0) {
		return;
	}

	/* Calculate label length needed */
	int label_len = 1;
	size_t max_elements = 26;

	while (max_elements < count) {
		label_len++;
		max_elements *= 26;
	}

	/* Generate labels */
	for (size_t i = 0; i < count; i++) {
		int remaining = i;
		char label[16] = {0};

		/* Initialize all positions to 'A' */
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

void hint_label_generate_numeric(struct hint *hints, size_t count)
{
	if (!hints || count == 0) {
		return;
	}

	/* Calculate label length needed (zero-padded) */
	int label_len = 1;
	size_t max_with_len = 9;

	while (max_with_len < count) {
		label_len++;
		max_with_len = max_with_len * 10 + 9;
	}

	/* Generate labels starting from 1 */
	for (size_t i = 0; i < count; i++) {
		snprintf(hints[i].label, sizeof(hints[i].label), "%0*zu", label_len, i + 1);
	}
}
