
#define UNICODE 1
#include <stdlib.h>
#include "windows.h"

static int keyboard_grabbed = 0;

static struct input_event *grab_events;
static size_t ngrab_events;

static int is_grabbed_key(uint8_t code, uint8_t mods)
{
	size_t i;
	for (i = 0; i < ngrab_events; i++)
		if (grab_events[i].code == code && grab_events[i].mods == mods)
			return 1;

	return 0;
}

static const char *input_lookup_name(uint8_t code, int shifted);

static LRESULT CALLBACK keyboardHook(int nCode, WPARAM wParam, LPARAM lParam)
{
	KBDLLHOOKSTRUCT *ev = (KBDLLHOOKSTRUCT *)lParam;

	uint8_t code = ev->vkCode;
	uint8_t mods = 0;
	uint8_t pressed = 0;

	if (ev->flags & LLKHF_INJECTED)
		goto passthrough;

	//https://learn.microsoft.com/en-us/previous-versions/windows/desktop/legacy/ms644985(v=vs.85)
	switch (wParam) {
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			pressed = 1;
			break;
		case WM_KEYUP:
		case WM_SYSKEYUP:
			pressed = 0;
			break;
		default:
			goto passthrough;
	}

	mods = (
		((GetKeyState(VK_SHIFT) & 0x8000) ? PLATFORM_MOD_SHIFT : 0) |
		((GetKeyState(VK_CONTROL) & 0x8000) ? PLATFORM_MOD_CONTROL : 0) |
		((GetKeyState(VK_MENU) & 0x8000) ? PLATFORM_MOD_ALT : 0) |
		((GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000) ? PLATFORM_MOD_META : 0));

	PostMessage(NULL, WM_KEY_EVENT, pressed << 16 | mods << 8 | code, 0);

	if (is_grabbed_key(code, mods))
		return 1;

	if (keyboard_grabbed)
		return 1;  //return non zero to consume the input

passthrough:
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static COLORREF str_to_colorref(const char *s)
{

    #define HEXVAL(c) ((c >= '0' && c <= '9') ? (c-'0') :\
            (c >= 'a' && c <= 'f') ?  (c - 'a' + 10) :\
            (c-'A'+10))

    if (s[0] == '#')
        s++;

    size_t len = strlen(s);
    
    /* Handle both 6-char (#RRGGBB) and 8-char (#RRGGBBAA) hex colors */
    /* Note: Windows COLORREF ignores alpha, so we just parse RGB portion */
    if (len == 6 || len == 8)
        return HEXVAL(s[5]) << 16 |
            HEXVAL(s[4]) << 20 |
            HEXVAL(s[3]) << 8 |
            HEXVAL(s[2]) << 12 |
            HEXVAL(s[1]) << 0 |
            HEXVAL(s[0]) << 4;

    return 0;
}

/* Extract alpha value from RGBA hex string (e.g., #FF4500AA returns 170) */
static BYTE str_to_alpha(const char *s)
{
    if (s[0] == '#')
        s++;

    size_t len = strlen(s);
    
    /* 8-char format includes alpha: #RRGGBBAA */
    if (len == 8) {
        return (BYTE)((HEXVAL(s[6]) << 4) | HEXVAL(s[7]));
    }
    
    /* 6-char format has no alpha, return fully opaque */
    return 255;
}

static void utf8_encode(const wchar_t *wstr, char *buf, size_t buf_sz)
{
    int nw = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, buf, buf_sz, NULL, NULL);
    buf[nw] = 0;
}

/* Platform Implementation.  */

static void screen_clear(screen_t scr)
{
	wn_screen_clear(scr);
}

static void screen_draw_box(screen_t scr, int x, int y, int w, int h, const char *color)
{
	wn_screen_add_box(scr, x, y, w, h, str_to_colorref(color));
}

static struct input_event *input_next_event(int timeout)
{
	MSG msg;
	static struct input_event ev;

	UINT_PTR timer = SetTimer(0, 0, timeout, 0);

	while (1)
	{
		GetMessage(&msg, 0, 0, 0);
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		switch (msg.message) {
			case WM_KEY_EVENT:
			ev.code = msg.wParam & 0xFF;
			ev.mods = (msg.wParam >> 8) & 0xFF;
			ev.pressed = (msg.wParam >> 16) & 0xFF;

			KillTimer(0, timer);
			return &ev;
			case WM_TIMER:
			KillTimer(0, timer);
			if (timeout)
				return NULL;
			break;
			case WM_FILE_UPDATED:
			return NULL;
			break;
		}
	}
}

static void init_hint(const char *bg, const char *fg, int border_radius, const char *font_family)
{
	//TODO: handle font family.
	BYTE alpha = str_to_alpha(bg);
	wn_screen_set_hintinfo(str_to_colorref(bg), str_to_colorref(fg), alpha, border_radius);
}

//====================================================================================

void screen_list(screen_t scr[MAX_SCREENS], size_t *n)
{
	// Return all enumerated screens instead of just primary
	wn_get_all_screens(scr, n);
}
//====================================================================================

void mouse_show()
{
	// Restore all system cursors to default
	SystemParametersInfo(SPI_SETCURSORS, 0, NULL, 0);
	
	// Force cursor refresh
	SetCursor(LoadCursor(NULL, IDC_ARROW));
}

void mouse_hide()
{
	static HANDLE hCursor = 0;
	static HANDLE cursor = 0;
	if (!hCursor) {
		uint8_t andmask[32*4];
		uint8_t xormask[32*4];

		memset(andmask, 0xFF, sizeof andmask);
		memset(xormask, 0x00, sizeof xormask);

		cursor = CreateCursor(GetModuleHandle(NULL),0,0,32,32, andmask, xormask);
		assert(cursor);
	}

	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32512);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32513);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32514);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32515);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32516);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32640);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32641);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32642);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32643);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32644);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32645);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32646);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32648);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32649);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32650);
	hCursor = CopyImage(cursor, IMAGE_CURSOR, 0, 0, 0);
	SetSystemCursor(hCursor, 32651);
}

static void print_last_error()
{
	char *buf = NULL;

	FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buf, 0, NULL);

	printf("ERROR: %s\n", buf);
}

static struct input_event *input_wait(struct input_event *events, size_t n)
{
	grab_events = events;
	ngrab_events = n;

	while (1) {
		size_t i;
		struct input_event *ev = input_next_event(0);

		if (!ev)
			return ev;

		for (i = 0; i < n; i++)
			if (ev->pressed && events[i].code == ev->code && events[i].mods == ev->mods) {
				grab_events = NULL;
				ngrab_events = 0;

				return ev;
			}
	}
}


static void scroll(int direction)
{
	DWORD delta = -(DWORD)((float)WHEEL_DELTA/2.5);
	DWORD flags = MOUSEEVENTF_WHEEL;
	
	switch (direction) {
	case SCROLL_UP:
		delta *= -1;
		break;
	case SCROLL_DOWN:
		/* delta is already negative for down */
		break;
	case SCROLL_LEFT:
		flags = MOUSEEVENTF_HWHEEL;
		delta *= -1;  /* Negative for left */
		break;
	case SCROLL_RIGHT:
		flags = MOUSEEVENTF_HWHEEL;
		/* delta is already negative, which means right for HWHEEL */
		break;
	}

	mouse_event(flags, 0, 0, delta, 0);
}

static const char *input_lookup_name(uint8_t code, int shifted)
{
	static char *keymap[256];
	static char *shifted_keymap[256];
	static int init = 0;

	//FIXME: account for keymap changes.
	if (!init) {
		wchar_t buf[64];
		uint8_t state[256] = {0};
		int code;
		int ret;

		for (code = 0; code < 256; code++) {
			char *name = malloc(64);
			char *shifted_name = malloc(64);

			state[VK_SHIFT] = 0;
			ret = ToUnicode(code, 0, state, buf, sizeof buf / sizeof buf[0], 0);
			utf8_encode(buf, name, 64);
			if (!ret)
				strcpy(name, "UNKNOWN");

			state[VK_SHIFT] = 0xff;
			ret = ToUnicode(code, 0, state, buf, sizeof buf / sizeof buf[0], 0);
			utf8_encode(buf, shifted_name, 64);
			if (!ret)
				strcpy(shifted_name, "UNKNOWN");

			switch (name[0]) {
				case '\033': strcpy(name, "esc"); break;
				case '\x08': strcpy(name, "backspace"); break;
				case '\x0d': strcpy(name, "enter"); break;
				case '\x20': strcpy(name, "space"); break;
			}

			keymap[code] = name;
			shifted_keymap[code] = shifted_name;
		}

		//Fix up conflicting codes
		strcpy(keymap[0x6E], "decimal"); //Avoid conflict with "." (0xBE)
		strcpy(shifted_keymap[0x6E], "decimal"); //Avoid conflict with "." (0xBE)
		
		//Fix modifier keys and special keys that ToUnicode doesn't handle
		strcpy(keymap[VK_SHIFT], "shift");
		strcpy(shifted_keymap[VK_SHIFT], "shift");
		strcpy(keymap[VK_CONTROL], "ctrl");
		strcpy(shifted_keymap[VK_CONTROL], "ctrl");
		strcpy(keymap[VK_MENU], "alt");  // VK_MENU is Alt key
		strcpy(shifted_keymap[VK_MENU], "alt");
		strcpy(keymap[VK_LSHIFT], "lshift");
		strcpy(shifted_keymap[VK_LSHIFT], "lshift");
		strcpy(keymap[VK_RSHIFT], "rshift");
		strcpy(shifted_keymap[VK_RSHIFT], "rshift");
		strcpy(keymap[VK_LCONTROL], "lctrl");
		strcpy(shifted_keymap[VK_LCONTROL], "lctrl");
		strcpy(keymap[VK_RCONTROL], "rctrl");
		strcpy(shifted_keymap[VK_RCONTROL], "rctrl");
		strcpy(keymap[VK_LMENU], "lalt");
		strcpy(shifted_keymap[VK_LMENU], "lalt");
		strcpy(keymap[VK_RMENU], "ralt");
		strcpy(shifted_keymap[VK_RMENU], "ralt");
		strcpy(keymap[VK_LWIN], "lwin");
		strcpy(shifted_keymap[VK_LWIN], "lwin");
		strcpy(keymap[VK_RWIN], "rwin");
		strcpy(shifted_keymap[VK_RWIN], "rwin");
		
		//Fix other special keys
		strcpy(keymap[VK_TAB], "tab");
		strcpy(shifted_keymap[VK_TAB], "tab");
		strcpy(keymap[VK_CAPITAL], "capslock");
		strcpy(shifted_keymap[VK_CAPITAL], "capslock");
		strcpy(keymap[VK_RETURN], "enter");
		strcpy(shifted_keymap[VK_RETURN], "enter");

		init++;
	}

	if (shifted)
		return shifted_keymap[code];
	else
		return keymap[code];
}

static void send_key(uint8_t code, int pressed)
{
	INPUT input;

	input.type = INPUT_KEYBOARD;
	input.ki.wVk = code;
	input.ki.dwFlags = pressed ? 0 : KEYEVENTF_KEYUP;

	SendInput(1, &input, sizeof(INPUT));
}

static void copy_selection()
{
	if (keyboard_grabbed) {
		return;
	}
	
	Sleep(20);
	send_key(VK_CONTROL, 1);
	send_key('C', 1);
	send_key('C', 0);
	send_key(VK_CONTROL, 0);
	Sleep(20);
}


static uint8_t input_lookup_code(const char *name, int *shifted)
{
	//TODO: fixme (eliminate input_lookup_code in platform.h and move reverse lookups into the calling code)

	for (int i=0;i<256;i++) {
		if (!strcmp(input_lookup_name(i, 0), name)) {
			*shifted = 0;
			return i;
		} else if(!strcmp(input_lookup_name(i, 1), name)) {
			*shifted = 1;
			return i;
		}
	}

	return 0;
}

static void mouse_get_position(screen_t *_scr, int *x, int *y)
{
	int sx, sy;

	POINT p;
	GetCursorPos(&p);

	struct screen *scr = wn_get_screen_at(p.x, p.y);
	assert(scr);

	wn_screen_get_dimensions(scr, &sx, &sy, NULL, NULL);

	if (_scr) *_scr = scr;
	if (x) *x = p.x - sx;
	if (y) *y = p.y - sy;
}

static void screen_get_dimensions(screen_t scr, int *w, int *h)
{
	wn_screen_get_dimensions(scr, NULL, NULL, w, h);
}

static void screen_get_offset(screen_t scr, int *x, int *y)
{
	wn_screen_get_dimensions(scr, x, y, NULL, NULL);
}

static void mouse_move(screen_t scr, int x, int y)
{
	int sx, sy;

	wn_screen_get_dimensions(scr, &sx, &sy, NULL, NULL);
	SetCursorPos(sx + x, sy + y);
}

static void input_grab_keyboard()
{
	int i;
	for (i = 0; i < 256; i++)
		if (GetKeyState(i))
			send_key(i, 0);

	keyboard_grabbed = 1;
	
	/* Use BlockInput to block all keyboard and mouse input
	 * 
	 * IMPORTANT: Requires administrator privileges to work!
	 */
	if (!BlockInput(TRUE)) {
        // fprintf(stderr, "Warning: BlockInput failed (may need admin privileges)\n");
	}
}

static void input_ungrab_keyboard()
{
	keyboard_grabbed = 0;
	
	// Unblock keyboard input
	BlockInput(FALSE);
}

void hint_draw(screen_t scr, struct hint *hints, size_t nhints)
{
	wn_screen_set_hints(scr, hints, nhints);
}

static void get_button_flags(int btn, DWORD *_up, DWORD *_down)
{
	DWORD up = MOUSEEVENTF_LEFTUP;
	DWORD down = MOUSEEVENTF_LEFTDOWN;
	switch (btn) {
		case 1: up = MOUSEEVENTF_LEFTUP; down = MOUSEEVENTF_LEFTDOWN; break;
		case 2: up = MOUSEEVENTF_MIDDLEUP; down = MOUSEEVENTF_MIDDLEDOWN; break;
		case 3: up = MOUSEEVENTF_RIGHTUP; down = MOUSEEVENTF_RIGHTDOWN; break;
	}

	if (_down) *_down = down;
	if (_up) *_up = up;
}

static void mouse_click(int btn)
{
	INPUT inputs[2] = {0};
	DWORD up, down;

	get_button_flags(btn, &up, &down);

	inputs[0].type = INPUT_MOUSE;
	inputs[0].mi.dwFlags = down;

	inputs[1].type = INPUT_MOUSE;
	inputs[1].mi.dwFlags = up;

	SendInput(2, inputs, sizeof(INPUT));
}

static void mouse_down(int btn)
{
	INPUT input = {0};

	input.type = INPUT_MOUSE;
	get_button_flags(btn, NULL, &input.mi.dwFlags);

	SendInput(1, &input, sizeof(INPUT));
}

static void mouse_up(int btn)
{
	INPUT input = {0};

	input.type = INPUT_MOUSE;
	get_button_flags(btn, &input.mi.dwFlags, NULL);

	SendInput(1, &input, sizeof(INPUT));
}

static void commit()
{
	size_t i;
	screen_t screens[MAX_SCREENS];
	size_t nscreens;
	
	/* Redraw all screens to ensure proper clearing when switching monitors */
	wn_get_all_screens(screens, &nscreens);
	for (i = 0; i < nscreens; i++) {
		wn_screen_redraw(screens[i]);
	}
}

/* UI element detector functions (implemented in ui_detector.c) */
extern struct ui_detection_result *windows_detect_ui_elements(void);
extern void windows_free_ui_elements(struct ui_detection_result *result);

/* UI Automation cleanup function */
extern void uiautomation_cleanup(void);

//====================================================================================
// Paste Key
//====================================================================================

static void send_paste()
{
	if (keyboard_grabbed) {
		return;
	}
	
	Sleep(20);
	
	/* Send Ctrl+V using SendInput directly for better reliability */
	INPUT inputs[4];
	UINT result;
	
	/* Initialize all inputs */
	memset(inputs, 0, sizeof(inputs));
	
	/* Press Ctrl */
	inputs[0].type = INPUT_KEYBOARD;
	inputs[0].ki.wVk = VK_CONTROL;
	inputs[0].ki.wScan = 0;
	inputs[0].ki.dwFlags = 0;
	inputs[0].ki.time = 0;
	inputs[0].ki.dwExtraInfo = 0;
	
	/* Press V */
	inputs[1].type = INPUT_KEYBOARD;
	inputs[1].ki.wVk = 'V';
	inputs[1].ki.wScan = 0;
	inputs[1].ki.dwFlags = 0;
	inputs[1].ki.time = 0;
	inputs[1].ki.dwExtraInfo = 0;
	
	/* Release V */
	inputs[2].type = INPUT_KEYBOARD;
	inputs[2].ki.wVk = 'V';
	inputs[2].ki.wScan = 0;
	inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
	inputs[2].ki.time = 0;
	inputs[2].ki.dwExtraInfo = 0;
	
	/* Release Ctrl */
	inputs[3].type = INPUT_KEYBOARD;
	inputs[3].ki.wVk = VK_CONTROL;
	inputs[3].ki.wScan = 0;
	inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
	inputs[3].ki.time = 0;
	inputs[3].ki.dwExtraInfo = 0;
	
	SendInput(4, inputs, sizeof(INPUT));
	Sleep(20);
}

//====================================================================================
// Simple Text Input Box
//====================================================================================

static HWND g_edit_hwnd = NULL;
static char *g_input_buffer = NULL;
static size_t g_input_buffer_size = 0;
static int g_input_submitted = 0;

static LRESULT CALLBACK SimpleEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
		case WM_CHAR:
			if (wParam == VK_RETURN) {
				/* Enter pressed - submit */
				GetWindowTextA(hwnd, g_input_buffer, g_input_buffer_size);
				g_input_submitted = 1;
				DestroyWindow(hwnd);
				return 0;
			} else if (wParam == VK_ESCAPE) {
				g_input_buffer[0] = '\0';
				g_input_submitted = 0;
				DestroyWindow(hwnd);
				return 0;
			}
			break;
			
		case WM_KILLFOCUS:
			if (!g_input_submitted) {
				g_input_buffer[0] = '\0';
				g_input_submitted = 0;
				DestroyWindow(hwnd);
			}
			return 0;
	}
	
	return CallWindowProc((WNDPROC)GetWindowLongPtr(hwnd, GWLP_USERDATA), hwnd, msg, wParam, lParam);
}

static int insert_text_mode(screen_t scr)
{
	char text_buffer[1024] = {0};
	g_input_buffer = text_buffer;
	g_input_buffer_size = sizeof(text_buffer);
	g_input_submitted = 0;
	
	input_ungrab_keyboard();
	
	copy_selection();
	Sleep(50);
	
	/* Hide warpd overlay */
	screen_clear(scr);
	commit();
	
	/* Get cursor position */
	POINT cursor_pos;
	GetCursorPos(&cursor_pos);
	
	/* Create a simple borderless edit control */
	HWND hwndEdit = CreateWindowExA(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		"EDIT",
		"",
		WS_POPUP | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
		cursor_pos.x + 10,
		cursor_pos.y + 10,
		300,  /* width */
		25,   /* height */
		NULL,
		NULL,
		GetModuleHandle(NULL),
		NULL
	);
	
	if (!hwndEdit) {
		fprintf(stderr, "ERROR: Failed to create edit control\n");
		input_grab_keyboard();
		return 0;
	}
	
	g_edit_hwnd = hwndEdit;
	
	/* Pre-fill with clipboard text if available */
	if (OpenClipboard(NULL)) {
		HANDLE hData = GetClipboardData(CF_TEXT);
		if (hData) {
			char *clipboard_text = (char*)GlobalLock(hData);
			if (clipboard_text) {
				SetWindowTextA(hwndEdit, clipboard_text);
				GlobalUnlock(hData);
			}
		}
		CloseClipboard();
	}
	
	/* Subclass the edit control to handle Enter/Escape */
	WNDPROC oldProc = (WNDPROC)SetWindowLongPtr(hwndEdit, GWLP_WNDPROC, (LONG_PTR)SimpleEditProc);
	SetWindowLongPtr(hwndEdit, GWLP_USERDATA, (LONG_PTR)oldProc);
	
	/* Show and focus */
	ShowWindow(hwndEdit, SW_SHOW);
	SetForegroundWindow(hwndEdit);
	SetFocus(hwndEdit);
	
	/* Select all text */
	SendMessage(hwndEdit, EM_SETSEL, 0, -1);
	
	/* Message loop */
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		if (!IsWindow(hwndEdit)) {
			break;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	
	
	/* Process result */
	int success = 0;
	if (g_input_submitted && text_buffer[0] != '\0') {
		if (OpenClipboard(NULL)) {
			EmptyClipboard();
			
			size_t len = strlen(text_buffer);
			HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
			if (hMem) {
				char *pMem = (char*)GlobalLock(hMem);
				if (pMem) {
					strcpy(pMem, text_buffer);
					GlobalUnlock(hMem);
					SetClipboardData(CF_TEXT, hMem);
				}
			}
			CloseClipboard();
		}
		
		Sleep(100);
		send_paste();
		success = 1;
	}
	
	input_grab_keyboard();
	return success;
}

static void cleanup_on_exit(void)
{
	// Restore system cursors
	mouse_show();
	// Cleanup UI Automation
	uiautomation_cleanup();
}

void platform_run(int (*main)(struct platform *platform))
{
	SetWindowsHookEx(WH_KEYBOARD_LL, keyboardHook, GetModuleHandle(NULL), 0);
	wn_init_screen();
	
	/* Register cleanup function to be called on exit */
	atexit(cleanup_on_exit);

	static struct platform platform;

	platform.init_hint = init_hint;
	platform.hint_draw = hint_draw;
	platform.screen_draw_box = screen_draw_box;
	platform.input_next_event = input_next_event;
	platform.input_wait = input_wait;
	platform.screen_clear = screen_clear;

	platform.screen_get_dimensions = screen_get_dimensions;
	platform.screen_get_offset = screen_get_offset;
	platform.screen_list = screen_list;
	platform.scroll = scroll;
	platform.mouse_click = mouse_click;
	platform.mouse_down = mouse_down;
	platform.mouse_get_position = mouse_get_position;
	platform.mouse_hide = mouse_hide;
	platform.mouse_move = mouse_move;
	platform.mouse_show = mouse_show;
	platform.mouse_up = mouse_up;
	platform.input_ungrab_keyboard = input_ungrab_keyboard;
	platform.commit = commit;
	platform.copy_selection = copy_selection;
	platform.input_grab_keyboard = input_grab_keyboard;
	platform.input_lookup_code = input_lookup_code;
	platform.input_lookup_name = input_lookup_name;
	platform.monitor_file = wn_monitor_file;

	/* UI element detection for smart hint mode */
	platform.detect_ui_elements = windows_detect_ui_elements;
	platform.free_ui_elements = windows_free_ui_elements;
	
	/* Insert text mode and paste */
	platform.insert_text_mode = insert_text_mode;
	platform.send_paste = send_paste;

	exit(main(&platform));
}
