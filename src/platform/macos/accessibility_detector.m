/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * macOS UI Element Detector using Accessibility API
 */

#include "../../platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

/* Forward declarations for OpenCV fallback */
extern struct ui_detection_result *opencv_detect_ui_elements(void);
extern void opencv_free_ui_elements(struct ui_detection_result *result);
extern int opencv_is_available(void);

/**
 * Convert AXElement to platform ui_element
 */
static void convert_ax_element(AXUIElementRef element, struct ui_element *dest)
{
    // Get position and size
    AXValueRef position = NULL;
    AXValueRef size = NULL;
    CGPoint point = CGPointZero;
    CGSize dimensions = CGSizeZero;

    // Get position
    if (AXUIElementCopyAttributeValue(element, kAXPositionAttribute, (CFTypeRef *)&position) == kAXErrorSuccess) {
        if (AXValueGetValue(position, kAXValueCGPointType, &point)) {
            dest->x = (int)point.x;
            dest->y = (int)point.y;
        }
        CFRelease(position);
    }

    // Get size
    if (AXUIElementCopyAttributeValue(element, kAXSizeAttribute, (CFTypeRef *)&size) == kAXErrorSuccess) {
        if (AXValueGetValue(size, kAXValueCGSizeType, &dimensions)) {
            dest->w = (int)dimensions.width;
            dest->h = (int)dimensions.height;
        }
        CFRelease(size);
    }

    // Get name/description
    CFStringRef name = NULL;
    if (AXUIElementCopyAttributeValue(element, kAXDescriptionAttribute, (CFTypeRef *)&name) == kAXErrorSuccess) {
        if (name) {
            CFIndex length = CFStringGetLength(name) + 1;
            dest->name = (char *)malloc(length);
            if (dest->name) {
                CFStringGetCString(name, dest->name, length, kCFStringEncodingUTF8);
            }
            CFRelease(name);
        }
    }

    // If no description, try title
    if (!dest->name) {
        CFStringRef title = NULL;
        if (AXUIElementCopyAttributeValue(element, kAXTitleAttribute, (CFTypeRef *)&title) == kAXErrorSuccess) {
            if (title) {
                CFIndex length = CFStringGetLength(title) + 1;
                dest->name = (char *)malloc(length);
                if (dest->name) {
                    CFStringGetCString(title, dest->name, length, kCFStringEncodingUTF8);
                }
                CFRelease(title);
            }
        }
    }

    // Get role
    CFStringRef role = NULL;
    if (AXUIElementCopyAttributeValue(element, kAXRoleAttribute, (CFTypeRef *)&role) == kAXErrorSuccess) {
        if (role) {
            CFIndex length = CFStringGetLength(role) + 1;
            dest->role = (char *)malloc(length);
            if (dest->role) {
                CFStringGetCString(role, dest->role, length, kCFStringEncodingUTF8);
            }
            CFRelease(role);
        }
    }
}

/**
 * Check if element is interactive/clickable
 */
static int is_interactive_element(AXUIElementRef element)
{
    if (!element)
        return 0;

    // Check if element is enabled
    CFBooleanRef enabled = NULL;
    int is_enabled = 1; // Assume enabled by default
    if (AXUIElementCopyAttributeValue(element, kAXEnabledAttribute, (CFTypeRef *)&enabled) == kAXErrorSuccess) {
        if (enabled) {
            is_enabled = CFBooleanGetValue(enabled);
            CFRelease(enabled);
        }
    }

    if (!is_enabled)
        return 0;

    // Check role to determine if interactive
    CFStringRef role = NULL;
    int is_interactive = 0;

    if (AXUIElementCopyAttributeValue(element, kAXRoleAttribute, (CFTypeRef *)&role) == kAXErrorSuccess) {
        if (role) {
            CFStringRef interactive_roles[] = {
                kAXButtonRole,
                kAXCheckBoxRole,
                kAXComboBoxRole,
                CFSTR("AXEditTextField"),  // Alternative to kAXTextFieldRole
                CFSTR("AXLink"),           // Link role
                kAXListRole,
                kAXMenuButtonRole,
                kAXMenuItemRole,
                kAXPopUpButtonRole,
                kAXRadioButtonRole,
                kAXScrollBarRole,
                kAXSliderRole,
                kAXStaticTextRole,  // Include text for navigation
                kAXTabGroupRole,
                CFSTR("AXTextField"),  // Text field
                CFSTR("AXToggle"),     // Toggle elements
                kAXToolbarRole,
                CFSTR("AXCell"),  // Table cells
                CFSTR("AXRow"),   // Table rows
                NULL
            };

            for (int i = 0; interactive_roles[i] != NULL; i++) {
                if (CFStringCompare(role, interactive_roles[i], 0) == kCFCompareEqualTo) {
                    is_interactive = 1;
                    break;
                }
            }

            CFRelease(role);
        }
    }

    // Additional check: if element has action, it's interactive
    if (!is_interactive) {
        CFArrayRef actions = NULL;
        if (AXUIElementCopyActionNames(element, (CFArrayRef *)&actions) == kAXErrorSuccess) {
            if (actions && CFArrayGetCount(actions) > 0) {
                is_interactive = 1;
            }
            if (actions) {
                CFRelease(actions);
            }
        }
    }

    return is_interactive;
}

/**
 * Recursively collect interactive elements
 */
static void collect_interactive_elements(AXUIElementRef element, CFMutableArrayRef elements)
{
    if (!element || !elements)
        return;

    // Check if current element is interactive
    if (is_interactive_element(element)) {
        CFRetain(element);
        CFArrayAppendValue(elements, element);
    }

    // Get children
    CFArrayRef children = NULL;
    if (AXUIElementCopyAttributeValue(element, kAXChildrenAttribute, (CFTypeRef *)&children) == kAXErrorSuccess) {
        if (children) {
            CFIndex count = CFArrayGetCount(children);
            for (CFIndex i = 0; i < count; i++) {
                AXUIElementRef child = (AXUIElementRef)CFArrayGetValueAtIndex(children, i);
                if (child) {
                    collect_interactive_elements(child, elements);
                }
            }
            CFRelease(children);
        }
    }
}

/**
 * Detect UI elements using macOS Accessibility API
 */
static struct ui_detection_result *accessibility_detect_ui_elements(void)
{
    struct ui_detection_result *result = calloc(1, sizeof(*result));
    if (!result)
        return NULL;

    // Check accessibility permissions
    if (!AXIsProcessTrusted()) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Accessibility permissions not granted. Please enable in System Preferences > Security & Privacy > Privacy > Accessibility");
        result->error = -1;
        return result;
    }

    // Get focused application
    AXUIElementRef focused_app = NULL;
    pid_t focused_pid = 0;

    if (AXUIElementCopyAttributeValue(AXUIElementCreateSystemWide(), kAXFocusedApplicationAttribute, (CFTypeRef *)&focused_app) != kAXErrorSuccess) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Could not get focused application");
        result->error = -2;
        return result;
    }

    if (!focused_app) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "No focused application");
        result->error = -3;
        return result;
    }

    // Get focused window
    AXUIElementRef focused_window = NULL;
    if (AXUIElementCopyAttributeValue(focused_app, kAXFocusedWindowAttribute, (CFTypeRef *)&focused_window) != kAXErrorSuccess) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Could not get focused window");
        CFRelease(focused_app);
        result->error = -4;
        return result;
    }

    if (!focused_window) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "No focused window");
        CFRelease(focused_app);
        result->error = -5;
        return result;
    }

    // Collect interactive elements
    CFMutableArrayRef elements = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if (!elements) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Memory allocation failed");
        CFRelease(focused_window);
        CFRelease(focused_app);
        result->error = -6;
        return result;
    }

    collect_interactive_elements(focused_window, elements);

    // Convert to platform format
    CFIndex count = CFArrayGetCount(elements);
    if (count == 0) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "No interactive elements detected");
        result->error = -7;
        CFRelease(elements);
        CFRelease(focused_window);
        CFRelease(focused_app);
        return result;
    }

    result->elements = calloc(count, sizeof(struct ui_element));
    if (!result->elements) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Memory allocation failed");
        CFRelease(elements);
        CFRelease(focused_window);
        CFRelease(focused_app);
        result->error = -8;
        return result;
    }

    // Convert elements
    for (CFIndex i = 0; i < count; i++) {
        AXUIElementRef element = (AXUIElementRef)CFArrayGetValueAtIndex(elements, i);
        if (element) {
            convert_ax_element(element, &result->elements[i]);
        }
    }

    result->count = count;
    result->error = 0;

    // Cleanup
    CFRelease(elements);
    CFRelease(focused_window);
    CFRelease(focused_app);

    return result;
}

/**
 * Free accessibility detection result
 */
static void accessibility_free_ui_elements(struct ui_detection_result *result)
{
    if (!result)
        return;

    if (result->elements) {
        for (size_t i = 0; i < result->count; i++) {
            if (result->elements[i].name)
                free(result->elements[i].name);
            if (result->elements[i].role)
                free(result->elements[i].role);
        }
        free(result->elements);
    }

    free(result);
}

/**
 * Detect UI elements with Accessibility API primary, OpenCV fallback
 */
struct ui_detection_result *macos_detect_ui_elements(void)
{
    /* Try Accessibility API first */
    struct ui_detection_result *result = accessibility_detect_ui_elements();

    /* If Accessibility API failed and OpenCV is available, try OpenCV fallback */
    if (result && result->error != 0 && opencv_is_available()) {
        fprintf(stderr, "Accessibility API failed (%s), falling back to OpenCV\n", result->error_msg);
        accessibility_free_ui_elements(result);
        return opencv_detect_ui_elements();
    }

    return result;
}

/**
 * Free UI detection result
 */
void macos_free_ui_elements(struct ui_detection_result *result)
{
    if (!result)
        return;

    // Check if this is an accessibility result or OpenCV result
    // For now, we'll use the accessibility free function for all
    // since it handles the memory layout correctly
    accessibility_free_ui_elements(result);
}