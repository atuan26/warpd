/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * macOS Accessibility API UI Element Detector
 */

#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include "../../platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Check if element is interactive (clickable)
 */
static BOOL is_interactive_element(AXUIElementRef element)
{
    CFStringRef role = NULL;
    CFStringRef subrole = NULL;
    
    // Get role
    if (AXUIElementCopyAttributeValue(element, kAXRoleAttribute, (CFTypeRef*)&role) != kAXErrorSuccess) {
        return NO;
    }
    
    // Get subrole (optional)
    AXUIElementCopyAttributeValue(element, kAXSubroleAttribute, (CFTypeRef*)&subrole);
    
    BOOL interactive = NO;
    
    // Check for interactive roles
    if (CFStringCompare(role, kAXButtonRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXCheckBoxRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXRadioButtonRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXPopUpButtonRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXMenuButtonRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXTextFieldRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXTextAreaRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXComboBoxRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXSliderRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXTabRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXLinkRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXMenuItemRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXListRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXTableRole, 0) == kCFCompareEqualTo ||
        CFStringCompare(role, kAXOutlineRole, 0) == kCFCompareEqualTo) {
        interactive = YES;
    }
    
    // Check for clickable subroles
    if (subrole) {
        if (CFStringCompare(subrole, kAXCloseButtonSubrole, 0) == kCFCompareEqualTo ||
            CFStringCompare(subrole, kAXMinimizeButtonSubrole, 0) == kCFCompareEqualTo ||
            CFStringCompare(subrole, kAXZoomButtonSubrole, 0) == kCFCompareEqualTo ||
            CFStringCompare(subrole, kAXToolbarButtonSubrole, 0) == kCFCompareEqualTo) {
            interactive = YES;
        }
        CFRelease(subrole);
    }
    
    CFRelease(role);
    return interactive;
}

/**
 * Get element bounds
 */
static BOOL get_element_bounds(AXUIElementRef element, int *x, int *y, int *w, int *h)
{
    CFTypeRef position = NULL;
    CFTypeRef size = NULL;
    
    if (AXUIElementCopyAttributeValue(element, kAXPositionAttribute, &position) != kAXErrorSuccess ||
        AXUIElementCopyAttributeValue(element, kAXSizeAttribute, &size) != kAXErrorSuccess) {
        if (position) CFRelease(position);
        if (size) CFRelease(size);
        return NO;
    }
    
    CGPoint pos;
    CGSize sz;
    
    if (!AXValueGetValue(position, kAXValueCGPointType, &pos) ||
        !AXValueGetValue(size, kAXValueCGSizeType, &sz)) {
        CFRelease(position);
        CFRelease(size);
        return NO;
    }
    
    *x = (int)pos.x;
    *y = (int)pos.y;
    *w = (int)sz.width;
    *h = (int)sz.height;
    
    CFRelease(position);
    CFRelease(size);
    return YES;
}

/**
 * Get element name/title
 */
static char* get_element_name(AXUIElementRef element)
{
    CFStringRef title = NULL;
    CFStringRef description = NULL;
    CFStringRef name = NULL;
    
    // Try title first
    if (AXUIElementCopyAttributeValue(element, kAXTitleAttribute, (CFTypeRef*)&title) == kAXErrorSuccess && title) {
        if (CFStringGetLength(title) > 0) {
            name = title;
        } else {
            CFRelease(title);
            title = NULL;
        }
    }
    
    // Try description if no title
    if (!name && AXUIElementCopyAttributeValue(element, kAXDescriptionAttribute, (CFTypeRef*)&description) == kAXErrorSuccess && description) {
        if (CFStringGetLength(description) > 0) {
            name = description;
        } else {
            CFRelease(description);
            description = NULL;
        }
    }
    
    if (!name) {
        if (title) CFRelease(title);
        if (description) CFRelease(description);
        return NULL;
    }
    
    // Convert to C string
    CFIndex length = CFStringGetLength(name);
    CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    char *cString = malloc(maxSize);
    
    if (CFStringGetCString(name, cString, maxSize, kCFStringEncodingUTF8)) {
        if (title) CFRelease(title);
        if (description) CFRelease(description);
        return cString;
    }
    
    free(cString);
    if (title) CFRelease(title);
    if (description) CFRelease(description);
    return NULL;
}

/**
 * Get element role as string
 */
static char* get_element_role(AXUIElementRef element)
{
    CFStringRef role = NULL;
    
    if (AXUIElementCopyAttributeValue(element, kAXRoleAttribute, (CFTypeRef*)&role) != kAXErrorSuccess || !role) {
        return NULL;
    }
    
    CFIndex length = CFStringGetLength(role);
    CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    char *cString = malloc(maxSize);
    
    if (CFStringGetCString(role, cString, maxSize, kCFStringEncodingUTF8)) {
        CFRelease(role);
        return cString;
    }
    
    free(cString);
    CFRelease(role);
    return NULL;
}

/**
 * Recursively collect interactive elements
 */
static void collect_interactive_elements(AXUIElementRef element, NSMutableArray *elements, int depth)
{
    if (depth > 10) return; // Prevent infinite recursion
    
    // Check if this element is interactive
    if (is_interactive_element(element)) {
        int x, y, w, h;
        if (get_element_bounds(element, &x, &y, &w, &h)) {
            // Filter out tiny elements and off-screen elements
            if (w >= 10 && h >= 10 && x >= 0 && y >= 0) {
                [elements addObject:(__bridge id)element];
            }
        }
    }
    
    // Get children
    CFArrayRef children = NULL;
    if (AXUIElementCopyAttributeValue(element, kAXChildrenAttribute, (CFTypeRef*)&children) == kAXErrorSuccess && children) {
        CFIndex count = CFArrayGetCount(children);
        for (CFIndex i = 0; i < count; i++) {
            AXUIElementRef child = (AXUIElementRef)CFArrayGetValueAtIndex(children, i);
            collect_interactive_elements(child, elements, depth + 1);
        }
        CFRelease(children);
    }
}

/**
 * Check if Accessibility API is available
 */
int accessibility_is_available(void)
{
    // Check if we have accessibility permissions
    NSDictionary *options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @NO};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options) ? 1 : 0;
}

/**
 * Detect UI elements using macOS Accessibility API
 */
struct ui_detection_result *accessibility_detect_ui_elements(void)
{
    struct ui_detection_result *result = calloc(1, sizeof(*result));
    if (!result) return NULL;
    
    @autoreleasepool {
        // Check accessibility permissions
        if (!accessibility_is_available()) {
            result->error = -1;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Accessibility permissions not granted. Please enable in System Preferences > Security & Privacy > Privacy > Accessibility");
            return result;
        }
        
        // Get frontmost application
        NSRunningApplication *frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (!frontApp) {
            result->error = -2;
            snprintf(result->error_msg, sizeof(result->error_msg), "No frontmost application");
            return result;
        }
        
        // Create AX application reference
        AXUIElementRef appRef = AXUIElementCreateApplication([frontApp processIdentifier]);
        if (!appRef) {
            result->error = -3;
            snprintf(result->error_msg, sizeof(result->error_msg), "Failed to create AX application reference");
            return result;
        }
        
        // Get focused window
        CFTypeRef focusedWindow = NULL;
        if (AXUIElementCopyAttributeValue(appRef, kAXFocusedWindowAttribute, &focusedWindow) != kAXErrorSuccess || !focusedWindow) {
            // Try to get main window instead
            if (AXUIElementCopyAttributeValue(appRef, kAXMainWindowAttribute, &focusedWindow) != kAXErrorSuccess || !focusedWindow) {
                CFRelease(appRef);
                result->error = -4;
                snprintf(result->error_msg, sizeof(result->error_msg), "No focused or main window");
                return result;
            }
        }
        
        // Collect interactive elements
        NSMutableArray *elementArray = [NSMutableArray array];
        collect_interactive_elements((AXUIElementRef)focusedWindow, elementArray, 0);
        
        CFRelease(focusedWindow);
        CFRelease(appRef);
        
        if ([elementArray count] == 0) {
            result->error = -5;
            snprintf(result->error_msg, sizeof(result->error_msg), "No interactive elements found");
            return result;
        }
        
        // Convert to ui_elements
        size_t count = [elementArray count];
        if (count > MAX_UI_ELEMENTS) {
            count = MAX_UI_ELEMENTS;
        }
        
        result->elements = calloc(count, sizeof(struct ui_element));
        if (!result->elements) {
            result->error = -6;
            snprintf(result->error_msg, sizeof(result->error_msg), "Memory allocation failed");
            return result;
        }
        
        size_t valid_count = 0;
        for (NSUInteger i = 0; i < count; i++) {
            AXUIElementRef element = (__bridge AXUIElementRef)[elementArray objectAtIndex:i];
            
            int x, y, w, h;
            if (get_element_bounds(element, &x, &y, &w, &h)) {
                result->elements[valid_count].x = x;
                result->elements[valid_count].y = y;
                result->elements[valid_count].w = w;
                result->elements[valid_count].h = h;
                result->elements[valid_count].name = get_element_name(element);
                result->elements[valid_count].role = get_element_role(element);
                valid_count++;
            }
        }
        
        result->count = valid_count;
        result->error = 0;
    }
    
    return result;
}

/**
 * Free Accessibility API detection result
 */
void accessibility_free_ui_elements(struct ui_detection_result *result)
{
    if (!result) return;
    
    if (result->elements) {
        for (size_t i = 0; i < result->count; i++) {
            if (result->elements[i].name) {
                free(result->elements[i].name);
            }
            if (result->elements[i].role) {
                free(result->elements[i].role);
            }
        }
        free(result->elements);
    }
    
    free(result);
}