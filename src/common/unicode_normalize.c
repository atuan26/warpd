/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Unicode Normalization Implementation
 * 
 * Fast lookup-table based normalization for Vietnamese and common accents.
 * Optimized for performance - no heap allocations, single-pass processing.
 */

#include "unicode_normalize.h"
#include <string.h>

/* 
 * Vietnamese Diacritics Mapping Table
 * Each entry: [UTF-8 bytes, length, ASCII equivalent]
 * Length is pre-calculated to avoid strlen() calls
 */
static const struct {
	const char *utf8;  /* UTF-8 sequence (2-3 bytes) */
	unsigned char len; /* Pre-calculated length (2 or 3) */
	char ascii;        /* ASCII replacement */
} diacritic_map[] = {
	/* Vietnamese vowels with diacritics → base vowel */
	/* a variants: à, á, ả, ã, ạ, ă, ằ, ắ, ẳ, ẵ, ặ, â, ầ, ấ, ẩ, ẫ, ậ */
	{"à", 2, 'a'}, {"á", 2, 'a'}, {"ả", 3, 'a'}, {"ã", 2, 'a'}, {"ạ", 3, 'a'},
	{"ă", 2, 'a'}, {"ằ", 3, 'a'}, {"ắ", 3, 'a'}, {"ẳ", 3, 'a'}, {"ẵ", 3, 'a'}, {"ặ", 3, 'a'},
	{"â", 2, 'a'}, {"ầ", 3, 'a'}, {"ấ", 3, 'a'}, {"ẩ", 3, 'a'}, {"ẫ", 3, 'a'}, {"ậ", 3, 'a'},
	
	/* e variants: è, é, ẻ, ẽ, ẹ, ê, ề, ế, ể, ễ, ệ */
	{"è", 2, 'e'}, {"é", 2, 'e'}, {"ẻ", 3, 'e'}, {"ẽ", 2, 'e'}, {"ẹ", 3, 'e'},
	{"ê", 2, 'e'}, {"ề", 3, 'e'}, {"ế", 3, 'e'}, {"ể", 3, 'e'}, {"ễ", 3, 'e'}, {"ệ", 3, 'e'},
	
	/* i variants: ì, í, ỉ, ĩ, ị */
	{"ì", 2, 'i'}, {"í", 2, 'i'}, {"ỉ", 3, 'i'}, {"ĩ", 2, 'i'}, {"ị", 3, 'i'},
	
	/* o variants: ò, ó, ỏ, õ, ọ, ô, ồ, ố, ổ, ỗ, ộ, ơ, ờ, ớ, ở, ỡ, ợ */
	{"ò", 2, 'o'}, {"ó", 2, 'o'}, {"ỏ", 3, 'o'}, {"õ", 2, 'o'}, {"ọ", 3, 'o'},
	{"ô", 2, 'o'}, {"ồ", 3, 'o'}, {"ố", 3, 'o'}, {"ổ", 3, 'o'}, {"ỗ", 3, 'o'}, {"ộ", 3, 'o'},
	{"ơ", 2, 'o'}, {"ờ", 3, 'o'}, {"ớ", 3, 'o'}, {"ở", 3, 'o'}, {"ỡ", 3, 'o'}, {"ợ", 3, 'o'},
	
	/* u variants: ù, ú, ủ, ũ, ụ, ư, ừ, ứ, ử, ữ, ự */
	{"ù", 2, 'u'}, {"ú", 2, 'u'}, {"ủ", 3, 'u'}, {"ũ", 2, 'u'}, {"ụ", 3, 'u'},
	{"ư", 2, 'u'}, {"ừ", 3, 'u'}, {"ứ", 3, 'u'}, {"ử", 3, 'u'}, {"ữ", 3, 'u'}, {"ự", 3, 'u'},
	
	/* y variants: ỳ, ý, ỷ, ỹ, ỵ */
	{"ỳ", 3, 'y'}, {"ý", 2, 'y'}, {"ỷ", 3, 'y'}, {"ỹ", 3, 'y'}, {"ỵ", 3, 'y'},
	
	/* d variants: đ */
	{"đ", 2, 'd'},
	
	/* Uppercase variants */
	{"À", 2, 'A'}, {"Á", 2, 'A'}, {"Ả", 3, 'A'}, {"Ã", 2, 'A'}, {"Ạ", 3, 'A'},
	{"Ă", 2, 'A'}, {"Ằ", 3, 'A'}, {"Ắ", 3, 'A'}, {"Ẳ", 3, 'A'}, {"Ẵ", 3, 'A'}, {"Ặ", 3, 'A'},
	{"Â", 2, 'A'}, {"Ầ", 3, 'A'}, {"Ấ", 3, 'A'}, {"Ẩ", 3, 'A'}, {"Ẫ", 3, 'A'}, {"Ậ", 3, 'A'},
	
	{"È", 2, 'E'}, {"É", 2, 'E'}, {"Ẻ", 3, 'E'}, {"Ẽ", 2, 'E'}, {"Ẹ", 3, 'E'},
	{"Ê", 2, 'E'}, {"Ề", 3, 'E'}, {"Ế", 3, 'E'}, {"Ể", 3, 'E'}, {"Ễ", 3, 'E'}, {"Ệ", 3, 'E'},
	
	{"Ì", 2, 'I'}, {"Í", 2, 'I'}, {"Ỉ", 3, 'I'}, {"Ĩ", 2, 'I'}, {"Ị", 3, 'I'},
	
	{"Ò", 2, 'O'}, {"Ó", 2, 'O'}, {"Ỏ", 3, 'O'}, {"Õ", 2, 'O'}, {"Ọ", 3, 'O'},
	{"Ô", 2, 'O'}, {"Ồ", 3, 'O'}, {"Ố", 3, 'O'}, {"Ổ", 3, 'O'}, {"Ỗ", 3, 'O'}, {"Ộ", 3, 'O'},
	{"Ơ", 2, 'O'}, {"Ờ", 3, 'O'}, {"Ớ", 3, 'O'}, {"Ở", 3, 'O'}, {"Ỡ", 3, 'O'}, {"Ợ", 3, 'O'},
	
	{"Ù", 2, 'U'}, {"Ú", 2, 'U'}, {"Ủ", 3, 'U'}, {"Ũ", 2, 'U'}, {"Ụ", 3, 'U'},
	{"Ư", 2, 'U'}, {"Ừ", 3, 'U'}, {"Ứ", 3, 'U'}, {"Ử", 3, 'U'}, {"Ữ", 3, 'U'}, {"Ự", 3, 'U'},
	
	{"Ỳ", 3, 'Y'}, {"Ý", 2, 'Y'}, {"Ỷ", 3, 'Y'}, {"Ỹ", 3, 'Y'}, {"Ỵ", 3, 'Y'},
	
	{"Đ", 2, 'D'},
	
	/* Common European accents (bonus) */
	{"ä", 2, 'a'}, {"ö", 2, 'o'}, {"ü", 2, 'u'}, {"ß", 2, 's'},
	{"Ä", 2, 'A'}, {"Ö", 2, 'O'}, {"Ü", 2, 'U'},
	{"ç", 2, 'c'}, {"Ç", 2, 'C'},
	{"ñ", 2, 'n'}, {"Ñ", 2, 'N'},
};

#define DIACRITIC_MAP_SIZE (sizeof(diacritic_map) / sizeof(diacritic_map[0]))

int unicode_normalize_char(const char *utf8, char *out_ascii)
{
	if (!utf8 || !out_ascii) {
		return 0;
	}
	
	if ((unsigned char)utf8[0] < 128) {
		*out_ascii = utf8[0];
		return 1;
	}
	
	for (size_t i = 0; i < DIACRITIC_MAP_SIZE; i++) {
		const char *map_utf8 = diacritic_map[i].utf8;
		unsigned char map_len = diacritic_map[i].len;
		
		int match = 1;
		for (int j = 0; j < map_len; j++) {
			if (utf8[j] != map_utf8[j]) {
				match = 0;
				break;
			}
		}
		
		if (match) {
			*out_ascii = diacritic_map[i].ascii;
			return map_len;
		}
	}
	
	unsigned char first = (unsigned char)utf8[0];
	int byte_len = 1;
	if ((first & 0xE0) == 0xC0) byte_len = 2;
	else if ((first & 0xF0) == 0xE0) byte_len = 3;
	else if ((first & 0xF8) == 0xF0) byte_len = 4;
	
	*out_ascii = '?';
	return byte_len;
}

void unicode_normalize(const char *input, char *output, size_t output_size)
{
	if (!output || output_size == 0) {
		return;
	}
	
	/* Handle null input - return empty string */
	if (!input) {
		output[0] = '\0';
		return;
	}
	
	const char *in = input;
	char *out = output;
	char *out_end = output + output_size - 1; /* Reserve space for null terminator */
	
	while (*in && out < out_end) {
		char ascii;
		int bytes_consumed = unicode_normalize_char(in, &ascii);
		
		if (bytes_consumed > 0) {
			*out++ = ascii;
			in += bytes_consumed;
		} else {
			/* Shouldn't happen, but be safe */
			*out++ = *in++;
		}
	}
	
	*out = '\0';
}
