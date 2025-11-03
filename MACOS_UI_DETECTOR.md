# macOS UI Element Detector Implementation

This document describes the macOS UI element detector implementation for warpd's smart hint mode.

## Overview

The macOS implementation uses a two-tier approach:

1. **Primary**: macOS Accessibility API - Native system API for detecting UI elements
2. **Fallback**: OpenCV computer vision - Visual detection when Accessibility API fails

## Architecture

### Files Created/Modified

- `src/platform/macos/ui_detector.c` - Main detector interface
- `src/platform/macos/accessibility_detector.m` - Accessibility API implementation
- `src/platform/macos/macos.h` - Added function declarations
- `src/platform/macos/macos.m` - Added platform function pointers
- `mk/macos.mk` - Updated build configuration

### Key Features

#### Accessibility API Detector
- Uses native macOS AX (Accessibility) API
- Detects interactive elements: buttons, text fields, checkboxes, etc.
- Provides element names and roles
- Requires accessibility permissions
- Fast and accurate for supported applications

#### OpenCV Fallback
- Computer vision-based detection
- Works with any visual content
- No permissions required
- Slower but universal compatibility

## Implementation Details

### Accessibility API Integration

The implementation uses Core Foundation and Cocoa frameworks:

```objective-c
// Get frontmost application
NSRunningApplication *frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
AXUIElementRef appRef = AXUIElementCreateApplication([frontApp processIdentifier]);

// Get focused window and traverse UI hierarchy
CFTypeRef focusedWindow = NULL;
AXUIElementCopyAttributeValue(appRef, kAXFocusedWindowAttribute, &focusedWindow);
```

### Supported Element Types

- Buttons (kAXButtonRole)
- Text fields (kAXTextFieldRole)
- Checkboxes (kAXCheckBoxRole)
- Radio buttons (kAXRadioButtonRole)
- Pop-up buttons (kAXPopUpButtonRole)
- Links (kAXLinkRole)
- Tables and lists (kAXTableRole, kAXListRole)
- Tabs (kAXTabRole)
- Sliders (kAXSliderRole)

### Error Handling

The implementation provides detailed error messages:

- Permission denied (accessibility not enabled)
- No frontmost application
- No focused/main window
- No interactive elements found
- Memory allocation failures

## Building

### Prerequisites

- macOS 10.9+ (for Accessibility API)
- Xcode command line tools
- OpenCV (for fallback detection)

### Compilation

```bash
make PLATFORM=macos all
```

### Required Frameworks

- Cocoa.framework
- ApplicationServices.framework
- CoreGraphics.framework (for OpenCV)
- ImageIO.framework (for OpenCV)

## Usage

### Permissions

The Accessibility API requires explicit user permission:

1. System Preferences → Security & Privacy → Privacy → Accessibility
2. Add and enable your application (warpd)

Without permissions, the detector automatically falls back to OpenCV.

### Testing

A test program is provided:

```bash
clang -o test_macos_detector test_macos_detector.c \
  src/platform/macos/ui_detector.c \
  src/platform/macos/accessibility_detector.m \
  src/platform/macos/opencv_detector.cpp \
  src/common/opencv_detector.cpp \
  -framework Cocoa -framework ApplicationServices \
  -framework CoreGraphics -framework ImageIO \
  -lopencv_core -lopencv_imgproc -lstdc++

./test_macos_detector
```

## Performance

### Accessibility API
- **Speed**: Very fast (~1-5ms)
- **Accuracy**: High for supported apps
- **Coverage**: Depends on app accessibility implementation

### OpenCV Fallback
- **Speed**: Moderate (~50-200ms)
- **Accuracy**: Good for visual elements
- **Coverage**: Universal (any visual content)

## Limitations

1. **Accessibility API**:
   - Requires user permission
   - Some apps have poor accessibility implementation
   - May not detect custom-drawn elements

2. **OpenCV Fallback**:
   - Slower than native API
   - May detect false positives
   - Requires visual contrast

## Integration with warpd

The detector integrates seamlessly with warpd's smart hint system:

```c
struct platform platform = {
    // ... other functions ...
    .detect_ui_elements = macos_detect_ui_elements,
    .free_ui_elements = macos_free_ui_elements,
};
```

## Future Enhancements

Possible improvements:

1. **Caching**: Cache accessibility tree for better performance
2. **Filtering**: Better heuristics for element relevance
3. **Hybrid Mode**: Combine both methods for optimal results
4. **Configuration**: User-configurable detection preferences

## Troubleshooting

### Common Issues

1. **"Accessibility permissions not granted"**
   - Enable accessibility permissions in System Preferences
   - Restart warpd after enabling permissions

2. **"No interactive elements found"**
   - Try a different application
   - Check if the app has proper accessibility implementation
   - OpenCV fallback should activate automatically

3. **Build errors**
   - Ensure Xcode command line tools are installed
   - Verify OpenCV is properly installed
   - Check framework linking

### Debug Output

Enable debug output to see detector selection:

```
macOS: Accessibility API found 15 elements
macOS: Falling back to OpenCV detection
macOS: OpenCV found 8 elements
```

This implementation provides robust UI element detection for macOS with graceful fallback, ensuring warpd's smart hint mode works reliably across different applications and system configurations.