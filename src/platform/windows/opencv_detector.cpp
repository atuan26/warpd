#if defined(HAVE_OPENCV)

/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Windows OpenCV detector implementation
 * Uses Windows GDI for screen capture + common OpenCV detection logic
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
#include <windows.h>
#include <vector>

/**
 * Capture screenshot using Windows GDI and convert to cv::Mat
 */
static cv::Mat capture_screenshot_windows()
{
    // Get screen dimensions
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);

    // Get screen DC
    HDC screen_dc = GetDC(NULL);
    if (!screen_dc) {
        return cv::Mat();
    }

    // Create compatible DC and bitmap
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    if (!mem_dc) {
        ReleaseDC(NULL, screen_dc);
        return cv::Mat();
    }

    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, screen_width, screen_height);
    if (!bitmap) {
        DeleteDC(mem_dc);
        ReleaseDC(NULL, screen_dc);
        return cv::Mat();
    }

    // Select bitmap into memory DC
    HBITMAP old_bitmap = (HBITMAP)SelectObject(mem_dc, bitmap);

    // Copy screen to memory DC
    if (!BitBlt(mem_dc, 0, 0, screen_width, screen_height, screen_dc, 0, 0, SRCCOPY)) {
        SelectObject(mem_dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(mem_dc);
        ReleaseDC(NULL, screen_dc);
        return cv::Mat();
    }

    // Get bitmap info
    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = screen_width;
    bi.biHeight = -screen_height;  // Negative for top-down bitmap
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    // Create cv::Mat
    cv::Mat img(screen_height, screen_width, CV_8UC4);

    // Get bitmap bits
    if (!GetDIBits(screen_dc, bitmap, 0, screen_height, img.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS)) {
        SelectObject(mem_dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(mem_dc);
        ReleaseDC(NULL, screen_dc);
        return cv::Mat();
    }

    // Cleanup
    SelectObject(mem_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);

    // Convert BGRA to RGBA if needed (Windows uses BGRA)
    cv::cvtColor(img, img, cv::COLOR_BGRA2RGBA);

    return img;
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
 * Detect UI elements using OpenCV on Windows
 */
struct ui_detection_result *opencv_detect_ui_elements(void)
{
    try {
        // Capture screenshot
        cv::Mat screenshot = capture_screenshot_windows();
        if (screenshot.empty()) {
            struct ui_detection_result *result = 
                (struct ui_detection_result *)calloc(1, sizeof(struct ui_detection_result));
            if (result) {
                result->error = -1;
                snprintf(result->error_msg, sizeof(result->error_msg),
                         "Failed to capture screenshot on Windows");
            }
            return result;
        }

        // Detect rectangles using OpenCV
        std::vector<cv::Rect> rectangles = detect_rectangles(screenshot);

        // Convert to ui_elements
        return rectangles_to_ui_elements(rectangles, "Windows OpenCV");

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

#endif // HAVE_OPENCV