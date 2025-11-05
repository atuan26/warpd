/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * Windows UI Automation C++ wrapper with C interface
 * This provides a C-compatible interface to Windows UI Automation API
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <uiautomation.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Global UI Automation objects
static IUIAutomation* g_pAutomation = nullptr;
static IUIAutomationTreeWalker* g_pTreeWalker = nullptr;
static bool g_initialized = false;
static int max_depth_reached = 0;

/**
 * Initialize UI Automation
 */
static bool initialize_uiautomation()
{
    if (g_initialized)
        return true;

    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr))
        return false;

    hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(IUIAutomation), (void**)&g_pAutomation);
    if (FAILED(hr)) {
        CoUninitialize();
        return false;
    }

    // Cache the tree walker for better performance
    hr = g_pAutomation->get_ControlViewWalker(&g_pTreeWalker);
    if (FAILED(hr)) {
        g_pAutomation->Release();
        g_pAutomation = nullptr;
        CoUninitialize();
        return false;
    }

    g_initialized = true;
    return true;
}

/**
 * Cleanup UI Automation
 */
static void cleanup_uiautomation()
{
    if (g_initialized) {
        if (g_pTreeWalker) {
            g_pTreeWalker->Release();
            g_pTreeWalker = nullptr;
        }
        if (g_pAutomation) {
            g_pAutomation->Release();
            g_pAutomation = nullptr;
        }
        CoUninitialize();
        g_initialized = false;
    }
}

/**
 * Convert BSTR to UTF-8 string
 */
static std::string bstr_to_utf8(BSTR bstr)
{
    if (!bstr)
        return "";

    int len = WideCharToMultiByte(CP_UTF8, 0, bstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return "";

    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, bstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}

/**
 * Check if element is interactive (optimized version)
 */
static bool is_interactive_element(IUIAutomationElement* element)
{
    if (!element)
        return false;

    CONTROLTYPEID controlType;
    HRESULT hr = element->get_CurrentControlType(&controlType);
    if (FAILED(hr))
        return false;

    // Check for obviously interactive control types first (fastest path)
    switch (controlType) {
        case UIA_ButtonControlTypeId:
        case UIA_CheckBoxControlTypeId:
        case UIA_ComboBoxControlTypeId:
        case UIA_EditControlTypeId:
        case UIA_HyperlinkControlTypeId:
        case UIA_ListItemControlTypeId:
        case UIA_MenuItemControlTypeId:
        case UIA_RadioButtonControlTypeId:
        case UIA_SliderControlTypeId:
        case UIA_SpinnerControlTypeId:
        case UIA_TabItemControlTypeId:
            return true;
        case UIA_TextControlTypeId:
        case UIA_TreeItemControlTypeId:
            // These might be interactive, check enabled state
            break;
        default:
            // For other types, we need to check patterns
            break;
    }

    // Check if element is enabled (required for interactivity)
    BOOL isEnabled = FALSE;
    hr = element->get_CurrentIsEnabled(&isEnabled);
    if (FAILED(hr) || !isEnabled)
        return false;

    // For text and tree items, being enabled is enough
    if (controlType == UIA_TextControlTypeId || controlType == UIA_TreeItemControlTypeId)
        return true;

    // For other types, check for interaction patterns (but limit to most common ones)
    // Check for invoke pattern first (most common)
    IUIAutomationInvokePattern* invokePattern = nullptr;
    hr = element->GetCurrentPattern(UIA_InvokePatternId, (IUnknown**)&invokePattern);
    if (SUCCEEDED(hr) && invokePattern) {
        invokePattern->Release();
        return true;
    }

    // Only check other patterns if invoke pattern failed
    IUIAutomationSelectionItemPattern* selectionPattern = nullptr;
    hr = element->GetCurrentPattern(UIA_SelectionItemPatternId, (IUnknown**)&selectionPattern);
    if (SUCCEEDED(hr) && selectionPattern) {
        selectionPattern->Release();
        return true;
    }

    return false;
}

/**
 * Get element bounding rectangle
 */
static bool get_element_rect(IUIAutomationElement* element, RECT* rect)
{
    if (!element || !rect)
        return false;

    HRESULT hr = element->get_CurrentBoundingRectangle(rect);
    return SUCCEEDED(hr) && rect->right > rect->left && rect->bottom > rect->top;
}

/**
 * Check if element is actually visible within the window bounds
 * This handles clipping by parent containers and window boundaries
 */
static bool check_is_actually_visible(IUIAutomationElement* element, HWND window)
{
    RECT elemRect, winRect;
    
    // Get element rectangle
    if (!get_element_rect(element, &elemRect)) {
        return false;
    }
    
    // Get window rectangle
    if (!GetWindowRect(window, &winRect)) {
        return true; // If we can't get window bounds, assume visible
    }
    
    // Check if element is completely outside window bounds
    if (elemRect.left >= winRect.right ||   // Element starts after window ends
        elemRect.top >= winRect.bottom ||   // Element starts below window
        elemRect.right <= winRect.left ||   // Element ends before window starts
        elemRect.bottom <= winRect.top) {   // Element ends above window
        return false;
    }
    
    // Check if element has reasonable overlap with window
    LONG overlapLeft = max(elemRect.left, winRect.left);
    LONG overlapTop = max(elemRect.top, winRect.top);
    LONG overlapRight = min(elemRect.right, winRect.right);
    LONG overlapBottom = min(elemRect.bottom, winRect.bottom);
    
    LONG overlapWidth = overlapRight - overlapLeft;
    LONG overlapHeight = overlapBottom - overlapTop;
    
    if (overlapWidth <= 0 || overlapHeight <= 0) {
        return false;
    }
    
    // Calculate visible area
    LONG visibleArea = overlapWidth * overlapHeight;
    LONG totalArea = (elemRect.right - elemRect.left) * (elemRect.bottom - elemRect.top);
    
    // Require at least 50% of element to be visible, or minimum 100 pixels
    return (visibleArea >= totalArea / 2) || (visibleArea >= 100);
}

/**
 * Get element name
 */
static std::string get_element_name(IUIAutomationElement* element)
{
    if (!element)
        return "";

    BSTR name = nullptr;
    HRESULT hr = element->get_CurrentName(&name);
    if (FAILED(hr) || !name)
        return "";

    std::string result = bstr_to_utf8(name);
    SysFreeString(name);
    return result;
}

/**
 * Get element control type name
 */
static std::string get_element_type(IUIAutomationElement* element)
{
    if (!element)
        return "";

    CONTROLTYPEID controlType;
    HRESULT hr = element->get_CurrentControlType(&controlType);
    if (FAILED(hr))
        return "";

    switch (controlType) {
        case UIA_ButtonControlTypeId: return "button";
        case UIA_CheckBoxControlTypeId: return "checkbox";
        case UIA_ComboBoxControlTypeId: return "combobox";
        case UIA_EditControlTypeId: return "edit";
        case UIA_HyperlinkControlTypeId: return "link";
        case UIA_ListItemControlTypeId: return "listitem";
        case UIA_MenuItemControlTypeId: return "menuitem";
        case UIA_RadioButtonControlTypeId: return "radio";
        case UIA_SliderControlTypeId: return "slider";
        case UIA_SpinnerControlTypeId: return "spinner";
        case UIA_TabItemControlTypeId: return "tab";
        case UIA_TextControlTypeId: return "text";
        case UIA_TreeItemControlTypeId: return "treeitem";
        default: return "element";
    }
}

/**
 * Recursively collect interactive elements with depth limiting for performance
 */
static void collect_elements(IUIAutomationElement* element, std::vector<struct ui_element>& elements, int max_depth = 8, int current_depth = 0, HWND window = NULL)
{
    if (!element || elements.size() >= MAX_UI_ELEMENTS || max_depth <= 0)
        return;
    
    // Track maximum depth actually reached
    if (current_depth > max_depth_reached) {
        max_depth_reached = current_depth;
    }

    // Check if current element is interactive
    if (is_interactive_element(element)) {
        RECT rect;
        if (get_element_rect(element, &rect)) {
            // Check if element is actually visible within window bounds
            if (window && !check_is_actually_visible(element, window)) {
                // Skip elements that are clipped or outside window
                goto process_children;
            }
            // Filter out very small elements
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            
            min_width = config_get_int("ui_min_width");
            min_height = config_get_int("ui_min_height");
            min_area = config_get_int("ui_min_area");
            
            if (width >= min_width && height >= min_height && 
                (width * height) >= min_area) {
                
                struct ui_element elem = {0};
                elem.x = rect.left;
                elem.y = rect.top;
                elem.w = width;
                elem.h = height;
                
                std::string name = get_element_name(element);
                std::string type = get_element_type(element);
                
                if (!name.empty()) {
                    elem.name = (char*)malloc(name.length() + 1);
                    strcpy(elem.name, name.c_str());
                } else {
                    elem.name = nullptr;
                }
                elem.role = (char*)malloc(type.length() + 1);
                strcpy(elem.role, type.c_str());
                
                elements.push_back(elem);
            }
        }
    }

process_children:
    // Use cached tree walker to process children
    if (!g_pTreeWalker)
        return;

    IUIAutomationElement* child = nullptr;
    HRESULT hr = g_pTreeWalker->GetFirstChildElement(element, &child);
    
    while (SUCCEEDED(hr) && child && elements.size() < MAX_UI_ELEMENTS) {
        collect_elements(child, elements, max_depth - 1, current_depth + 1, window);
        
        IUIAutomationElement* nextChild = nullptr;
        hr = g_pTreeWalker->GetNextSiblingElement(child, &nextChild);
        child->Release();
        child = nextChild;
    }
    
    if (child)
        child->Release();
}

// C interface functions
extern "C" {

/**
 * Check if UI Automation is available
 */
int uiautomation_is_available(void)
{
    return initialize_uiautomation() ? 1 : 0;
}

/**
 * Detect UI elements using Windows UI Automation
 */
struct ui_detection_result *uiautomation_detect_ui_elements(void)
{
    struct ui_detection_result *result = 
        (struct ui_detection_result *)calloc(1, sizeof(struct ui_detection_result));
    if (!result)
        return nullptr;

    if (!initialize_uiautomation()) {
        result->error = -1;
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Failed to initialize UI Automation");
        return result;
    }

    try {
        // Get the foreground window
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) {
            result->error = -2;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "No foreground window found");
            return result;
        }

        // Get UI Automation element for the window
        IUIAutomationElement* rootElement = nullptr;
        HRESULT hr = g_pAutomation->ElementFromHandle(hwnd, &rootElement);
        if (FAILED(hr) || !rootElement) {
            result->error = -3;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Failed to get UI Automation element for window");
            return result;
        }

        // Collect interactive elements with timing
        std::vector<struct ui_element> elements;
        DWORD startTime = GetTickCount();
        
        // Reset depth tracking
        max_depth_reached = 0;
        
        // Get max depth from config, default to 8
        int max_depth = 8;
        try {
            max_depth = config_get_int("ui_max_depth");
        } catch (...) {
            // Use default if config access fails
        }
        
        collect_elements(rootElement, elements, max_depth, 0, hwnd);
        DWORD endTime = GetTickCount();
        rootElement->Release();

        fprintf(stderr, "UI Automation: Collection took %lu ms (depth: %d/%d, elements: %zu, limit: %d)\n", 
                endTime - startTime, max_depth_reached, max_depth, elements.size(), MAX_UI_ELEMENTS);
        
        /* Suggest increasing depth if we hit the limit */
        if (max_depth_reached >= max_depth) {
            fprintf(stderr, "UI Automation: Hit max depth limit! Consider increasing ui_max_depth for more hints\n");
        }

        if (elements.empty()) {
            result->error = -4;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "No interactive UI elements detected");
            return result;
        }

        // Allocate result array
        result->elements = (struct ui_element *)calloc(elements.size(), sizeof(struct ui_element));
        if (!result->elements) {
            result->error = -5;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Memory allocation failed");
            return result;
        }

        // Copy elements to result
        for (size_t i = 0; i < elements.size(); i++) {
            result->elements[i] = elements[i];
        }

        result->count = elements.size();
        result->error = 0;

        fprintf(stderr, "UI Automation: Detected %zu interactive elements\n", result->count);

    } catch (...) {
        result->error = -6;
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Unknown error in UI Automation detection");
    }

    return result;
}

/**
 * Free UI Automation detection result
 */
void uiautomation_free_ui_elements(struct ui_detection_result *result)
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

/**
 * Cleanup UI Automation resources (called on exit)
 */
void uiautomation_cleanup(void)
{
    fprintf(stderr, "UI Automation: Cleaning up resources\n");
    cleanup_uiautomation();
}

} // extern "C"