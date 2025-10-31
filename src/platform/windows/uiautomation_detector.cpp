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
static bool g_initialized = false;

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

    g_initialized = true;
    return true;
}

/**
 * Cleanup UI Automation
 */
static void cleanup_uiautomation()
{
    if (g_initialized) {
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
 * Check if element is interactive
 */
static bool is_interactive_element(IUIAutomationElement* element)
{
    if (!element)
        return false;

    CONTROLTYPEID controlType;
    HRESULT hr = element->get_CurrentControlType(&controlType);
    if (FAILED(hr))
        return false;

    // Check for interactive control types
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
        case UIA_TextControlTypeId:
        case UIA_TreeItemControlTypeId:
            return true;
        default:
            break;
    }

    // Check if element is enabled and has patterns that indicate interactivity
    BOOL isEnabled = FALSE;
    hr = element->get_CurrentIsEnabled(&isEnabled);
    if (FAILED(hr) || !isEnabled)
        return false;

    // Check for invoke pattern (clickable)
    IUIAutomationInvokePattern* invokePattern = nullptr;
    hr = element->GetCurrentPattern(UIA_InvokePatternId, (IUnknown**)&invokePattern);
    if (SUCCEEDED(hr) && invokePattern) {
        invokePattern->Release();
        return true;
    }

    // Check for selection item pattern (selectable)
    IUIAutomationSelectionItemPattern* selectionPattern = nullptr;
    hr = element->GetCurrentPattern(UIA_SelectionItemPatternId, (IUnknown**)&selectionPattern);
    if (SUCCEEDED(hr) && selectionPattern) {
        selectionPattern->Release();
        return true;
    }

    // Check for toggle pattern (toggleable)
    IUIAutomationTogglePattern* togglePattern = nullptr;
    hr = element->GetCurrentPattern(UIA_TogglePatternId, (IUnknown**)&togglePattern);
    if (SUCCEEDED(hr) && togglePattern) {
        togglePattern->Release();
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
 * Recursively collect interactive elements
 */
static void collect_elements(IUIAutomationElement* element, std::vector<struct ui_element>& elements)
{
    if (!element || elements.size() >= MAX_UI_ELEMENTS)
        return;

    // Check if current element is interactive
    if (is_interactive_element(element)) {
        RECT rect;
        if (get_element_rect(element, &rect)) {
            // Filter out very small elements
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            
            // Use default values if config functions fail
            int min_width = 10;
            int min_height = 10;
            int min_area = 100;
            
            try {
                min_width = config_get_int("uiautomation_min_width");
                min_height = config_get_int("uiautomation_min_height");
                min_area = config_get_int("uiautomation_min_area");
            } catch (...) {
                // Use defaults if config access fails
            }
            
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

    // Use tree walker to process children
    if (!g_pAutomation)
        return;

    IUIAutomationTreeWalker* walker = nullptr;
    HRESULT hr = g_pAutomation->get_ControlViewWalker(&walker);
    if (FAILED(hr) || !walker)
        return;

    IUIAutomationElement* child = nullptr;
    hr = walker->GetFirstChildElement(element, &child);
    
    while (SUCCEEDED(hr) && child && elements.size() < MAX_UI_ELEMENTS) {
        collect_elements(child, elements);
        
        IUIAutomationElement* nextChild = nullptr;
        hr = walker->GetNextSiblingElement(child, &nextChild);
        child->Release();
        child = nextChild;
    }
    
    if (child)
        child->Release();
    walker->Release();
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

        // Collect interactive elements
        std::vector<struct ui_element> elements;
        collect_elements(rootElement, elements);
        rootElement->Release();

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

} // extern "C"