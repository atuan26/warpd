/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Unicode Normalization Utilities
 * 
 * Provides fast normalization of Vietnamese diacritics and common accented
 * characters to their ASCII equivalents for fuzzy matching.
 */

#ifndef UNICODE_NORMALIZE_H
#define UNICODE_NORMALIZE_H

#include <stddef.h>

/**
 * Normalize a UTF-8 string by removing diacritics
 * 
 * Converts Vietnamese and common accented characters to ASCII:
 *   "Bỏ qua" → "Bo qua"
 *   "café" → "cafe"
 * 
 * @param input Input UTF-8 string (null-terminated)
 * @param output Output buffer for normalized ASCII string
 * @param output_size Size of output buffer
 * 
 * Performance: O(n) where n = strlen(input)
 * Memory: Static lookup table (~2KB), no heap allocations
 */
void unicode_normalize(const char *input, char *output, size_t output_size);

/**
 * Check if a UTF-8 sequence starts with a Vietnamese diacritic
 * Returns the number of bytes to skip (0 if not a known diacritic)
 * 
 * @param utf8 Pointer to UTF-8 byte sequence
 * @param out_ascii Output ASCII equivalent character
 * @return Number of UTF-8 bytes consumed (0 if no match)
 */
int unicode_normalize_char(const char *utf8, char *out_ascii);

#endif /* UNICODE_NORMALIZE_H */
