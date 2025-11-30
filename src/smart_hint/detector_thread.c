/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Smart Hint - Detector Thread Module Implementation
 *
 * Provides cross-platform threading abstraction for UI detection.
 */

#include "detector_thread.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

extern struct platform *platform;

/* Internal thread context structure */
struct detector_thread {
	volatile int done;
	struct ui_detection_result *result;

#ifdef _WIN32
	HANDLE thread;
	CRITICAL_SECTION lock;
#else
	pthread_t thread;
	pthread_mutex_t lock;
#endif
};

/**
 * Thread entry point - runs UI detection
 */
#ifdef _WIN32
static DWORD WINAPI detection_worker(LPVOID param)
#else
static void *detection_worker(void *param)
#endif
{
	detector_thread_t *ctx = (detector_thread_t *)param;

	/* Run platform-specific detection */
	ctx->result = platform->detect_ui_elements();

	/* Mark as done (thread-safe) */
#ifdef _WIN32
	EnterCriticalSection(&ctx->lock);
	ctx->done = 1;
	LeaveCriticalSection(&ctx->lock);
#else
	pthread_mutex_lock(&ctx->lock);
	ctx->done = 1;
	pthread_mutex_unlock(&ctx->lock);
#endif

	return 0;
}

detector_thread_t* detector_thread_create(void)
{
	detector_thread_t *thread = calloc(1, sizeof(detector_thread_t));
	if (!thread) {
		return NULL;
	}

	thread->done = 0;
	thread->result = NULL;

#ifdef _WIN32
	InitializeCriticalSection(&thread->lock);
#else
	if (pthread_mutex_init(&thread->lock, NULL) != 0) {
		free(thread);
		return NULL;
	}
#endif

	return thread;
}

int detector_thread_start(detector_thread_t *thread)
{
	if (!thread) {
		return -1;
	}

#ifdef _WIN32
	thread->thread = CreateThread(NULL, 0, detection_worker, thread, 0, NULL);
	if (!thread->thread) {
		return -1;
	}
#else
	if (pthread_create(&thread->thread, NULL, detection_worker, thread) != 0) {
		return -1;
	}
#endif

	return 0;
}

int detector_thread_is_done(detector_thread_t *thread)
{
	if (!thread) {
		return 1;
	}

	int done;

#ifdef _WIN32
	EnterCriticalSection(&thread->lock);
	done = thread->done;
	LeaveCriticalSection(&thread->lock);
#else
	pthread_mutex_lock(&thread->lock);
	done = thread->done;
	pthread_mutex_unlock(&thread->lock);
#endif

	return done;
}

struct ui_detection_result* detector_thread_join(detector_thread_t *thread)
{
	if (!thread) {
		return NULL;
	}

	/* Wait for thread to finish */
#ifdef _WIN32
	WaitForSingleObject(thread->thread, INFINITE);
	CloseHandle(thread->thread);
#else
	pthread_join(thread->thread, NULL);
#endif

	/* Extract result */
	struct ui_detection_result *result = thread->result;

	/* Clean up thread resources */
#ifdef _WIN32
	DeleteCriticalSection(&thread->lock);
#else
	pthread_mutex_destroy(&thread->lock);
#endif

	free(thread);

	return result;
}

void detector_thread_destroy(detector_thread_t *thread)
{
	if (!thread) {
		return;
	}

	/* Note: This doesn't actually stop the thread, just cleans up */
	/* Only call this if you haven't called detector_thread_join */

#ifdef _WIN32
	if (thread->thread) {
		CloseHandle(thread->thread);
	}
	DeleteCriticalSection(&thread->lock);
#else
	pthread_mutex_destroy(&thread->lock);
#endif

	free(thread);
}
