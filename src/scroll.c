/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * © 2019 Raheman Vaiya (see: LICENSE).
 */

#include "warpd.h"

#ifdef __APPLE__
#define factor 1
#elif defined(WINDOWS)
#define factor 15
#else
#define factor 50
#endif

#define fling_velocity (2000.0 / factor);

/* terminal velocity */
#define vt ((float)config_get_int("scroll_max_speed") / factor)
#define v0 ((float)config_get_int("scroll_speed") / factor)
#define da0 ((float)config_get_int("scroll_deceleration") / factor) /* deceleration */
#define a0 ((float)config_get_int("scroll_acceleration") / factor)

static long last_tick = 0;

/* in scroll units per second. */
static float v = 0;
static float a = 0;
static float d = 0; /* total distance */

static int direction = 0;

static long traveled = 0; /* scroll units emitted. */

void scroll_tick()
{
	int i;
	/* Non zero to provide the illusion of continuous scrolling */

	const long current_time = get_time_us()/1000;
	float t = (float)(current_time - last_tick); // time elapsed since last tick in ms
	last_tick = current_time;

	/* Safety: clamp time delta to prevent huge scrolls from stale last_tick */
	if (t < 0 || t > 500) {
		t = 16;  /* Default to ~60fps equivalent */
	}

	/* distance traveled since the last tick */
	d += v * (t / 1000) + .5 * a * (t / 1000) * (t / 1000);
	v += a * (t / 1000);

	if (v < 0) {
		v = 0;
		d = 0;
		traveled = 0;
	}

	if (v >= vt) {
		v = vt;
		a = 0;
	}

	for (i = 0; i < (long)d - traveled; i++)
		platform->scroll(direction);

	traveled = (long)d;
}

void scroll_stop()
{
	v = 0;
	a = 0;
	traveled = 0;
	d = 0;
	last_tick = get_time_us() / 1000;
}

void scroll_decelerate()
{
	a = da0;
}

void scroll_accelerate(int _direction)
{
	if (direction == _direction && v > 0) {
		a = a0;
		return;
	}

	direction = _direction;
	a = a0;
	d = 0;
	traveled = 0;
	v = v0;
}

void scroll_impart_impulse()
{
	v += fling_velocity;
}
