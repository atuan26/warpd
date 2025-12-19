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
#include "../../common/opencv_detector.h"

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

    CFStringRef description = NULL;
    CFStringRef title = NULL;
    CFStringRef value = NULL;
    CFStringRef label = NULL;
    
    char descStr[256] = {0};
    char titleStr[256] = {0};
    char valueStr[256] = {0};
    char labelStr[256] = {0};

    if (AXUIElementCopyAttributeValue(element, kAXDescriptionAttribute, (CFTypeRef *)&description) == kAXErrorSuccess && description) {
        CFStringGetCString(description, descStr, sizeof(descStr), kCFStringEncodingUTF8);
        CFRelease(description);
    }
    
    if (AXUIElementCopyAttributeValue(element, kAXTitleAttribute, (CFTypeRef *)&title) == kAXErrorSuccess && title) {
        CFStringGetCString(title, titleStr, sizeof(titleStr), kCFStringEncodingUTF8);
        CFRelease(title);
    }
    
    if (AXUIElementCopyAttributeValue(element, kAXValueAttribute, (CFTypeRef *)&value) == kAXErrorSuccess && value) {
        if (CFGetTypeID(value) == CFStringGetTypeID()) {
            CFStringGetCString((CFStringRef)value, valueStr, sizeof(valueStr), kCFStringEncodingUTF8);
        }
        CFRelease(value);
    }
    
    if (AXUIElementCopyAttributeValue(element, CFSTR("AXLabel"), (CFTypeRef *)&label) == kAXErrorSuccess && label) {
        CFStringGetCString(label, labelStr, sizeof(labelStr), kCFStringEncodingUTF8);
        CFRelease(label);
    }

    static int debug_count = 0;
    if (debug_count < 5) {
        fprintf(stderr, "DEBUG RAW [macOS]: Description='%s' Title='%s' Value='%s' Label='%s'\n",
            descStr, titleStr, valueStr, labelStr);
        debug_count++;
    }

    if (descStr[0]) {
        dest->name = strdup(descStr);
    } else if (titleStr[0]) {
        dest->name = strdup(titleStr);
    } else if (labelStr[0]) {
        dest->name = strdup(labelStr);
    } else if (valueStr[0]) {
        dest->name = strdup(valueStr);
    } else {
        dest->name = NULL;
    }

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
 * Dump UI tree to file for debugging
 */
static void dump_element_tree(FILE *fp, AXUIElementRef element, int depth, int max_depth)
{
    if (!element || depth > max_depth)
        return;
    
    for (int i = 0; i < depth * 2; i++)
        fprintf(fp, " ");
    
    CFStringRef role = NULL;
    CFStringRef title = NULL;
    CFStringRef desc = NULL;
    AXValueRef position = NULL;
    AXValueRef size = NULL;
    CGPoint point = CGPointZero;
    CGSize dimensions = CGSizeZero;
    
    char roleStr[256] = {0};
    char titleStr[256] = {0};
    char descStr[256] = {0};
    
    if (AXUIElementCopyAttributeValue(element, kAXRoleAttribute, (CFTypeRef *)&role) == kAXErrorSuccess && role) {
        CFStringGetCString(role, roleStr, sizeof(roleStr), kCFStringEncodingUTF8);
        CFRelease(role);
    }
    
    if (AXUIElementCopyAttributeValue(element, kAXTitleAttribute, (CFTypeRef *)&title) == kAXErrorSuccess && title) {
        CFStringGetCString(title, titleStr, sizeof(titleStr), kCFStringEncodingUTF8);
        CFRelease(title);
    }
    
    if (AXUIElementCopyAttributeValue(element, kAXDescriptionAttribute, (CFTypeRef *)&desc) == kAXErrorSuccess && desc) {
        CFStringGetCString(desc, descStr, sizeof(descStr), kCFStringEncodingUTF8);
        CFRelease(desc);
    }
    
    int x = 0, y = 0, w = 0, h = 0;
    if (AXUIElementCopyAttributeValue(element, kAXPositionAttribute, (CFTypeRef *)&position) == kAXErrorSuccess) {
        if (AXValueGetValue(position, kAXValueCGPointType, &point)) {
            x = (int)point.x;
            y = (int)point.y;
        }
        CFRelease(position);
    }
    
    if (AXUIElementCopyAttributeValue(element, kAXSizeAttribute, (CFTypeRef *)&size) == kAXErrorSuccess) {
        if (AXValueGetValue(size, kAXValueCGSizeType, &dimensions)) {
            w = (int)dimensions.width;
            h = (int)dimensions.height;
        }
        CFRelease(size);
    }
    
    fprintf(fp, "[%s] name='%s' x=%d y=%d w=%d h=%d", 
        roleStr[0] ? roleStr : "unknown",
        titleStr[0] ? titleStr : (descStr[0] ? descStr : ""),
        x, y, w, h);
    
    if (descStr[0] && titleStr[0])
        fprintf(fp, " desc='%s'", descStr);
    
    fprintf(fp, "\n");
    
    CFArrayRef children = NULL;
    if (AXUIElementCopyAttributeValue(element, kAXChildrenAttribute, (CFTypeRef *)&children) == kAXErrorSuccess) {
        if (children) {
            CFIndex count = CFArrayGetCount(children);
            for (CFIndex i = 0; i < count; i++) {
                AXUIElementRef child = (AXUIElementRef)CFArrayGetValueAtIndex(children, i);
                if (child) {
                    dump_element_tree(fp, child, depth + 1, max_depth);
                }
            }
            CFRelease(children);
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
            // High-value interactive elements (prioritize these)
            CFStringRef high_priority_roles[] = {
                kAXButtonRole,
                kAXCheckBoxRole,
                kAXRadioButtonRole,
                kAXPopUpButtonRole,
                kAXMenuButtonRole,
                kAXMenuItemRole,
                kAXComboBoxRole,
                CFSTR("AXTextField"),
                CFSTR("AXEditTextField"),
                CFSTR("AXLink"),           // Link role
                NULL
            };

            // Medium-value elements (include if space allows)
            CFStringRef medium_priority_roles[] = {
                kAXListRole,
                kAXTabGroupRole,
                kAXToolbarRole,
                CFSTR("AXCell"),
                CFSTR("AXRow"),
                kAXScrollBarRole,
                kAXSliderRole,
                NULL
            };

            // Low-value elements (only include if very few others)
            CFStringRef low_priority_roles[] = {
                kAXStaticTextRole,  // Only large text blocks
                NULL
            };

            // Check high priority roles first
            for (int i = 0; high_priority_roles[i] != NULL; i++) {
                if (CFStringCompare(role, high_priority_roles[i], 0) == kCFCompareEqualTo) {
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
                // Check if it has meaningful actions
                int has_meaningful_action = 0;
                CFIndex action_count = CFArrayGetCount(actions);
                for (CFIndex i = 0; i < action_count; i++) {
                    CFStringRef action = (CFStringRef)CFArrayGetValueAtIndex(actions, i);
                    if (action) {
                        CFStringRef meaningful_actions[] = {
                            CFSTR("AXPress"),
                            CFSTR("AXClick"),
                            CFSTR("AXSelect"),
                            CFSTR("AXToggle"),
                            NULL
                        };

                        for (int j = 0; meaningful_actions[j] != NULL; j++) {
                            if (CFStringCompare(action, meaningful_actions[j], 0) == kCFCompareEqualTo) {
                                has_meaningful_action = 1;
                                break;
                            }
                        }
                    }
                }

                if (has_meaningful_action) {
                    is_interactive = 1;
                }
            }
            if (actions) {
                CFRelease(actions);
            }
        }
    }

    return is_interactive;
}

/**
 * Check if element should be included based on size and position
 */
static int should_include_element(AXUIElementRef element)
{
    // Get size
    AXValueRef size = NULL;
    CGSize dimensions = CGSizeZero;

    if (AXUIElementCopyAttributeValue(element, kAXSizeAttribute, (CFTypeRef *)&size) == kAXErrorSuccess) {
        if (AXValueGetValue(size, kAXValueCGSizeType, &dimensions)) {
            CFRelease(size);

            // Filter out very small elements (likely decorative)
            if (dimensions.width < 10 || dimensions.height < 10) {
                return 0;
            }

            // Filter out very large elements (likely containers)
            if (dimensions.width > 2000 || dimensions.height > 2000) {
                return 0;
            }

            return 1;
        }
        if (size) {
            CFRelease(size);
        }
    }

    return 1; // Include if we can't determine size
}

/**
 * Recursively collect interactive elements
 */
static void collect_interactive_elements(AXUIElementRef element, CFMutableArrayRef elements)
{
    if (!element || !elements)
        return;

    // Check if current element is interactive and should be included
    if (is_interactive_element(element) && should_include_element(element)) {
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
    AXUIElementRef focused_window = NULL;
    int use_screen_wide = 0;

    if (AXUIElementCopyAttributeValue(AXUIElementCreateSystemWide(), kAXFocusedApplicationAttribute, (CFTypeRef *)&focused_app) != kAXErrorSuccess) {
        use_screen_wide = 1; // Fall back to screen-wide detection
    }

    if (!use_screen_wide && focused_app) {
        // Try to get focused window
        if (AXUIElementCopyAttributeValue(focused_app, kAXFocusedWindowAttribute, (CFTypeRef *)&focused_window) != kAXErrorSuccess) {
            use_screen_wide = 1; // Fall back to screen-wide detection
            CFRelease(focused_app);
            focused_app = NULL;
        }
    }

    if (use_screen_wide || !focused_window) {
        // Screen-wide detection: get all applications and their windows
        CFArrayRef apps = NULL;
        if (AXUIElementCopyAttributeValue(AXUIElementCreateSystemWide(), kAXWindowsAttribute, (CFTypeRef *)&apps) != kAXErrorSuccess) {
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Could not get any windows");
            if (focused_app) CFRelease(focused_app);
            result->error = -2;
            return result;
        }

        if (!apps || CFArrayGetCount(apps) == 0) {
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "No windows found");
            if (focused_app) CFRelease(focused_app);
            if (apps) CFRelease(apps);
            result->error = -3;
            return result;
        }

        // Collect elements from all visible windows
        CFMutableArrayRef elements = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
        if (!elements) {
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Memory allocation failed");
            if (focused_app) CFRelease(focused_app);
            CFRelease(apps);
            result->error = -4;
            return result;
        }

        CFIndex app_count = CFArrayGetCount(apps);
        for (CFIndex i = 0; i < app_count; i++) {
            AXUIElementRef window = (AXUIElementRef)CFArrayGetValueAtIndex(apps, i);
            if (window) {
                // Check if window is visible and on current screen
                CFBooleanRef is_minimized = NULL;
                int should_process = 1;

                if (AXUIElementCopyAttributeValue(window, kAXMinimizedAttribute, (CFTypeRef *)&is_minimized) == kAXErrorSuccess) {
                    if (is_minimized) {
                        should_process = CFBooleanGetValue(is_minimized) ? 0 : 1;
                        CFRelease(is_minimized);
                    }
                }

                if (should_process) {
                    collect_interactive_elements(window, elements);
                }
            }
        }

        CFRelease(apps);

        // Convert to platform format
        CFIndex count = CFArrayGetCount(elements);
        if (count == 0) {
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "No interactive elements detected");
            result->error = -5;
            CFRelease(elements);
            if (focused_app) CFRelease(focused_app);
            return result;
        }

        result->elements = calloc(count, sizeof(struct ui_element));
        if (!result->elements) {
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Memory allocation failed");
            CFRelease(elements);
            if (focused_app) CFRelease(focused_app);
            result->error = -6;
            return result;
        }

        // Convert elements
        for (CFIndex i = 0; i < count; i++) {
            AXUIElementRef element = (AXUIElementRef)CFArrayGetValueAtIndex(elements, i);
            if (element) {
                convert_ax_element(element, &result->elements[i]);

                // Debug output: print detected element info
                fprintf(stderr, "DEBUG: Element %ld: x=%d, y=%d, w=%d, h=%d, name='%s', role='%s'\n",
                        (long)i,
                        result->elements[i].x,
                        result->elements[i].y,
                        result->elements[i].w,
                        result->elements[i].h,
                        result->elements[i].name ? result->elements[i].name : "(null)",
                        result->elements[i].role ? result->elements[i].role : "(null)");
            }
        }

        result->count = count;
        result->error = 0;

        // Remove overlapping elements to reduce crowding
        remove_overlapping_elements(result);

        CFRelease(elements);
        if (focused_app) CFRelease(focused_app);

        return result;
    }

    // Original focused window detection path
    if (!focused_window) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "No focused window");
        if (focused_app) CFRelease(focused_app);
        result->error = -7;
        return result;
    }

    // Collect interactive elements from focused window
    CFMutableArrayRef elements = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if (!elements) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Memory allocation failed");
        CFRelease(focused_window);
        CFRelease(focused_app);
        result->error = -8;
        return result;
    }

    // FILE *dump_fp = fopen("ui_tree_dump_macos.txt", "w");
    // if (dump_fp) {
    //     fprintf(dump_fp, "macOS Accessibility API Tree Dump\n");
    //     fprintf(dump_fp, "==================================\n\n");
    //     dump_element_tree(dump_fp, focused_window, 0, 25);
    //     fclose(dump_fp);
    //     fprintf(stderr, "macOS: Tree dumped to ui_tree_dump_macos.txt\n");
    // }
    
    collect_interactive_elements(focused_window, elements);

    // Convert to platform format
    CFIndex count = CFArrayGetCount(elements);
    if (count == 0) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "No interactive elements detected");
        result->error = -9;
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
        result->error = -10;
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

    remove_overlapping_elements(result);

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