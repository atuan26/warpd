/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Linux OpenCV detector implementation
 * Unified detector that chooses between X11/Wayland at runtime
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

#include "../../common/opencv_detector.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <algorithm>
#include <cstdlib>  // for atof

// Forward declarations for platform-specific screen capture
#ifdef WARPD_X
#include <X11/Xlib.h>
#include <X11/Xutil.h>
extern "C" Display *dpy; // From X.c
static cv::Mat capture_screenshot_x11();
#endif

#ifdef WARPD_WAYLAND
static cv::Mat capture_screenshot_wayland();
#endif

#ifdef WARPD_X
/**
 * Capture screenshot using X11 and convert to cv::Mat
 */
static cv::Mat capture_screenshot_x11()
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
#endif

#ifdef WARPD_WAYLAND
/**
 * Capture screenshot using Wayland and convert to cv::Mat
 * TODO: Implement proper Wayland screen capture
 */
static cv::Mat capture_screenshot_wayland()
{
    // TODO: Implement Wayland screen capture using:
    // - wlr-screencopy protocol for wlroots compositors
    // - xdg-desktop-portal for GNOME/KDE
    // - Direct compositor-specific APIs
    
    fprintf(stderr, "WARNING: Wayland OpenCV screen capture not yet implemented\n");
    return cv::Mat();
}
#endif

/**
 * Capture screenshot using the appropriate method for the current session
 */
static cv::Mat capture_screenshot_linux()
{
#ifdef WARPD_X
    if (dpy) {
        return capture_screenshot_x11();
    }
#endif

#ifdef WARPD_WAYLAND
    // TODO: Add runtime Wayland detection
    return capture_screenshot_wayland();
#endif

    fprintf(stderr, "ERROR: No screen capture method available\n");
    return cv::Mat();
}

// C interface functions
extern "C" {

/**
 * Check if OpenCV is available
 */
int opencv_is_available(void)
{
#ifdef WARPD_X
    if (dpy) {
        return 1; // X11 OpenCV is available
    }
#endif

#ifdef WARPD_WAYLAND
    // TODO: Check if Wayland screen capture is available
    fprintf(stderr, "WARNING: Wayland OpenCV detector not fully implemented\n");
    return 0; // Disabled until implementation is complete
#endif

    return 0;
}

/**
 * Detect UI elements using OpenCV on Linux
 * Supports configurable detection modes: strict, relaxed, auto
 */
struct ui_detection_result *opencv_detect_ui_elements(void)
{
    struct ui_detection_result *result =
        (struct ui_detection_result *)calloc(1, sizeof(struct ui_detection_result));
    if (!result)
        return NULL;

    try {
        const char *backend = "Unknown";
#ifdef WARPD_X
        if (dpy) backend = "X11";
#endif
#ifdef WARPD_WAYLAND
        if (!dpy) backend = "Wayland";
#endif

        fprintf(stderr, "\n");
        fprintf(stderr, "========================================\n");
        fprintf(stderr, "  OpenCV UI Detection Debug Output (%s)\n", backend);
        fprintf(stderr, "========================================\n");

        // Capture screenshot
        cv::Mat screenshot = capture_screenshot_linux();
        if (screenshot.empty()) {
            result->error = -1;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Failed to capture %s screenshot", backend);
            return result;
        }

        fprintf(stderr, "\nStep 0: Captured %s screenshot (%dx%d)\n", backend, screenshot.cols, screenshot.rows);

        // Detect rectangles using OpenCV
        std::vector<cv::Rect> rects = detect_rectangles(screenshot);

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
        fprintf(stderr, "✅ SUCCESS: Detected %zu UI elements (%s)\n", result->count, backend);
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
    opencv_free_ui_elements_common(result);
}

} // extern "C"