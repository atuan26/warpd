/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * macOS OpenCV detector implementation
 * Uses macOS Core Graphics for screen capture + common OpenCV detection logic
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "../../platform.h"

#ifdef __cplusplus
}
#endif

#include "../../common/opencv_detector.h"
#include <opencv2/opencv.hpp>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

/**
 * Capture screenshot using macOS Core Graphics and convert to cv::Mat
 */
static cv::Mat capture_screenshot_macos()
{
#ifdef __APPLE__
    // Get main display
    CGDirectDisplayID display = CGMainDisplayID();
    
    // Create screenshot
    CGImageRef screenshot = CGDisplayCreateImage(display);
    if (!screenshot) {
        return cv::Mat();
    }
    
    // Get image dimensions
    size_t width = CGImageGetWidth(screenshot);
    size_t height = CGImageGetHeight(screenshot);
    
    // Create cv::Mat
    cv::Mat img(height, width, CV_8UC4);
    
    // Create bitmap context
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(
        img.data,
        width,
        height,
        8,
        img.step[0],
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
    );
    
    if (context) {
        // Draw image to context
        CGContextDrawImage(context, CGRectMake(0, 0, width, height), screenshot);
        CGContextRelease(context);
    }
    
    CGColorSpaceRelease(colorSpace);
    CGImageRelease(screenshot);
    
    if (!context) {
        return cv::Mat();
    }
    
    // Convert RGBA to BGRA for OpenCV
    cv::cvtColor(img, img, cv::COLOR_RGBA2BGRA);
    
    return img;
#else
    return cv::Mat();
#endif
}

// C interface functions
extern "C" {

/**
 * Check if OpenCV is available
 */
int opencv_is_available(void)
{
    return 1;  // OpenCV should always be available if compiled in
}

/**
 * Detect UI elements using OpenCV on macOS
 */
struct ui_detection_result *opencv_detect_ui_elements(void)
{
    try {
        // Capture screenshot
        cv::Mat screenshot = capture_screenshot_macos();
        if (screenshot.empty()) {
            struct ui_detection_result *result = 
                (struct ui_detection_result *)calloc(1, sizeof(struct ui_detection_result));
            if (result) {
                result->error = -1;
                snprintf(result->error_msg, sizeof(result->error_msg),
                         "Failed to capture screenshot on macOS");
            }
            return result;
        }

        // Try strict mode first
        std::vector<cv::Rect> rectangles = detect_rectangles(screenshot, true);
        
        // If strict mode finds too few elements, try relaxed mode
        if (rectangles.size() < 3) {
            fprintf(stderr, "OpenCV: Strict mode found %zu elements, trying relaxed mode\n", rectangles.size());
            rectangles = detect_rectangles(screenshot, false);
        }

        // Convert to ui_elements
        return rectangles_to_ui_elements(rectangles, "macOS OpenCV");

    } catch (const std::exception& e) {
        struct ui_detection_result *result = 
            (struct ui_detection_result *)calloc(1, sizeof(struct ui_detection_result));
        if (result) {
            result->error = -2;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "OpenCV error: %s", e.what());
        }
        return result;
    } catch (...) {
        struct ui_detection_result *result = 
            (struct ui_detection_result *)calloc(1, sizeof(struct ui_detection_result));
        if (result) {
            result->error = -3;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Unknown OpenCV error");
        }
        return result;
    }
}

/**
 * Free OpenCV detection result
 */
void opencv_free_ui_elements(struct ui_detection_result *result)
{
    opencv_free_ui_elements_common(result);
}

} // extern "C"