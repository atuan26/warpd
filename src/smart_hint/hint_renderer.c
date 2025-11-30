/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Rendering Module Implementation
 */

#include "hint_renderer.h"

extern struct platform *platform;

void hint_renderer_clear(screen_t scr)
{
	if (!scr || !platform) {
		return;
	}

	platform->screen_clear(scr);
}

void hint_renderer_draw(screen_t scr, struct hint *hints, size_t count)
{
	if (!scr || !hints || count == 0 || !platform) {
		return;
	}

	platform->hint_draw(scr, hints, count);
}

void hint_renderer_commit(void)
{
	if (!platform) {
		return;
	}

	platform->commit();
}

void hint_renderer_draw_state(hint_state_t *state)
{
	if (!state) {
		return;
	}

	hint_renderer_clear(state->screen);
	hint_renderer_draw(state->screen, state->matched, state->nr_matched);
	hint_renderer_commit();
}
