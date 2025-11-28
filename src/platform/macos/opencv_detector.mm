/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * macOS OpenCV detector implementation
 * Uses ScreenCaptureKit for screen capture (macOS 15.0+) + common OpenCV detection logic
 */

// Include C++ headers first, before Objective-C
#ifdef __cplusplus
extern "C" {
#endif

#include "../../platform.h"

#ifdef __cplusplus
}
#endif

#include "../../common/opencv_detector.h"
#include <opencv2/opencv.hpp>
#include <vector>

// Now include Objective-C headers and define Objective-C classes
#ifdef __APPLE__
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <ImageIO/ImageIO.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <dispatch/dispatch.h>

// Simple output class that conforms to SCStreamOutput protocol
@interface SimpleStreamOutput : NSObject <SCStreamOutput>
@property (nonatomic, copy) void (^sampleHandler)(CMSampleBufferRef, SCStreamOutputType, NSError *);
@end

@implementation SimpleStreamOutput
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
    if (self.sampleHandler) {
        self.sampleHandler(sampleBuffer, type, nil);
    }
}
@end
#endif

/**
 * Capture screenshot using ScreenCaptureKit (macOS 15.0+)
 * Replaces deprecated CGDisplayCreateImage and CGWindowListCreateImage
 */
static cv::Mat capture_screenshot_macos()
{
#ifdef __APPLE__
    __block cv::Mat result;
    __block bool capture_complete = false;
    __block NSError *capture_error = nil;
    
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    
    // Get shareable content (displays)
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                 onScreenWindowsOnly:YES
                                                  completionHandler:^(SCShareableContent *content, NSError *error) {
        if (error || !content || content.displays.count == 0) {
            capture_error = error;
            dispatch_semaphore_signal(semaphore);
            return;
        }
        
        // Get the main display
        SCDisplay *display = content.displays[0];
        CGRect contentRect = display.frame;
        
        // Create content filter for the display
        SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display
                                                          excludingWindows:@[]];
        
        // Create stream configuration
        SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
        config.width = (size_t)contentRect.size.width;
        config.height = (size_t)contentRect.size.height;
        config.pixelFormat = kCVPixelFormatType_32BGRA;
        config.showsCursor = NO;
        config.captureResolution = SCCaptureResolutionBest;
        
        // Frame handler block
        __block SCStream *streamRef = nil;
        void (^frameHandler)(CMSampleBufferRef, SCStreamOutputType, NSError *) = 
            ^(CMSampleBufferRef sampleBuffer, SCStreamOutputType type, NSError *error) {
            if (error || !sampleBuffer || capture_complete) {
                if (error) {
                    capture_error = error;
                }
                if (streamRef) {
                    [streamRef stopCaptureWithCompletionHandler:nil];
                }
                dispatch_semaphore_signal(semaphore);
                return;
            }
            
            // Convert CMSampleBuffer to cv::Mat
            CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
            if (!imageBuffer) {
                if (streamRef) {
                    [streamRef stopCaptureWithCompletionHandler:nil];
                }
                dispatch_semaphore_signal(semaphore);
                return;
            }
            
            CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
            
            size_t width = CVPixelBufferGetWidth(imageBuffer);
            size_t height = CVPixelBufferGetHeight(imageBuffer);
            size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
            void *baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
            
            // Create cv::Mat from pixel buffer (already BGRA format)
            result = cv::Mat((int)height, (int)width, CV_8UC4, baseAddress, bytesPerRow).clone();
            
            CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
            
            capture_complete = true;
            
            // Stop capture after first frame
            if (streamRef) {
                [streamRef stopCaptureWithCompletionHandler:^(NSError *stopError) {
                    dispatch_semaphore_signal(semaphore);
                }];
            } else {
                dispatch_semaphore_signal(semaphore);
            }
        };
        
        // Create stream
        SCStream *stream = [[SCStream alloc] initWithFilter:filter
                                              configuration:config
                                                   delegate:nil];
        streamRef = stream;
        
        // Create output object that conforms to SCStreamOutput protocol
        SimpleStreamOutput *output = [[SimpleStreamOutput alloc] init];
        output.sampleHandler = frameHandler;
        
        NSError *addError = nil;
        BOOL success = [stream addStreamOutput:output
                                           type:SCStreamOutputTypeScreen
                            sampleHandlerQueue:dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0)
                            error:&addError];
        
        if (!success || addError) {
            capture_error = addError;
            dispatch_semaphore_signal(semaphore);
            return;
        }
        
        // Start capture
        [stream startCaptureWithCompletionHandler:^(NSError *error) {
            if (error) {
                capture_error = error;
                dispatch_semaphore_signal(semaphore);
            }
            // Frame will be delivered via frameHandler
        }];
    }];
    
    // Wait for capture to complete (with timeout)
    dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC);
    dispatch_semaphore_wait(semaphore, timeout);
    
    if (capture_error || !capture_complete || result.empty()) {
        return cv::Mat();
    }
    
    return result;
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

        // Detect rectangles using OpenCV
        std::vector<cv::Rect> rectangles = detect_rectangles(screenshot);

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
