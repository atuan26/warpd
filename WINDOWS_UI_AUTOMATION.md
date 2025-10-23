# Windows UI Automation Detector Implementation

This document describes the implementation of Windows UI Automation detector for warpd's smart hint mode.

## Files Created/Modified

### New Files:
1. **src/platform/windows/uiautomation_detector.cpp** - C++ wrapper for Windows UI Automation API
2. **src/platform/windows/ui_detector.c** - C interface wrapper for the UI Automation detector

### Modified Files:
1. **src/platform/windows/windows.c** - Added UI detection function registration
2. **mk/windows.mk** - Updated to support C++ compilation and UI Automation libraries
3. **src/config.c** - Added UI Automation configuration options

## Implementation Details

### UI Automation Detector (uiautomation_detector.cpp)
- Uses Windows UI Automation API to detect interactive elements
- Supports various control types: buttons, checkboxes, edit fields, links, etc.
- Filters elements based on size and interactivity patterns
- Provides C-compatible interface using extern "C"

### Key Features:
- **Element Detection**: Identifies interactive UI elements using UI Automation patterns
- **Filtering**: Configurable minimum width, height, and area filters
- **Recursive Traversal**: Walks the UI element tree to find all interactive elements
- **Memory Management**: Proper allocation and cleanup of UI element data

### Configuration Options:
- `uiautomation_min_width`: Minimum element width (default: 10px)
- `uiautomation_min_height`: Minimum element height (default: 10px)  
- `uiautomation_min_area`: Minimum element area (default: 100px²)

### Build Requirements:
- Windows SDK with UI Automation headers
- MinGW-w64 cross-compiler or Visual Studio
- Additional libraries: ole32, oleaut32, uuid

## Usage

The Windows UI Automation detector is automatically used when warpd's smart hint mode is activated on Windows. It will detect interactive elements in the foreground window and generate hints for them.

## Error Handling

The detector includes comprehensive error handling for:
- UI Automation initialization failures
- Missing foreground windows
- Element access errors
- Memory allocation failures

All errors are reported through the standard ui_detection_result error mechanism.