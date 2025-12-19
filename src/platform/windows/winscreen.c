#include "windows.h"

#define TRANSPARENT_COLOR RGB(0,0,1)
#define AcquireMutex(mtx) assert(WaitForSingleObject(mtx, INFINITE) == WAIT_OBJECT_0)

static DWORD ui_thread_id;
static HANDLE mtx;

struct box {
	RECT rect;
	COLORREF color;
};

struct screen {
	/* Position in the virtual screen space */
	int x;
	int y;
	int w;
	int h;
	
	struct hint hints[4096];
	struct box boxes[2048];  /* Increased for PNG cursor support */

	size_t nboxes;
	size_t nhints;

	HANDLE mtx;
	HWND overlay;
	HDC dc;
};

static COLORREF hint_bgcol;
static COLORREF hint_fgcol;
static BYTE hint_alpha = 255;
static int hint_border_radius = 0;

static struct screen screens[16];
static size_t nscreens = 0;

/* Derive a bolder/lighter highlight color from the base background color */
static COLORREF derive_highlight_bg(COLORREF base)
{
	int r = GetRValue(base);
	int g = GetGValue(base);
	int b = GetBValue(base);
	
	/* Calculate luminance to decide if we should lighten or darken */
	int luminance = (r * 299 + g * 587 + b * 114) / 1000;
	
	if (luminance > 128) {
		/* Light color - darken it by 30% */
		r = (int)(r * 0.7);
		g = (int)(g * 0.7);
		b = (int)(b * 0.7);
	} else {
		/* Dark color - lighten it by 40% */
		r = r + (int)((255 - r) * 0.4);
		g = g + (int)((255 - g) * 0.4);
		b = b + (int)((255 - b) * 0.4);
	}
	
	return RGB(r, g, b);
}

/* Derive a contrasting foreground color for highlighted hints */
static COLORREF derive_highlight_fg(COLORREF highlight_bg)
{
	int r = GetRValue(highlight_bg);
	int g = GetGValue(highlight_bg);
	int b = GetBValue(highlight_bg);
	
	/* Calculate luminance */
	int luminance = (r * 299 + g * 587 + b * 114) / 1000;
	
	/* Use black text on light backgrounds, white on dark */
	return (luminance > 128) ? RGB(0, 0, 0) : RGB(255, 255, 255);
}

/* Draw a filled rounded rectangle */
static void draw_rounded_rect(HDC dc, int x, int y, int w, int h, int r, HBRUSH brush)
{
	if (r <= 0) {
		RECT rect = {x, y, x + w, y + h};
		FillRect(dc, &rect, brush);
		return;
	}
	
	/* Clamp radius */
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	
	/* Create a rounded rectangle region */
	HRGN rgn = CreateRoundRectRgn(x, y, x + w + 1, y + h + 1, r * 2, r * 2);
	FillRgn(dc, rgn, brush);
	DeleteObject(rgn);
}

static void draw_hints(struct screen *scr)
{
	size_t i;

	HBRUSH bgbrush = CreateSolidBrush(hint_bgcol);
	
	/* Derive highlight colors from hint_bgcolor */
	COLORREF highlight_bg = derive_highlight_bg(hint_bgcol);
	COLORREF highlight_fg = derive_highlight_fg(highlight_bg);

	SetBkColor(scr->dc, hint_bgcol);
	for (i = 0; i < scr->nhints; i++) {
		RECT rect;
		wchar_t label[64];
		struct hint *h = &scr->hints[i];

		rect.left = h->x;
		rect.top = h->y;
		rect.right = h->x+h->w;
		rect.bottom = h->y+h->h;

		if (h->highlighted) {
			HBRUSH highlight_bgbrush = CreateSolidBrush(highlight_bg);
			draw_rounded_rect(scr->dc, h->x, h->y, h->w, h->h, hint_border_radius, highlight_bgbrush);
			DeleteObject(highlight_bgbrush);
			SetBkColor(scr->dc, highlight_bg);
			SetTextColor(scr->dc, highlight_fg);
		} else {
			draw_rounded_rect(scr->dc, h->x, h->y, h->w, h->h, hint_border_radius, bgbrush);
			SetBkColor(scr->dc, hint_bgcol);
			SetTextColor(scr->dc, hint_fgcol);
		}

		/* For rounded corners, we need transparent text background */
		if (hint_border_radius > 0) {
			SetBkMode(scr->dc, TRANSPARENT);
		}
		DrawText(scr->dc, h->label, -1, &rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
		if (hint_border_radius > 0) {
			SetBkMode(scr->dc, OPAQUE);
		}
	}

	DeleteObject(bgbrush);
}

static void clear(struct screen *scr)
{
	RECT rect;
	static HBRUSH br = 0;

	if (!br)
		br = CreateSolidBrush(TRANSPARENT_COLOR);

	rect.left = 0;
	rect.top = 0;
	rect.right = scr->w;
	rect.bottom = scr->h;

	FillRect(scr->dc,  &rect, br);
}


static void redraw(struct screen *scr)
{
	size_t i;

	SetWindowPos(scr->overlay, HWND_TOPMOST, 0, 0, 0, 0, 
	             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	clear(scr);

	for (i = 0; i < scr->nboxes; i++) {
		HBRUSH brush = CreateSolidBrush(scr->boxes[i].color); //TODO: optimize
		FillRect(scr->dc, &scr->boxes[i].rect, brush);
		DeleteObject(brush);
	}

	draw_hints(scr);
}

LRESULT CALLBACK OverlayRedrawProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (Msg == WM_PAINT) {
		size_t i;
		for (i = 0; i < nscreens; i++) {
			if (screens[i].overlay == hWnd)
				redraw(&screens[i]);
		}
	}

	return DefWindowProc(hWnd, Msg, wParam, lParam);
}

static HWND create_overlay(int x, int y, int w, int h)
{
	static WNDCLASS wc = {0};
	static int init = 0;

	const char CLASS_NAME[] = "warpd";

	HINSTANCE hInstance = GetModuleHandle(NULL);

	if (!init) {
		wc.lpfnWndProc = OverlayRedrawProc; // Window callback function
		wc.hInstance = hInstance;
		wc.lpszClassName = CLASS_NAME;

		RegisterClass(&wc);
	}

	HWND wnd = CreateWindowEx(WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
				      WS_EX_LAYERED, // Optional window styles.
				  CLASS_NAME,	     // Window class
				  "",		     // Window text
				  WS_POPUP,	     // Window style

				  // Size and position
				  x, y, w, h,

				  NULL,	     // Parent window
				  NULL,	     // Menu
				  hInstance, // Instance handle
				  NULL	     // Additional application data
	);

	assert(wnd);

	SetLayeredWindowAttributes(wnd, TRANSPARENT_COLOR, hint_alpha, LWA_COLORKEY | LWA_ALPHA);
	return wnd;
}

static BOOL CALLBACK screenCallback(HMONITOR mon, HDC hdc, LPRECT dim, LPARAM lParam)
{
AcquireMutex(mtx);

    assert(nscreens < sizeof screens / sizeof screens[0]);

    struct screen *scr = &screens[nscreens++];

    scr->x = dim->left;
    scr->y = dim->top;
    scr->h = dim->bottom - dim->top;
    scr->w = dim->right - dim->left;

    scr->nboxes = 0;
    scr->nhints = 0;

    scr->overlay = create_overlay(scr->x, scr->y, scr->w, scr->h);
    scr->dc = GetDC(scr->overlay);

    ShowWindow(scr->overlay, SW_SHOW);
    
    SetWindowPos(scr->overlay, HWND_TOPMOST, 0, 0, 0, 0, 
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

ReleaseMutex(mtx);

    return TRUE;
}

/* Main draw loop. */

static DWORD WINAPI uithread(void *arg)
{
	EnumDisplayMonitors(0, 0, screenCallback, 0);

	while (1) {
		MSG msg;
		GetMessage(&msg, 0, 0, 0);

AcquireMutex(mtx);

		DispatchMessage(&msg);

		if (msg.message == WM_USER)
			redraw((struct screen *)msg.lParam);

ReleaseMutex(mtx);
	}
}

/* internal screen API */

void wn_screen_redraw(struct screen *scr)
{
	PostThreadMessage(ui_thread_id, WM_USER, 0, (LPARAM)scr);
}

void wn_screen_set_hints(struct screen *scr, struct hint *hints, size_t nhints)
{
AcquireMutex(mtx);

	assert(nhints < sizeof scr->hints / sizeof scr->hints[0]);
	memcpy(scr->hints, hints, sizeof(struct hint) * nhints);
	scr->nhints = nhints;

ReleaseMutex(mtx);
}

void wn_screen_get_dimensions(struct screen *scr, int *xoff, int *yoff, int *w, int *h)
{
	if (xoff) *xoff = scr->x;
	if (yoff) *yoff = scr->y;
	if (w) *w = scr->w;
	if (h) *h = scr->h;
}

void wn_screen_add_box(struct screen *scr, int x, int y, int w, int h, COLORREF color)
{
AcquireMutex(mtx);

	assert(scr->nboxes < sizeof scr->boxes / sizeof scr->boxes[0]);

	struct box *box = &scr->boxes[scr->nboxes];

	box->rect.top = y;
	box->rect.left = x;
	box->rect.right = x + w;
	box->rect.bottom = y + h;

	box->color = color;
	scr->nboxes++;

ReleaseMutex(mtx);
}

void wn_screen_clear(struct screen *scr)
{
AcquireMutex(mtx);

	scr->nboxes = 0;
	scr->nhints = 0;

ReleaseMutex(mtx);
}

void wn_screen_set_hintinfo(COLORREF _hint_bgcol, COLORREF _hint_fgcol, BYTE alpha, int border_radius)
{
	AcquireMutex(mtx);
	hint_bgcol = _hint_bgcol;
	hint_fgcol = _hint_fgcol;
	hint_alpha = alpha;
	hint_border_radius = border_radius;

	/* Apply alpha to all existing overlay windows */
	for (size_t i = 0; i < nscreens; i++) {
		if (screens[i].overlay) {
			SetLayeredWindowAttributes(screens[i].overlay, TRANSPARENT_COLOR, hint_alpha, LWA_COLORKEY | LWA_ALPHA);
		}
	}
	ReleaseMutex(mtx);
}

void wn_init_screen()
{
	mtx = CreateMutex(0, 0, NULL);

	CreateThread(0, 0, uithread, 0, 0, &ui_thread_id);
	Sleep(200); //FIXME
}

struct screen *wn_get_screen_at(int x, int y)
{
	size_t i;

	for (i = 0; i < nscreens; i++) {
		if (
			x >= screens[i].x && x < screens[i].x + screens[i].w &&
			y >= screens[i].y && y < screens[i].y + screens[i].h
		)
			return &screens[i];
	}

	/* Fallback to primary monitor (index 0) when no screen matches */
	return nscreens > 0 ? &screens[0] : NULL;
}

void wn_get_all_screens(struct screen **scr_array, size_t *count)
{
	size_t i;
	
	for (i = 0; i < nscreens; i++) {
		scr_array[i] = &screens[i];
	}
	
	*count = nscreens;
}
