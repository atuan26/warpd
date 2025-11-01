/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Common OpenCV detector - Header
 */

#ifndef OPENCV_DETECTOR_H
#define OPENCV_DETECTOR_H

#ifdef __cplusplus
#include <opencv2/opencv.hpp>
#include <vector>

// Common detection functions
std::vector<cv::Rect> detect_rectangles(const cv::Mat &img, bool strict_mode);
std::vector<cv::Rect> deduplicate_rectangles(std::vector<cv::Rect> &rects);
struct ui_detection_result *rectangles_to_ui_elements(const std::vector<cv::Rect> &rectangles, const char *detector_name);
void opencv_free_ui_elements_common(struct ui_detection_result *result);

extern "C" {
#endif

#include "../platform.h"

// C interface functions that each platform must implement
struct ui_detection_result *opencv_detect_ui_elements(void);
void opencv_free_ui_elements(struct ui_detection_result *result);
int opencv_is_available(void);

#ifdef __cplusplus
}
#endif

#endif // OPENCV_DETECTOR_H