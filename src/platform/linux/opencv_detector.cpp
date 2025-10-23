/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * OpenCV C++ wrapper with C interface
 * This provides a C-compatible interface to OpenCV C++ API
 * Now with configurable detection parameters!
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "../../platform.h"

// Import config functions from C
extern const char *config_get(const char *key);
extern int config_get_int(const char *key);

#ifdef __cplusplus
}
#endif

#include <opencv2/opencv.hpp>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <vector>
#include <algorithm>
#include <cstdlib>  // for atof

extern "C" Display *dpy; // From X.c

/**
 * Capture screenshot using X11 and convert to cv::Mat
 */
static cv::Mat capture_screenshot()
{
	Window root = DefaultRootWindow(dpy);
	XWindowAttributes attrs;

	XGetWindowAttributes(dpy, root, &attrs);

	XImage *ximg = XGetImage(dpy, root, 0, 0, attrs.width, attrs.height,
	                          AllPlanes, ZPixmap);
	if (!ximg) {
		return cv::Mat();
	}

	// Create cv::Mat from XImage
	cv::Mat img(attrs.height, attrs.width, CV_8UC4);

	for (int y = 0; y < attrs.height; y++) {
		for (int x = 0; x < attrs.width; x++) {
			unsigned long pixel = XGetPixel(ximg, x, y);

			// Extract RGBA
			unsigned char b = (pixel & 0xFF);
			unsigned char g = (pixel >> 8) & 0xFF;
			unsigned char r = (pixel >> 16) & 0xFF;

			// Set BGRA
			img.at<cv::Vec4b>(y, x) = cv::Vec4b(b, g, r, 255);
		}
	}

	XDestroyImage(ximg);
	return img;
}

/**
 * Detect rectangular UI elements using edge detection
 * Now reads parameters from config file!
 * DEBUG: Prints detailed config and filter statistics
 */
static std::vector<cv::Rect> detect_rectangles(const cv::Mat &img, bool strict_mode)
{
	cv::Mat gray, blurred, edges;

	// Read filter parameters from config
	int min_area, max_area;
	double min_aspect, max_aspect;
	int min_width, min_height, max_width, max_height;

	if (strict_mode) {
		// Strict mode: Read from opencv_* config options
		min_area = config_get_int("opencv_min_area");
		max_area = config_get_int("opencv_max_area");
		min_width = config_get_int("opencv_min_width");
		min_height = config_get_int("opencv_min_height");
		max_width = config_get_int("opencv_max_width");
		max_height = config_get_int("opencv_max_height");
		min_aspect = atof(config_get("opencv_min_aspect"));
		max_aspect = atof(config_get("opencv_max_aspect"));

		fprintf(stderr, "\n=== OpenCV Strict Mode Config ===\n");
	} else {
		// Relaxed mode: Read from opencv_relaxed_* config options
		min_area = config_get_int("opencv_relaxed_min_area");
		max_area = config_get_int("opencv_relaxed_max_area");
		min_width = config_get_int("opencv_relaxed_min_width");
		min_height = config_get_int("opencv_relaxed_min_height");
		max_width = config_get_int("opencv_relaxed_max_width");
		max_height = config_get_int("opencv_relaxed_max_height");
		min_aspect = atof(config_get("opencv_relaxed_min_aspect"));
		max_aspect = atof(config_get("opencv_relaxed_max_aspect"));

		fprintf(stderr, "\n=== OpenCV Relaxed Mode Config ===\n");
	}

	// // Print config values
	// fprintf(stderr, "  Area: %d - %d pixels²\n", min_area, max_area);
	// fprintf(stderr, "  Width: %d - %d pixels\n", min_width, max_width);
	// fprintf(stderr, "  Height: %d - %d pixels\n", min_height, max_height);
	// fprintf(stderr, "  Aspect ratio: %.2f - %.2f\n", min_aspect, max_aspect);
	// fprintf(stderr, "==================================\n\n");

	// Step 1: Convert to grayscale
	cv::cvtColor(img, gray, cv::COLOR_BGRA2GRAY);
	// fprintf(stderr, "Step 1: Converted to grayscale (%dx%d)\n", gray.cols, gray.rows);

	// Step 2: Apply Gaussian blur
	cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0);
	// fprintf(stderr, "Step 2: Applied Gaussian blur (5x5)\n");

	// Step 3: Canny edge detection
	cv::Canny(blurred, edges, 50, 150);
	// fprintf(stderr, "Step 3: Canny edge detection (thresholds: 50, 150)\n");

	// Count edges
	int edge_pixels = cv::countNonZero(edges);
	// fprintf(stderr, "         Edge pixels: %d (%.2f%% of image)\n",
	//         edge_pixels, (edge_pixels * 100.0) / (edges.cols * edges.rows));

	// Step 4: Find contours
	std::vector<std::vector<cv::Point>> contours;
	cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
	// fprintf(stderr, "Step 4: Found %zu contours\n", contours.size());

	// Step 5: Filter rectangles
	std::vector<cv::Rect> rectangles;
	int rejected_area = 0, rejected_aspect = 0, rejected_size = 0;

	for (const auto &contour : contours) {
		double area = cv::contourArea(contour);

		// Filter by area
		if (area < min_area) {
			rejected_area++;
			continue;
		}
		if (area > max_area) {
			rejected_area++;
			continue;
		}

		// Get bounding rectangle
		cv::Rect rect = cv::boundingRect(contour);

		// Filter by aspect ratio
		double aspect_ratio = (double)rect.width / rect.height;
		if (aspect_ratio < min_aspect || aspect_ratio > max_aspect) {
			rejected_aspect++;
			continue;
		}

		// Filter by size
		if (rect.width < min_width || rect.height < min_height) {
			rejected_size++;
			continue;
		}
		if (rect.width > max_width || rect.height > max_height) {
			rejected_size++;
			continue;
		}

		rectangles.push_back(rect);

		// Print first few accepted elements for debugging
		if (rectangles.size() <= 5) {
			fprintf(stderr, "         ✓ Element #%zu: %dx%d at (%d,%d), area=%d, aspect=%.2f\n",
			        rectangles.size(), rect.width, rect.height, rect.x, rect.y,
			        rect.width * rect.height, aspect_ratio);
		}
	}

	// fprintf(stderr, "Step 5: Filtered rectangles\n");
	// fprintf(stderr, "  ✓ Accepted: %zu\n", rectangles.size());
	// fprintf(stderr, "  ✗ Rejected by area: %d (< %d or > %d)\n", rejected_area, min_area, max_area);
	// fprintf(stderr, "  ✗ Rejected by aspect ratio: %d (< %.2f or > %.2f)\n",
	//         rejected_aspect, min_aspect, max_aspect);
	// fprintf(stderr, "  ✗ Rejected by size: %d\n", rejected_size);
	// fprintf(stderr, "  Total contours: %zu\n", contours.size());

	// if (rectangles.size() > 5) {
	// 	fprintf(stderr, "  (showing first 5 accepted elements above)\n");
	// }

	return rectangles;
}

/**
 * Remove overlapping rectangles (keep larger ones)
 */
static std::vector<cv::Rect> deduplicate_rectangles(std::vector<cv::Rect> &rects)
{
	if (rects.size() <= 1)
		return rects;

	std::vector<bool> keep(rects.size(), true);

	for (size_t i = 0; i < rects.size(); i++) {
		if (!keep[i])
			continue;

		for (size_t j = i + 1; j < rects.size(); j++) {
			if (!keep[j])
				continue;

			// Check overlap
			cv::Rect intersect = rects[i] & rects[j];
			if (intersect.area() > 0) {
				// Keep larger one
				if (rects[i].area() > rects[j].area()) {
					keep[j] = false;
				} else {
					keep[i] = false;
					break;
				}
			}
		}
	}

	std::vector<cv::Rect> result;
	for (size_t i = 0; i < rects.size(); i++) {
		if (keep[i]) {
			result.push_back(rects[i]);
		}
	}

	return result;
}

// C interface functions
extern "C" {

/**
 * Check if OpenCV is available
 */
int opencv_is_available(void)
{
	return 1; // OpenCV is compiled in
}

/**
 * Detect UI elements using OpenCV
 * Supports configurable detection modes: strict, relaxed, auto
 * DEBUG: Prints detailed detection statistics
 */
struct ui_detection_result *opencv_detect_ui_elements(void)
{
	struct ui_detection_result *result =
		(struct ui_detection_result *)calloc(1, sizeof(struct ui_detection_result));
	if (!result)
		return NULL;

	try {
		// Get detection mode from config
		const char *mode = config_get("opencv_mode");
		int threshold = config_get_int("opencv_auto_threshold");

		fprintf(stderr, "\n");
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "  OpenCV UI Detection Debug Output\n");
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "Mode: %s\n", mode);
		fprintf(stderr, "Auto threshold: %d elements\n", threshold);
		fprintf(stderr, "========================================\n");

		// Capture screenshot
		cv::Mat screenshot = capture_screenshot();
		if (screenshot.empty()) {
			result->error = -1;
			snprintf(result->error_msg, sizeof(result->error_msg),
			         "Failed to capture screenshot");
			return result;
		}

		fprintf(stderr, "\nStep 0: Captured screenshot (%dx%d)\n", screenshot.cols, screenshot.rows);

		std::vector<cv::Rect> rects;

		if (strcmp(mode, "strict") == 0) {
			// Strict mode only
			fprintf(stderr, "\n>>> Running in STRICT mode only <<<\n");
			rects = detect_rectangles(screenshot, true);

		} else if (strcmp(mode, "relaxed") == 0) {
			// Relaxed mode only
			fprintf(stderr, "\n>>> Running in RELAXED mode only <<<\n");
			rects = detect_rectangles(screenshot, false);

		} else {
			// Auto mode (default): Try strict first, fall back to relaxed
			fprintf(stderr, "\n>>> Running in AUTO mode <<<\n");
			fprintf(stderr, ">>> Pass 1: Trying STRICT mode <<<\n");
			rects = detect_rectangles(screenshot, true);

			if ((int)rects.size() < threshold) {
				fprintf(stderr, "\n>>> Pass 2: Found %zu elements (< %d), switching to RELAXED mode <<<\n",
				        rects.size(), threshold);
				rects = detect_rectangles(screenshot, false);
			} else {
				fprintf(stderr, "\n>>> Pass 1: Found %zu elements (>= %d), staying in STRICT mode <<<\n",
				        rects.size(), threshold);
			}
		}

		fprintf(stderr, "\n");

		if (rects.empty()) {
			result->error = -2;
			snprintf(result->error_msg, sizeof(result->error_msg),
			         "No UI elements detected");
			fprintf(stderr, "❌ ERROR: No UI elements detected after filtering!\n");
			fprintf(stderr, "========================================\n\n");
			return result;
		}

		// Step 6: Deduplicate
		fprintf(stderr, "Step 6: Deduplicating rectangles\n");
		fprintf(stderr, "  Before dedup: %zu\n", rects.size());
		rects = deduplicate_rectangles(rects);
		fprintf(stderr, "  After dedup: %zu\n", rects.size());

		// Limit to MAX_UI_ELEMENTS
		if (rects.size() > MAX_UI_ELEMENTS) {
			fprintf(stderr, "  Limited to: %d (MAX_UI_ELEMENTS)\n", MAX_UI_ELEMENTS);
			rects.resize(MAX_UI_ELEMENTS);
		}

		// Allocate result
		result->elements = (struct ui_element *)calloc(rects.size(), sizeof(struct ui_element));
		if (!result->elements) {
			result->error = -3;
			snprintf(result->error_msg, sizeof(result->error_msg),
			         "Memory allocation failed");
			return result;
		}

		// Copy rectangles to result
		for (size_t i = 0; i < rects.size(); i++) {
			result->elements[i].x = rects[i].x;
			result->elements[i].y = rects[i].y;
			result->elements[i].w = rects[i].width;
			result->elements[i].h = rects[i].height;
			result->elements[i].name = strdup("UI Element");
			result->elements[i].role = strdup("button");
		}

		result->count = rects.size();
		result->error = 0;

		fprintf(stderr, "\n");
		fprintf(stderr, "✅ SUCCESS: Detected %zu UI elements\n", result->count);
		fprintf(stderr, "========================================\n\n");

	} catch (const cv::Exception &e) {
		result->error = -4;
		snprintf(result->error_msg, sizeof(result->error_msg),
		         "OpenCV error: %s", e.what());
		fprintf(stderr, "❌ OpenCV error: %s\n", e.what());
	} catch (...) {
		result->error = -5;
		snprintf(result->error_msg, sizeof(result->error_msg),
		         "Unknown error in OpenCV detection");
		fprintf(stderr, "❌ Unknown error in OpenCV detection\n");
	}

	return result;
}

/**
 * Free OpenCV detection result
 */
void opencv_free_ui_elements(struct ui_detection_result *result)
{
	if (!result)
		return;

	if (result->elements) {
		for (size_t i = 0; i < result->count; i++) {
			free(result->elements[i].name);
			free(result->elements[i].role);
		}
		free(result->elements);
	}

	free(result);
}

} // extern "C"
