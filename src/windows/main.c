/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * © 2019 Raheman Vaiya (see: LICENSE).
 * Fork maintained by atuan26.
 */

#include "../warpd.h"

#include <assert.h>
#include <direct.h>
#include <stdio.h>
#include <windows.h>
#include <winuser.h>
#include <shlobj.h>

static HWND icon_wnd;
static HMENU icon_menu;
static char config_path[1024];
static char config_dir[1024];

static const char *icon_menu_items[] = {
	"Run at startup",
	"Edit config",
	"Help",
	"About",
	"Exit",
};

struct platform *platform;

uint64_t get_time_us()
{
    static LARGE_INTEGER tps = {0}; //ticks per second
	LARGE_INTEGER ticks;

    if (!tps.QuadPart)
        assert(QueryPerformanceFrequency(&tps));

	QueryPerformanceCounter(&ticks);
	return (ticks.QuadPart * 1E6) / tps.QuadPart;
}

static void get_startup_shortcut_path(char *path, size_t size)
{
	char startupFolder[MAX_PATH];
	SHGetFolderPath(NULL, CSIDL_STARTUP, NULL, 0, startupFolder);
	snprintf(path, size, "%s\\warpd.lnk", startupFolder);
}

static int is_startup_enabled()
{
	char shortcutPath[MAX_PATH];
	get_startup_shortcut_path(shortcutPath, sizeof(shortcutPath));
	
	DWORD attrs = GetFileAttributes(shortcutPath);
	return (attrs != INVALID_FILE_ATTRIBUTES);
}

static void toggle_startup()
{
	char shortcutPath[MAX_PATH];
	get_startup_shortcut_path(shortcutPath, sizeof(shortcutPath));
	
	if (is_startup_enabled()) {
		// Remove shortcut
		if (DeleteFile(shortcutPath)) {
			MessageBox(NULL, "warpd will no longer run at startup", "Startup", MB_OK | MB_ICONINFORMATION);
		} else {
			MessageBox(NULL, "Failed to remove startup shortcut", "Error", MB_OK | MB_ICONERROR);
		}
	} else {
		// Create shortcut using shell command
		char exePath[MAX_PATH];
		GetModuleFileName(NULL, exePath, MAX_PATH);
		
		// Use PowerShell to create shortcut (simpler than COM)
		char command[1024];
		snprintf(command, sizeof(command),
			"powershell -Command \"$ws = New-Object -ComObject WScript.Shell; "
			"$s = $ws.CreateShortcut('%s'); "
			"$s.TargetPath = '%s'; "
			"$s.Save()\"",
			shortcutPath, exePath);
		
		STARTUPINFO si = {0};
		PROCESS_INFORMATION pi = {0};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		
		if (CreateProcess(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
			WaitForSingleObject(pi.hProcess, 5000);
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			
			if (is_startup_enabled()) {
				MessageBox(NULL, "warpd will now run at startup", "Startup", MB_OK | MB_ICONINFORMATION);
			} else {
				MessageBox(NULL, "Failed to create startup shortcut", "Error", MB_OK | MB_ICONERROR);
			}
		} else {
			MessageBox(NULL, "Failed to create startup shortcut", "Error", MB_OK | MB_ICONERROR);
		}
	}
}

static LRESULT CALLBACK IconWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg) {
		case WM_CLOSE:
			ShowWindow(hWnd, SW_HIDE);
			return 1;
			break;
		case WM_USER:
			if (lParam == WM_RBUTTONDOWN) {
				POINT pt;
				GetCursorPos(&pt);
				
				// Update the startup menu item to show current state
				MENUITEMINFO mii = {0};
				mii.cbSize = sizeof(MENUITEMINFO);
				mii.fMask = MIIM_STATE;
				mii.fState = is_startup_enabled() ? MFS_CHECKED : MFS_UNCHECKED;
				SetMenuItemInfo(icon_menu, 0, TRUE, &mii);
				
				TrackPopupMenu(icon_menu, TPM_RIGHTALIGN,
						pt.x, pt.y,
						0,
						icon_wnd,
						NULL);
			}

			break;

		case WM_COMMAND:
			if (!strcmp(icon_menu_items[wParam], "Run at startup")) {
				toggle_startup();
			} else if (!strcmp(icon_menu_items[wParam], "Help")) {
				ShellExecute(NULL, "open", "https://github.com/atuan26/warpd/blob/master/warpd.1.md", NULL, NULL, SW_SHOWNORMAL);
			} else if (!strcmp(icon_menu_items[wParam], "About")) {
				char about_msg[512];
				snprintf(about_msg, sizeof(about_msg), 
					"warpd %s\n\n"
					"A modal keyboard-driven pointing system\n\n"
					"Repository: https://github.com/atuan26/warpd\n"
					"Config: %s\n\n"
					"Maintained by atuan26", 
					VERSION, config_path);
				MessageBox(NULL, about_msg, "About warpd", MB_OK | MB_ICONINFORMATION);
			} else if (!strcmp(icon_menu_items[wParam], "Exit")) {
				NOTIFYICONDATA nic = {0};
				nic.cbSize = sizeof(NOTIFYICONDATA);
				nic.uID = 0;

				Shell_NotifyIconA(NIM_DELETE, &nic);
				exit(0);
			} else if (!strcmp(icon_menu_items[wParam], "Edit config")) {
				ShellExecute(NULL, "open", config_path, NULL, NULL, SW_SHOWNORMAL);
			}
			break;
	}

	return DefWindowProc(hWnd, Msg, wParam, lParam);
}

static void init_icon(HICON icon)
{
	int i;
	static WNDCLASS wc = {0};
	static int init = 0;

	char CLASS_NAME[] = "warpd-iconwindow";

	HINSTANCE hInstance = GetModuleHandle(NULL);

	wc.lpfnWndProc = IconWindowProc; // Window callback function
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;
	wc.hbrBackground = CreateSolidBrush(RGB(255,0,0));

	RegisterClass(&wc);

	icon_wnd = CreateWindowEx(WS_EX_TOOLWINDOW, CLASS_NAME, "warpd", WS_OVERLAPPEDWINDOW, 1, 1, 1, 1, NULL, NULL, hInstance, NULL);

	assert(icon_wnd);

	NOTIFYICONDATA nic;

	snprintf(nic.szTip, sizeof(nic.szTip), "warpd %s - Modal keyboard pointing", VERSION);
	nic.hIcon = icon;
	nic.cbSize = sizeof(NOTIFYICONDATA);
	nic.hWnd = icon_wnd;
	nic.uID = 0;
	nic.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_STATE | NIF_GUID;
	nic.uCallbackMessage = WM_USER;
	nic.dwStateMask = NIS_HIDDEN;
	nic.dwState = 0;

	Shell_NotifyIconA(NIM_ADD, &nic);

	icon_menu = CreatePopupMenu();
	for (i = 0; i < sizeof icon_menu_items / sizeof icon_menu_items[0]; i++)
		AppendMenu(icon_menu, MF_STRING, i, icon_menu_items[i]);
}


static DWORD WINAPI icon_thread(void *arg) 
{
	HICON icon = LoadIcon(GetModuleHandleA(NULL), "IDI_APPLICATION");
	init_icon(icon);

	while (1) {
		MSG msg;
		GetMessage(&msg, 0, 0, 0);
		DispatchMessage(&msg);
	}

	return 0;
}

static int platform_main(struct platform *_platform)
{
	platform = _platform;
	parse_config(config_path);
	init_mouse();
	init_hints();

	daemon_loop(config_path); //FIXME pass in a proper path + implement monitor_file

	return 0;
}

void redirect_stdout()
{
	long lStdHandle = (long)GetStdHandle(STD_OUTPUT_HANDLE);
	int hConHandle = _open_osfhandle(lStdHandle, _O_TEXT);
	FILE *fp = _fdopen( hConHandle, "w" );
	*stdout = *fp;
	setvbuf( stdout, NULL, _IONBF, 0 );
}


int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	CreateMutex( NULL, TRUE, "warpd" );

	if (GetLastError() ==  ERROR_ALREADY_EXISTS) {
		MessageBox(NULL, "warpd is already running", "", MB_OK|MB_ICONSTOP);
		exit(0);
	}

	sprintf(config_dir, "%s\\warpd", getenv("APPDATA"));
	sprintf(config_path, "%s\\warpd.conf", config_dir);

	CreateDirectory(config_dir, NULL);
	HANDLE fh = CreateFile(config_path, GENERIC_WRITE, 0, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
	CloseHandle(fh);

	CreateThread(0, 0, icon_thread, 0, 0, NULL);

	platform_run(platform_main);

}
