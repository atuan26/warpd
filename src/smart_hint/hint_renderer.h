/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Rendering Module
 *
 * Handles all drawing operations for smart hint mode
 */

#ifndef HINT_RENDERER_H
#define HINT_RENDERER_H

#include "hint_state.h"
#include "../platform.h"

/**
 * Clear the screen
 *
 * @param scr Screen to clear
 */
void hint_renderer_clear(screen_t scr);

/**
 * Draw hints on screen
 *
 * @param scr Screen to draw on
 * @param hints Array of hints to draw
 * @param count Number of hints
 */
void hint_renderer_draw(screen_t scr, struct hint *hints, size_t count);

/**
 * Commit all pending draw operations
 */
void hint_renderer_commit(void);

/**
 * Draw hints from state (convenience function)
 *
 * @param state Hint state containing matched hints
 */
void hint_renderer_draw_state(hint_state_t *state);

#endif /* HINT_RENDERER_H */
