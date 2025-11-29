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
#include <algorithm>

// Global UI Automation objects
static IUIAutomation* g_pAutomation = nullptr;
static IUIAutomationTreeWalker* g_pTreeWalker = nullptr;
static IUIAutomationCacheRequest* g_pCacheRequest = nullptr;
static bool g_initialized = false;
static int max_depth_reached = 0;

// Timing utility for debug logs with actual time
static void log_with_time(const char* format, ...) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(stderr, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

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

    // Create cache request for better performance
    // Cache the properties we need to avoid repeated cross-process calls
    hr = g_pAutomation->CreateCacheRequest(&g_pCacheRequest);
    if (SUCCEEDED(hr) && g_pCacheRequest) {
        g_pCacheRequest->AddProperty(UIA_ControlTypePropertyId);
        g_pCacheRequest->AddProperty(UIA_IsEnabledPropertyId);
        g_pCacheRequest->AddProperty(UIA_BoundingRectanglePropertyId);
        g_pCacheRequest->AddProperty(UIA_NamePropertyId);
        g_pCacheRequest->put_TreeScope((TreeScope)(TreeScope_Element | TreeScope_Children));
        
        IUIAutomationCondition* pCondition = nullptr;
        hr = g_pAutomation->get_ControlViewCondition(&pCondition);
        if (SUCCEEDED(hr) && pCondition) {
            g_pCacheRequest->put_TreeFilter(pCondition);
            pCondition->Release();
        }
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
        if (g_pCacheRequest) {
            g_pCacheRequest->Release();
            g_pCacheRequest = nullptr;
        }
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

    // Explicitly exclude container types that should never be interactive
    switch (controlType) {
        case UIA_PaneControlTypeId:
        case UIA_GroupControlTypeId:
        case UIA_WindowControlTypeId:
        case UIA_DocumentControlTypeId:
        case UIA_ToolBarControlTypeId:
        case UIA_StatusBarControlTypeId:
        case UIA_TitleBarControlTypeId:
        case UIA_MenuBarControlTypeId:
        case UIA_ScrollBarControlTypeId:
        case UIA_SeparatorControlTypeId:
        case UIA_ListControlTypeId:
        case UIA_TableControlTypeId:
        case UIA_TreeControlTypeId:
        case UIA_TabControlTypeId:
        case UIA_HeaderControlTypeId:
        case UIA_HeaderItemControlTypeId:
            // These are containers, not interactive elements
            return false;
    }

    // Check if element is enabled FIRST (required for all interactive elements)
    BOOL isEnabled = FALSE;
    hr = element->get_CurrentIsEnabled(&isEnabled);
    if (FAILED(hr) || !isEnabled)
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
        case UIA_TreeItemControlTypeId:
        case UIA_DataItemControlTypeId:
        case UIA_SplitButtonControlTypeId:
            return true;
        case UIA_TextControlTypeId:
            // Text might be interactive (links), check patterns
            break;
        case UIA_ImageControlTypeId:
            // Images might be clickable, check patterns
            break;
        default:
            // Unknown types, skip them
            return false;
    }

    // For text and images, check if they have interaction patterns
    // Check for invoke pattern first (most common)
    IUIAutomationInvokePattern* invokePattern = nullptr;
    hr = element->GetCurrentPattern(UIA_InvokePatternId, (IUnknown**)&invokePattern);
    if (SUCCEEDED(hr) && invokePattern) {
        invokePattern->Release();
        return true;
    }

    // Check for selection pattern (for selectable text/images)
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
    // First check the IsOffscreen property (fast check)
    BOOL isOffscreen = TRUE;
    HRESULT hr = element->get_CurrentIsOffscreen(&isOffscreen);
    if (SUCCEEDED(hr) && isOffscreen) {
        return false;  // Element is explicitly marked as offscreen
    }

    RECT elemRect, winRect;

    // Get element rectangle
    if (!get_element_rect(element, &elemRect)) {
        return false;
    }

    // Check if element has a valid size (not zero or negative)
    LONG width = elemRect.right - elemRect.left;
    LONG height = elemRect.bottom - elemRect.top;
    if (width <= 0 || height <= 0) {
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
    LONG overlapLeft = std::max(elemRect.left, winRect.left);
    LONG overlapTop = std::max(elemRect.top, winRect.top);
    LONG overlapRight = std::min(elemRect.right, winRect.right);
    LONG overlapBottom = std::min(elemRect.bottom, winRect.bottom);

    LONG overlapWidth = overlapRight - overlapLeft;
    LONG overlapHeight = overlapBottom - overlapTop;

    if (overlapWidth <= 0 || overlapHeight <= 0) {
        return false;
    }

    // Calculate visible area
    LONG visibleArea = overlapWidth * overlapHeight;
    LONG totalArea = width * height;

    // Require at least 50% of element to be visible, or minimum 100 pixels
    return (visibleArea >= (totalArea / 2) || (visibleArea >= 100);
}

/**
 * Get element name 
 */
static std::string get_element_name(IUIAutomationElement* element)
{
    if (!element)
        return "";

    BSTR name = nullptr;
    BSTR helpText = nullptr;
    BSTR automationId = nullptr;
    BSTR ariaRole = nullptr;
    HRESULT hr;
    std::string result;

    hr = element->get_CurrentName(&name);
    std::string nameStr = (SUCCEEDED(hr) && name && SysStringLen(name) > 0) ? bstr_to_utf8(name) : "";
    
    hr = element->get_CurrentHelpText(&helpText);
    std::string helpStr = (SUCCEEDED(hr) && helpText && SysStringLen(helpText) > 0) ? bstr_to_utf8(helpText) : "";
    
    hr = element->get_CurrentAutomationId(&automationId);
    std::string idStr = (SUCCEEDED(hr) && automationId && SysStringLen(automationId) > 0) ? bstr_to_utf8(automationId) : "";
    
    hr = element->get_CurrentAriaRole(&ariaRole);
    std::string ariaStr = (SUCCEEDED(hr) && ariaRole && SysStringLen(ariaRole) > 0) ? bstr_to_utf8(ariaRole) : "";

    if (!nameStr.empty()) {
        result = nameStr;
    } else if (!helpStr.empty()) {
        result = helpStr;
    } else if (!idStr.empty()) {
        result = idStr;
    } else if (!ariaStr.empty()) {
        result = ariaStr;
    }

    if (name) SysFreeString(name);
    if (helpText) SysFreeString(helpText);
    if (automationId) SysFreeString(automationId);
    if (ariaRole) SysFreeString(ariaRole);

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
        case UIA_DataItemControlTypeId: return "dataitem";
        case UIA_SplitButtonControlTypeId: return "splitbutton";
        case UIA_ImageControlTypeId: return "image";
        default: return "element";
    }
}

// Global counters for performance tracking
static int g_nodes_visited = 0;
static DWORD g_last_progress_time = 0;
static DWORD g_traversal_start_time = 0;
static int g_max_traversal_time_ms = 5000;  // 5 second timeout
static bool g_timeout_triggered = false;  // Flag to stop all recursion

/**
 * Dump UI tree to file for debugging
 */
static void dump_element_tree(FILE* fp, IUIAutomationElement* element, int depth, int max_depth, HWND window)
{
    if (!element || depth > max_depth)
        return;

    std::string indent(depth * 2, ' ');

    std::string name = get_element_name(element);
    std::string type = get_element_type(element);

    BSTR desc = nullptr;
    std::string descStr;
    if (SUCCEEDED(element->get_CurrentHelpText(&desc)) && desc) {
        descStr = bstr_to_utf8(desc);
        SysFreeString(desc);
    }

    BSTR automationId = nullptr;
    std::string idStr;
    if (SUCCEEDED(element->get_CurrentAutomationId(&automationId)) && automationId) {
        idStr = bstr_to_utf8(automationId);
        SysFreeString(automationId);
    }

    RECT rect = {0};
    element->get_CurrentBoundingRectangle(&rect);
    int x = rect.left;
    int y = rect.top;
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;

    BOOL enabled = FALSE;
    element->get_CurrentIsEnabled(&enabled);

    BOOL isOffscreen = FALSE;
    element->get_CurrentIsOffscreen(&isOffscreen);

    // Check if element would be considered interactive
    bool isInteractive = is_interactive_element(element);

    // Check if element is visible within window
    bool isVisibleInWindow = false;
    if (window) {
        isVisibleInWindow = check_is_actually_visible(element, window);
    }

    // Determine validation status
    std::string status = "";
    if (!enabled) {
        status += " [DISABLED]";
    }
    if (isOffscreen) {
        status += " [OFFSCREEN]";
    }
    if (w <= 0 || h <= 0) {
        status += " [INVALID_SIZE]";
    }
    if (window && !isVisibleInWindow && w > 0 && h > 0) {
        status += " [NOT_VISIBLE]";
    }
    if (isInteractive && enabled && !isOffscreen && w > 0 && h > 0) {
        if (!window || isVisibleInWindow) {
            status += " ✓ [VALID_HINT]";
        }
    }

    fprintf(fp, "%s[%s] name='%s' x=%d y=%d w=%d h=%d enabled=%d offscreen=%d interactive=%d%s",
        indent.c_str(), type.c_str(), name.c_str(), x, y, w, h,
        enabled ? 1 : 0, isOffscreen ? 1 : 0, isInteractive ? 1 : 0, status.c_str());

    if (!descStr.empty())
        fprintf(fp, " desc='%s'", descStr.c_str());
    if (!idStr.empty())
        fprintf(fp, " id='%s'", idStr.c_str());

    fprintf(fp, "\n");

    IUIAutomationElement* child = nullptr;
    if (SUCCEEDED(g_pTreeWalker->GetFirstChildElement(element, &child)) && child) {
        dump_element_tree(fp, child, depth + 1, max_depth, window);
        child->Release();
    }

    IUIAutomationElement* sibling = nullptr;
    if (SUCCEEDED(g_pTreeWalker->GetNextSiblingElement(element, &sibling)) && sibling) {
        dump_element_tree(fp, sibling, depth, max_depth, window);
        sibling->Release();
    }
}

/**
 * Collect interactive elements using breadth-first search for better early stopping
 * 
 * PERFORMANCE NOTE: UI Automation tree traversal is very slow on complex pages (browsers).
 * Breadth-first search finds visible elements faster than depth-first.
 */
static void collect_elements_bfs(IUIAutomationElement* rootElement, std::vector<struct ui_element>& elements,
                                 int max_depth, HWND window, int min_width, int min_height, int min_area)
{
    if (!rootElement || !g_pTreeWalker)
        return;
    
    // Use a queue for breadth-first traversal
    struct QueueItem {
        IUIAutomationElement* element;
        int depth;
    };
    std::vector<QueueItem> queue;
    queue.push_back({rootElement, 0});
    rootElement->AddRef();
    
    int target_elements = 200;  // Stop after finding enough elements
    
    while (!queue.empty() && !g_timeout_triggered) {
        QueueItem item = queue.front();
        queue.erase(queue.begin());
        
        IUIAutomationElement* element = item.element;
        int depth = item.depth;
        
        g_nodes_visited++;
        
        // Check timeout every 50 nodes
        if (g_nodes_visited % 50 == 0) {
            DWORD now = GetTickCount();
            DWORD elapsed = now - g_traversal_start_time;
            
            if (now - g_last_progress_time > 2000) {
                log_with_time("UI Automation: Progress - visited %d nodes, found %zu elements, depth %d/%d, elapsed %lu ms\n", 
                             g_nodes_visited, elements.size(), depth, max_depth, elapsed);
                g_last_progress_time = now;
            }
            
            if (elapsed > (DWORD)g_max_traversal_time_ms && !g_timeout_triggered) {
                g_timeout_triggered = true;
                log_with_time("UI Automation: TIMEOUT! Stopping traversal after %lu ms (%d nodes, %zu elements)\n", 
                             elapsed, g_nodes_visited, elements.size());
                element->Release();
                break;
            }
        }
        
        // Early stop if we have enough elements
        if (elements.size() >= (size_t)target_elements) {
            log_with_time("UI Automation: Found %zu elements (target: %d), stopping early\n",
                         elements.size(), target_elements);
            element->Release();
            break;
        }

        // Check basic element validity before processing it or its children
        bool shouldProcessChildren = false;

        // First check if element is enabled and visible (quick filters)
        BOOL isEnabled = FALSE;
        HRESULT hr = element->get_CurrentIsEnabled(&isEnabled);
        if (SUCCEEDED(hr) && isEnabled) {
            // Check if element is offscreen
            BOOL isOffscreen = TRUE;
            hr = element->get_CurrentIsOffscreen(&isOffscreen);
            bool isVisible = (SUCCEEDED(hr) && !isOffscreen);

            // Check size constraints
            RECT rect;
            if (get_element_rect(element, &rect)) {
                int width = rect.right - rect.left;
                int height = rect.bottom - rect.top;

                // Element has valid size and basic visibility - worth processing children
                if (width > 0 && height > 0) {
                    shouldProcessChildren = true;

                    // Now check if it's actually an interactive element worth adding
                    if (is_interactive_element(element)) {
                        if (width >= min_width && height >= min_height && (width * height) >= min_area) {
                            if (!window || check_is_actually_visible(element, window)) {
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

                                if (elements.size() >= MAX_UI_ELEMENTS) {
                                    element->Release();
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Add children to queue ONLY if element passed basic validity checks
        if (shouldProcessChildren && depth < max_depth) {
            IUIAutomationElement* child = nullptr;
            hr = g_pTreeWalker->GetFirstChildElement(element, &child);

            while (SUCCEEDED(hr) && child) {
                queue.push_back({child, depth + 1});

                if (depth + 1 > max_depth_reached) {
                    max_depth_reached = depth + 1;
                }

                IUIAutomationElement* nextChild = nullptr;
                hr = g_pTreeWalker->GetNextSiblingElement(child, &nextChild);
                child = nextChild;
            }
        }
        
        element->Release();
    }
    
    // Clean up remaining queue items
    for (auto& item : queue) {
        item.element->Release();
    }
}

/**
 * Old recursive depth-first collection (kept for reference)
 */
static void collect_elements(IUIAutomationElement* element, std::vector<struct ui_element>& elements, 
                            int max_depth, int current_depth, HWND window,
                            int min_width, int min_height, int min_area)
{
    if (!element || elements.size() >= MAX_UI_ELEMENTS || max_depth <= 0 || g_timeout_triggered)
        return;
    
    // Track nodes visited and check timeout
    g_nodes_visited++;
    
    // Check timeout every 100 nodes to avoid overhead
    if (g_nodes_visited % 100 == 0) {
        DWORD now = GetTickCount();
        DWORD elapsed = now - g_traversal_start_time;
        
        // Show progress every 2 seconds
        if (now - g_last_progress_time > 2000) {
            log_with_time("UI Automation: Progress - visited %d nodes, found %zu elements, depth %d/%d, elapsed %lu ms\n", 
                         g_nodes_visited, elements.size(), current_depth, max_depth, elapsed);
            g_last_progress_time = now;
        }
        
        // Timeout check - stop if taking too long
        if (elapsed > (DWORD)g_max_traversal_time_ms && !g_timeout_triggered) {
            g_timeout_triggered = true;
            log_with_time("UI Automation: TIMEOUT! Stopping all traversal after %lu ms (%d nodes, %zu elements)\n", 
                         elapsed, g_nodes_visited, elements.size());
            return;
        }
    }
    
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
        collect_elements(child, elements, max_depth - 1, current_depth + 1, window, min_width, min_height, min_area);
        
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
        log_with_time("========== UI Automation Detection Started ==========\n");
        DWORD t0 = GetTickCount();
        
        // Get the foreground window
        log_with_time("UI Automation: Getting foreground window...\n");
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) {
            result->error = -2;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "No foreground window found");
            return result;
        }
        DWORD t1 = GetTickCount();
        log_with_time("UI Automation: Got window in %lu ms\n", t1 - t0);

        // Get UI Automation element for the window
        log_with_time("UI Automation: Getting root element...\n");
        IUIAutomationElement* rootElement = nullptr;
        HRESULT hr = g_pAutomation->ElementFromHandle(hwnd, &rootElement);
        if (FAILED(hr) || !rootElement) {
            result->error = -3;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Failed to get UI Automation element for window");
            return result;
        }
        DWORD t2 = GetTickCount();
        log_with_time("UI Automation: Got root element in %lu ms\n", t2 - t1);

        // Read config values ONCE before traversal (not in the loop!)
        log_with_time("UI Automation: Reading config values...\n");
        int max_depth = 10;
        int min_width = 10;
        int min_height = 10;
        int min_area = 100;
        int timeout_ms = 5000;
        try {
            max_depth = config_get_int("ui_max_depth");
            min_width = config_get_int("ui_min_width");
            min_height = config_get_int("ui_min_height");
            min_area = config_get_int("ui_min_area");
            timeout_ms = config_get_int("ui_detection_timeout");
        } catch (...) {
            // Use defaults if config access fails
        }
        g_max_traversal_time_ms = timeout_ms;
        DWORD t3 = GetTickCount();
        log_with_time("UI Automation: Config loaded (max_depth=%d, min_size=%dx%d, min_area=%d) in %lu ms\n", 
                     max_depth, min_width, min_height, min_area, t3 - t2);
        
        // Collect interactive elements with timing
        log_with_time("UI Automation: Starting tree traversal (max_depth=%d, timeout=%dms)...\n", 
                     max_depth, g_max_traversal_time_ms);
        std::vector<struct ui_element> elements;
        max_depth_reached = 0;
        g_nodes_visited = 0;
        g_timeout_triggered = false;  // Reset timeout flag
        DWORD startTime = GetTickCount();
        g_traversal_start_time = startTime;
        g_last_progress_time = startTime;
        
        // // Dump UI tree to file for debugging
        // FILE* dump_fp = fopen("ui_tree_dump_windows.txt", "w");
        // if (dump_fp) {
        //     fprintf(dump_fp, "Windows UI Automation Tree Dump\n");
        //     fprintf(dump_fp, "================================\n");
        //     fprintf(dump_fp, "Legend:\n");
        //     fprintf(dump_fp, "  ✓ [VALID_HINT]   - Element will receive a hint (interactive, enabled, visible)\n");
        //     fprintf(dump_fp, "  [DISABLED]       - Element is disabled (not interactive)\n");
        //     fprintf(dump_fp, "  [OFFSCREEN]      - Element is marked as offscreen by UI Automation\n");
        //     fprintf(dump_fp, "  [INVALID_SIZE]   - Element has zero or negative dimensions\n");
        //     fprintf(dump_fp, "  [NOT_VISIBLE]    - Element is clipped/outside window bounds\n");
        //     fprintf(dump_fp, "\n");
        //     dump_element_tree(dump_fp, rootElement, 0, max_depth, hwnd);
        //     fclose(dump_fp);
        //     log_with_time("UI Automation: Tree dumped to ui_tree_dump_windows.txt\n");
        // }
        
        // Use breadth-first search for better performance (finds visible elements faster)
        collect_elements_bfs(rootElement, elements, max_depth, hwnd, min_width, min_height, min_area);
        
        DWORD endTime = GetTickCount();
        rootElement->Release();

        log_with_time("UI Automation: Collection took %lu ms (visited %d nodes, depth: %d/%d, elements: %zu, limit: %d)%s\n", 
                endTime - startTime, g_nodes_visited, max_depth_reached, max_depth, elements.size(), MAX_UI_ELEMENTS,
                g_timeout_triggered ? " [STOPPED BY TIMEOUT]" : "");
        
        /* Suggest increasing depth if we hit the limit */
        if (max_depth_reached >= max_depth) {
            log_with_time("UI Automation: Hit max depth limit! Consider increasing ui_max_depth for more hints\n");
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

        DWORD totalTime = GetTickCount() - t0;
        log_with_time("UI Automation: Detected %zu interactive elements (total time: %lu ms)\n", result->count, totalTime);
        log_with_time("========== UI Automation Detection Completed ==========\n\n");

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
    log_with_time("UI Automation: Cleaning up resources\n");
    cleanup_uiautomation();
}

} // extern "C"