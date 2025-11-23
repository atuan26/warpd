/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * © 2019 Raheman Vaiya (see: LICENSE).
 */

#include "macos.h"

// Forward declarations for UI detection functions
#ifdef __cplusplus
extern "C" {
#endif
	struct ui_detection_result *opencv_detect_ui_elements(void);
	void opencv_free_ui_elements(struct ui_detection_result *result);
	struct ui_detection_result *macos_detect_ui_elements(void);
	void macos_free_ui_elements(struct ui_detection_result *result);
#ifdef __cplusplus
}
#endif


static void osx_send_paste()
{
	int shifted;
	
	/* Send Cmd+V (same pattern as osx_copy_selection) */
	send_key(osx_input_lookup_code("leftmeta", &shifted), 1);
	send_key(osx_input_lookup_code("v", &shifted), 1);
	send_key(osx_input_lookup_code("leftmeta", &shifted), 0);
	send_key(osx_input_lookup_code("v", &shifted), 0);
}

/* Simple text input window - minimal UI with just a text field */
@interface SimpleTextInputWindow : NSWindow <NSTextFieldDelegate>
@property (nonatomic, strong) NSTextField *textField;
@property (nonatomic, assign) BOOL submitted;
@end

@implementation SimpleTextInputWindow

- (instancetype)init
{
	NSRect frame = NSMakeRect(0, 0, 400, 40);
	self = [super initWithContentRect:frame
			        styleMask:NSWindowStyleMaskBorderless
			          backing:NSBackingStoreBuffered
			            defer:NO];
	if (self) {
		[self setLevel:NSFloatingWindowLevel];
		[self setOpaque:NO];
		[self setBackgroundColor:[NSColor colorWithWhite:0.95 alpha:0.95]];
		[self setHasShadow:YES];
		[self setAcceptsMouseMovedEvents:YES];
		
		// Center on screen
		[self center];
		
		// Create text field
		NSRect textFrame = NSMakeRect(5, 5, 390, 30);
		_textField = [[NSTextField alloc] initWithFrame:textFrame];
		[_textField setDelegate:self];
		[_textField setEditable:YES];
		[_textField setSelectable:YES];
		[_textField setBezeled:NO];
		[_textField setDrawsBackground:NO];
		[_textField setFont:[NSFont systemFontOfSize:16]];
		[_textField setFocusRingType:NSFocusRingTypeNone];
		
		[[self contentView] addSubview:_textField];
		_submitted = NO;
	}
	return self;
}

- (void)controlTextDidEndEditing:(NSNotification *)notification
{
	NSInteger reason = [[[notification userInfo] objectForKey:@"NSTextMovement"] integerValue];
	if (reason == NSReturnTextMovement) {
		_submitted = YES;
		[NSApp stopModal];
	} else if (reason == NSCancelTextMovement) {
		_submitted = NO;
		[NSApp stopModal];
	}
}

- (void)keyDown:(NSEvent *)event
{
	if ([event keyCode] == 53) { // Escape key
		_submitted = NO;
		[NSApp stopModal];
	} else {
		// Pass event to text field
		[_textField keyDown:event];
	}
}

- (BOOL)canBecomeKeyWindow
{
	return YES;
}

- (BOOL)canBecomeMainWindow
{
	return YES;
}

@end

/* Insert text mode - temporarily disabled due to modal event loop issues */
static int osx_insert_text_mode(screen_t scr)
{
	/* TODO: Fix modal window event handling
	 * Current issue: Modal window blocks input and cannot be dismissed
	 * Possible solutions:
	 * 1. Use non-modal window with custom event loop
	 * 2. Use NSAlert with text input accessory view
	 * 3. Use separate process for input dialog
	 */
	(void)scr;
	return 0;
}

static NSDictionary *get_font_attrs(const char *family, NSColor *color, int h)
{
	NSDictionary *attrs;

	int ptsz = h;
	CGSize size;
	do {
		NSFont *font =
		    [NSFont fontWithName:[NSString stringWithUTF8String:family]
				    size:ptsz];
		if (!font) {
			fprintf(stderr, "ERROR: %s is not a valid font\n",
				family);
			exit(-1);
		}
		attrs = @{
			NSFontAttributeName : font,
			NSForegroundColorAttributeName : color,
		};
		size = [@"m" sizeWithAttributes:attrs];
		ptsz--;
	} while (size.height > h);

	return attrs;
}

void macos_draw_text(struct screen *scr, NSColor *col, const char *font,
		     int x, int y, int w,
		     int h, const char *s)
{

	NSDictionary *attrs = get_font_attrs(font, col, h);
	NSString *str = [NSString stringWithUTF8String:s];
	CGSize size = [str sizeWithAttributes:attrs];

	x += (w - size.width)/2;

	y += size.height + (h - size.height)/2;

	/* Convert to LLO */
	y = scr->h - y;

	[str drawAtPoint:NSMakePoint((float)x, (float)y) withAttributes: attrs];
}

void macos_draw_box(struct screen *scr, NSColor *col, float x, float y, float w, float h, float r)
{
	[col setFill];

	/* Convert to LLO */
	y = scr->h - y - h;

	NSBezierPath *path = [NSBezierPath
	    bezierPathWithRoundedRect:NSMakeRect((float)x, (float)y, (float)w,
						 (float)h)
			      xRadius:(float)r
			      yRadius:(float)r];
	[path fill];
}


NSColor *nscolor_from_hex(const char *str)
{
	ssize_t len;
	uint8_t r, g, b, a;
#define X2B(c) ((c >= '0' && c <= '9') ? (c & 0xF) : (((c | 0x20) - 'a') + 10))

	if (str == NULL)
		return 0;

	str = (*str == '#') ? str + 1 : str;
	len = strlen(str);

	if (len != 6 && len != 8) {
		fprintf(stderr, "Failed to parse %s, paint it black!\n", str);
		return NSColor.blackColor;
	}

	r = X2B(str[0]);
	r <<= 4;
	r |= X2B(str[1]);

	g = X2B(str[2]);
	g <<= 4;
	g |= X2B(str[3]);

	b = X2B(str[4]);
	b <<= 4;
	b |= X2B(str[5]);

	a = 255;
	if (len == 8) {
		a = X2B(str[6]);
		a <<= 4;
		a |= X2B(str[7]);
	}

	return [NSColor colorWithCalibratedRed:(float)r / 255
					 green:(float)g / 255
					  blue:(float)b / 255
					 alpha:(float)a / 255];
}

void osx_copy_selection()
{
	int shifted;

	send_key(osx_input_lookup_code("leftmeta", &shifted), 1);
	send_key(osx_input_lookup_code("c", &shifted), 1);
	send_key(osx_input_lookup_code("leftmeta", &shifted), 0);
	send_key(osx_input_lookup_code("c", &shifted), 0);
}

void osx_scroll(int direction)
{
	int y = 0;
	int x = 0;

	switch (direction) {
	case SCROLL_UP:
		y = 1;
		break;
	case SCROLL_DOWN:
		y = -1;
		break;
	case SCROLL_RIGHT:
		x = -1;
		break;
	case SCROLL_LEFT:
		x = 1;
		break;
	}

	CGEventRef ev = CGEventCreateScrollWheelEvent(
	    NULL, kCGScrollEventUnitPixel, 2, y, x);
	CGEventPost(kCGHIDEventTap, ev);
}

void osx_commit()
{
	dispatch_sync(dispatch_get_main_queue(), ^{
		size_t i;
		for (i = 0; i < nr_screens; i++) {
			struct window *win = screens[i].overlay;

			if (win->nr_hooks)
				window_show(win);
			else
				window_hide(win);
		}
	});
}

static void *mainloop(void *arg)
{
	int (*main)(struct platform *platform) = (int (*)(struct platform *platform)) arg;
	struct platform platform = {
		.commit = osx_commit,
		.copy_selection = osx_copy_selection,
		.hint_draw = osx_hint_draw,
		.init_hint = osx_init_hint,
		.input_grab_keyboard = osx_input_grab_keyboard,
		.input_lookup_code = osx_input_lookup_code,
		.input_lookup_name = osx_input_lookup_name,
		.input_next_event = osx_input_next_event,
		.input_ungrab_keyboard = osx_input_ungrab_keyboard,
		.input_wait = osx_input_wait,
		.mouse_click = osx_mouse_click,
		.mouse_down = osx_mouse_down,
		.mouse_get_position = osx_mouse_get_position,
		.mouse_hide = osx_mouse_hide,
		.mouse_move = osx_mouse_move,
		.mouse_show = osx_mouse_show,
		.mouse_up = osx_mouse_up,
		.screen_clear = osx_screen_clear,
		.screen_draw_box = osx_screen_draw_box,
		.screen_get_dimensions = osx_screen_get_dimensions,
		.screen_list = osx_screen_list,
		.scroll = osx_scroll,
		.monitor_file = osx_monitor_file,
		.insert_text_mode = osx_insert_text_mode,
		.send_paste = osx_send_paste,
		.detect_ui_elements = macos_detect_ui_elements,
		.free_ui_elements = macos_free_ui_elements,
	};

	main(&platform);
	exit(0);
}


void platform_run(int (*main)(struct platform *platform))
{
	pthread_t thread;

	[NSApplication sharedApplication];
	[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

	macos_init_input();
	macos_init_mouse();
	macos_init_screen();

	pthread_create(&thread, NULL, mainloop, (void *)main);

	[NSApp run];
}
