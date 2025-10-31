/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Common OpenCV detector - Shared detection logic
 * This provides shared OpenCV detection algorithms for all platforms
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "../platform.h"

// Import config functions from C
extern const char *config_get(const char *key);
extern int config_get_int(const char *key);

#ifdef __cplusplus
}
#endif

#include <opencv2/opencv.hpp>
#include <vector>
#include <algorithm>
#include <cstdlib>  // for atof

/**
 * Remove overlapping rectangles (keep larger ones)
 * Common deduplication logic for all platforms
 */
std::vector<cv::Rect> deduplicate_rectangles(std::vector<cv::Rect> &rects)
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

/**
 * Detect rectangular UI elements using edge detection
 * Cross-platform detection logic - works with any cv::Mat input
 */
std::vector<cv::Rect> detect_rectangles(const cv::Mat &img, bool strict_mode)
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

    // Step 1: Convert to grayscale
    cv::cvtColor(img, gray, cv::COLOR_BGRA2GRAY);

    // Step 2: Apply Gaussian blur
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0);

    // Step 3: Canny edge detection
    cv::Canny(blurred, edges, 50, 150);

    // Step 4: Find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

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

        // Filter by dimensions
        if (rect.width < min_width || rect.width > max_width ||
            rect.height < min_height || rect.height > max_height) {
            rejected_size++;
            continue;
        }

        // Filter by aspect ratio
        double aspect = (double)rect.width / rect.height;
        if (aspect < min_aspect || aspect > max_aspect) {
            rejected_aspect++;
            continue;
        }

        rectangles.push_back(rect);
    }

    // Sort rectangles by area (largest first)
    std::sort(rectangles.begin(), rectangles.end(),
              [](const cv::Rect &a, const cv::Rect &b) {
                  return (a.width * a.height) > (b.width * b.height);
              });

    // Limit to MAX_UI_ELEMENTS
    if (rectangles.size() > MAX_UI_ELEMENTS) {
        rectangles.resize(MAX_UI_ELEMENTS);
    }

    return rectangles;
}

/**
 * Convert rectangles to ui_element array
 * Common logic for all platforms
 */
struct ui_detection_result *rectangles_to_ui_elements(const std::vector<cv::Rect> &rectangles, const char *detector_name)
{
    struct ui_detection_result *result = 
        (struct ui_detection_result *)calloc(1, sizeof(struct ui_detection_result));
    if (!result)
        return nullptr;

    if (rectangles.empty()) {
        result->error = -1;
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "No UI elements detected by %s", detector_name);
        return result;
    }

    // Allocate result array
    result->elements = (struct ui_element *)calloc(rectangles.size(), sizeof(struct ui_element));
    if (!result->elements) {
        result->error = -2;
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Memory allocation failed");
        free(result);
        return nullptr;
    }

    // Convert rectangles to ui_elements
    for (size_t i = 0; i < rectangles.size(); i++) {
        const cv::Rect &rect = rectangles[i];
        
        result->elements[i].x = rect.x;
        result->elements[i].y = rect.y;
        result->elements[i].w = rect.width;
        result->elements[i].h = rect.height;
        result->elements[i].name = nullptr;  // OpenCV doesn't provide names
        
        // Set generic role
        result->elements[i].role = (char*)malloc(8);
        strcpy(result->elements[i].role, "element");
    }

    result->count = rectangles.size();
    result->error = 0;

    fprintf(stderr, "%s: Detected %zu UI elements\n", detector_name, result->count);
    return result;
}

/**
 * Free UI detection result - common for all platforms
 */
void opencv_free_ui_elements_common(struct ui_detection_result *result)
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