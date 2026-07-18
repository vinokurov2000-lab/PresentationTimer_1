#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdint.h>
#include <stdlib.h>

#define APP_NAME L"Таймер презентации"
#define WM_TRAY (WM_APP + 1)
#define TIMER_ID 1
#define HK_TOGGLE 101
#define HK_RESET 102
#define HK_SETTINGS 103
#define HK_ADD 104
#define HK_SUB 105
#define IDM_TOGGLE 201
#define IDM_RESET 202
#define IDM_SETTINGS 203
#define IDM_SHOW 204
#define IDM_EXIT 205
#define IDC_MINUTES 301
#define IDC_SECONDS 302
#define IDC_FONTSIZE 303
#define IDC_CLICKTHROUGH 304
#define IDC_NORMAL_COLOR 305
#define IDC_WARNING_COLOR 306
#define IDC_BACKGROUND_COLOR 307
#define IDC_SAVE 308
#define IDC_CANCEL 309

typedef struct {
    int duration_seconds;
    int font_size;
    COLORREF normal_color;
    COLORREF warning_color;
    COLORREF background_color;
    BOOL click_through;
    int x, y;
} Settings;

static HINSTANCE g_instance;
static HWND g_timer_window, g_settings_window;
static HFONT g_timer_font;
static Settings g_settings;
static Settings g_edit_settings;
static WCHAR g_ini_path[MAX_PATH];
static ULONGLONG g_remaining_ms;
static ULONGLONG g_started_at;
static BOOL g_running;
static BOOL g_allow_exit;
static NOTIFYICONDATAW g_nid;

static LRESULT CALLBACK TimerProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK SettingsProc(HWND, UINT, WPARAM, LPARAM);

static void MakeIniPath(void) {
    WCHAR dir[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH);
    if (!n || n >= MAX_PATH - 40) GetCurrentDirectoryW(MAX_PATH, dir);
    lstrcatW(dir, L"\\PresentationTimer");
    CreateDirectoryW(dir, NULL);
    wsprintfW(g_ini_path, L"%s\\settings.ini", dir);
}

static int ReadInt(LPCWSTR key, int fallback) {
    return GetPrivateProfileIntW(L"Timer", key, fallback, g_ini_path);
}

static void WriteInt(LPCWSTR key, int value) {
    WCHAR text[32];
    wsprintfW(text, L"%d", value);
    WritePrivateProfileStringW(L"Timer", key, text, g_ini_path);
}

static void LoadSettings(void) {
    MakeIniPath();
    g_settings.duration_seconds = ReadInt(L"DurationSeconds", 300);
    g_settings.font_size = ReadInt(L"FontSize", 72);
    g_settings.normal_color = (COLORREF)ReadInt(L"NormalColor", RGB(255,255,255));
    g_settings.warning_color = (COLORREF)ReadInt(L"WarningColor", RGB(255,77,77));
    g_settings.background_color = (COLORREF)ReadInt(L"BackgroundColor", RGB(20,24,31));
    g_settings.click_through = ReadInt(L"ClickThrough", 0) != 0;
    g_settings.x = ReadInt(L"X", CW_USEDEFAULT);
    g_settings.y = ReadInt(L"Y", CW_USEDEFAULT);
    if (g_settings.duration_seconds < 1 || g_settings.duration_seconds > 59999) g_settings.duration_seconds = 300;
    if (g_settings.font_size < 28 || g_settings.font_size > 180) g_settings.font_size = 72;
}

static void SaveSettings(void) {
    WriteInt(L"DurationSeconds", g_settings.duration_seconds);
    WriteInt(L"FontSize", g_settings.font_size);
    WriteInt(L"NormalColor", (int)g_settings.normal_color);
    WriteInt(L"WarningColor", (int)g_settings.warning_color);
    WriteInt(L"BackgroundColor", (int)g_settings.background_color);
    WriteInt(L"ClickThrough", g_settings.click_through);
    WriteInt(L"X", g_settings.x);
    WriteInt(L"Y", g_settings.y);
}

static ULONGLONG Remaining(void) {
    if (!g_running) return g_remaining_ms;
    ULONGLONG elapsed = GetTickCount64() - g_started_at;
    return elapsed >= g_remaining_ms ? 0 : g_remaining_ms - elapsed;
}

static void ResetTimer(void) {
    g_running = FALSE;
    g_remaining_ms = (ULONGLONG)g_settings.duration_seconds * 1000;
    KillTimer(g_timer_window, TIMER_ID);
    InvalidateRect(g_timer_window, NULL, TRUE);
}

static void ToggleTimer(void) {
    if (g_running) {
        g_remaining_ms = Remaining();
        g_running = FALSE;
        KillTimer(g_timer_window, TIMER_ID);
    } else if (g_remaining_ms > 0) {
        g_started_at = GetTickCount64();
        g_running = TRUE;
        SetTimer(g_timer_window, TIMER_ID, 50, NULL);
    }
    InvalidateRect(g_timer_window, NULL, TRUE);
}

static void ChangeRemaining(int delta_seconds) {
    ULONGLONG current = Remaining();
    LONGLONG changed = (LONGLONG)current + (LONGLONG)delta_seconds * 1000;
    if (changed < 0) changed = 0;
    if (changed > 59999000) changed = 59999000;
    g_remaining_ms = (ULONGLONG)changed;
    if (g_running) g_started_at = GetTickCount64();
    InvalidateRect(g_timer_window, NULL, TRUE);
}

static void ApplyClickThrough(void) {
    LONG_PTR ex = GetWindowLongPtrW(g_timer_window, GWL_EXSTYLE);
    if (g_settings.click_through) ex |= WS_EX_TRANSPARENT;
    else ex &= ~((LONG_PTR)WS_EX_TRANSPARENT);
    SetWindowLongPtrW(g_timer_window, GWL_EXSTYLE, ex);
}

static void ResizeOverlay(void) {
    if (g_timer_font) DeleteObject(g_timer_font);
    HDC dc = GetDC(g_timer_window);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(g_timer_window, dc);
    int height = -MulDiv(g_settings.font_size, dpi, 72);
    g_timer_font = CreateFontW(height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HDC measure = GetDC(g_timer_window);
    HFONT old = (HFONT)SelectObject(measure, g_timer_font);
    SIZE size = {0};
    GetTextExtentPoint32W(measure, L"000:00.0", 8, &size);
    SelectObject(measure, old);
    ReleaseDC(g_timer_window, measure);
    int w = size.cx + 34, h = size.cy + 20;
    SetWindowPos(g_timer_window, HWND_TOPMOST, 0, 0, w, h,
        SWP_NOMOVE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    HRGN region = CreateRoundRectRgn(0, 0, w + 1, h + 1, 22, 22);
    SetWindowRgn(g_timer_window, region, TRUE);
    InvalidateRect(g_timer_window, NULL, TRUE);
}

static void ApplySettings(BOOL reset) {
    ApplyClickThrough();
    ResizeOverlay();
    SetLayeredWindowAttributes(g_timer_window, 0, 235, LWA_ALPHA);
    if (reset) ResetTimer();
    SaveSettings();
}

static void AddTrayIcon(void) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_timer_window;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIconW(NULL, IDI_INFORMATION);
    lstrcpyW(g_nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

static void ShowTrayMenu(void) {
    POINT p; GetCursorPos(&p);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TOGGLE, g_running ? L"Пауза" : L"Старт");
    AppendMenuW(menu, MF_STRING, IDM_RESET, L"Сбросить");
    AppendMenuW(menu, MF_STRING, IDM_SHOW, IsWindowVisible(g_timer_window) ? L"Скрыть таймер" : L"Показать таймер");
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Настройки…");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Выход");
    SetForegroundWindow(g_timer_window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, g_timer_window, NULL);
    DestroyMenu(menu);
}

static void SetControlFont(HWND parent, int id) {
    SendDlgItemMessageW(parent, id, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

static HWND AddControl(HWND parent, LPCWSTR cls, LPCWSTR text, DWORD style,
                       int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_instance, NULL);
    if (id) SetControlFont(parent, id);
    else SendMessageW(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return c;
}

static void SetNumber(HWND window, int id, int value) {
    WCHAR s[24]; wsprintfW(s, L"%d", value); SetDlgItemTextW(window, id, s);
}

static int GetNumber(HWND window, int id, BOOL *ok) {
    WCHAR s[32]; GetDlgItemTextW(window, id, s, 32);
    WCHAR *end; long v = wcstol(s, &end, 10);
    *ok = end != s && *end == 0;
    return (int)v;
}

static void ColorButtonText(HWND window, int id, COLORREF c) {
    WCHAR s[32]; wsprintfW(s, L"Выбрать…   #%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    SetDlgItemTextW(window, id, s);
}

static BOOL PickColor(HWND owner, COLORREF *color) {
    static COLORREF custom[16];
    CHOOSECOLORW cc;
    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner; cc.rgbResult = *color; cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return FALSE;
    *color = cc.rgbResult; return TRUE;
}

static void OpenSettings(void) {
    if (g_settings_window) { SetForegroundWindow(g_settings_window); return; }
    g_edit_settings = g_settings;
    EnableWindow(g_timer_window, FALSE);
    g_settings_window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"PresentationTimerSettings", L"Настройки таймера",
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 470, 490,
        g_timer_window, NULL, g_instance, NULL);
    ShowWindow(g_settings_window, SW_SHOW);
    UpdateWindow(g_settings_window);
}

static void CreateSettingsControls(HWND h) {
    AddControl(h, L"STATIC", L"Время обратного отсчёта", 0, 24, 22, 260, 22, 0);
    AddControl(h, L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_CENTER, 24, 49, 76, 28, IDC_MINUTES);
    AddControl(h, L"STATIC", L"минут", 0, 108, 54, 55, 22, 0);
    AddControl(h, L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_CENTER, 172, 49, 76, 28, IDC_SECONDS);
    AddControl(h, L"STATIC", L"секунд", 0, 256, 54, 70, 22, 0);
    AddControl(h, L"STATIC", L"Размер цифр (28–180)", 0, 24, 95, 240, 22, 0);
    AddControl(h, L"EDIT", L"", WS_BORDER | ES_NUMBER, 24, 121, 110, 28, IDC_FONTSIZE);
    AddControl(h, L"STATIC", L"Цвет цифр", 0, 24, 166, 150, 22, 0);
    AddControl(h, L"BUTTON", L"", BS_PUSHBUTTON, 215, 158, 215, 32, IDC_NORMAL_COLOR);
    AddControl(h, L"STATIC", L"Цвет последней минуты", 0, 24, 210, 180, 22, 0);
    AddControl(h, L"BUTTON", L"", BS_PUSHBUTTON, 215, 202, 215, 32, IDC_WARNING_COLOR);
    AddControl(h, L"STATIC", L"Цвет фона", 0, 24, 254, 150, 22, 0);
    AddControl(h, L"BUTTON", L"", BS_PUSHBUTTON, 215, 246, 215, 32, IDC_BACKGROUND_COLOR);
    AddControl(h, L"BUTTON", L"Пропускать клики мыши сквозь таймер",
        BS_AUTOCHECKBOX, 24, 301, 390, 26, IDC_CLICKTHROUGH);
    AddControl(h, L"STATIC", L"Управление: двойной щелчок — старт/пауза; перетаскивание — позиция.\nГорячие клавиши работают даже во время показа презентации.",
        0, 24, 340, 406, 42, 0);
    AddControl(h, L"BUTTON", L"Отмена", BS_PUSHBUTTON, 210, 400, 100, 34, IDC_CANCEL);
    AddControl(h, L"BUTTON", L"Сохранить", BS_DEFPUSHBUTTON, 322, 400, 108, 34, IDC_SAVE);
    SetNumber(h, IDC_MINUTES, g_edit_settings.duration_seconds / 60);
    SetNumber(h, IDC_SECONDS, g_edit_settings.duration_seconds % 60);
    SetNumber(h, IDC_FONTSIZE, g_edit_settings.font_size);
    CheckDlgButton(h, IDC_CLICKTHROUGH, g_edit_settings.click_through ? BST_CHECKED : BST_UNCHECKED);
    ColorButtonText(h, IDC_NORMAL_COLOR, g_edit_settings.normal_color);
    ColorButtonText(h, IDC_WARNING_COLOR, g_edit_settings.warning_color);
    ColorButtonText(h, IDC_BACKGROUND_COLOR, g_edit_settings.background_color);
}

static void CloseSettings(HWND h) {
    DestroyWindow(h);
    g_settings_window = NULL;
    EnableWindow(g_timer_window, TRUE);
    ShowWindow(g_timer_window, SW_SHOWNOACTIVATE);
}

static void SaveSettingsDialog(HWND h) {
    BOOL ok1, ok2, ok3;
    int min = GetNumber(h, IDC_MINUTES, &ok1);
    int sec = GetNumber(h, IDC_SECONDS, &ok2);
    int size = GetNumber(h, IDC_FONTSIZE, &ok3);
    if (!ok1 || !ok2 || !ok3 || min < 0 || min > 999 || sec < 0 || sec > 59 ||
        min * 60 + sec < 1 || size < 28 || size > 180) {
        MessageBoxW(h, L"Проверьте значения: время — от 1 секунды до 999:59, размер — от 28 до 180.",
            L"Некорректные настройки", MB_OK | MB_ICONWARNING);
        return;
    }
    g_edit_settings.duration_seconds = min * 60 + sec;
    g_edit_settings.font_size = size;
    g_edit_settings.click_through = IsDlgButtonChecked(h, IDC_CLICKTHROUGH) == BST_CHECKED;
    g_edit_settings.x = g_settings.x; g_edit_settings.y = g_settings.y;
    g_settings = g_edit_settings;
    ApplySettings(TRUE);
    CloseSettings(h);
}

static LRESULT CALLBACK SettingsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: CreateSettingsControls(h); return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_NORMAL_COLOR:
                    if (PickColor(h, &g_edit_settings.normal_color)) ColorButtonText(h, IDC_NORMAL_COLOR, g_edit_settings.normal_color); return 0;
                case IDC_WARNING_COLOR:
                    if (PickColor(h, &g_edit_settings.warning_color)) ColorButtonText(h, IDC_WARNING_COLOR, g_edit_settings.warning_color); return 0;
                case IDC_BACKGROUND_COLOR:
                    if (PickColor(h, &g_edit_settings.background_color)) ColorButtonText(h, IDC_BACKGROUND_COLOR, g_edit_settings.background_color); return 0;
                case IDC_SAVE: SaveSettingsDialog(h); return 0;
                case IDC_CANCEL: CloseSettings(h); return 0;
            } break;
        case WM_CLOSE: CloseSettings(h); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void PaintTimer(HWND h) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT r; GetClientRect(h, &r);
    HBRUSH background = CreateSolidBrush(g_settings.background_color);
    FillRect(dc, &r, background); DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    ULONGLONG ms = Remaining();
    ULONGLONG shown = (ms + 999) / 1000;
    int minutes = (int)(shown / 60), seconds = (int)(shown % 60);
    WCHAR text[32]; wsprintfW(text, L"%02d:%02d", minutes, seconds);
    SetTextColor(dc, shown <= 60 ? g_settings.warning_color : g_settings.normal_color);
    HFONT old = (HFONT)SelectObject(dc, g_timer_font);
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old); EndPaint(h, &ps);
}

static void PositionInitially(HWND h) {
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    RECT r; GetWindowRect(h, &r);
    int x = g_settings.x, y = g_settings.y;
    if (x == CW_USEDEFAULT || y == CW_USEDEFAULT) { x = wa.right - (r.right-r.left) - 30; y = wa.top + 30; }
    SetWindowPos(h, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static LRESULT CALLBACK TimerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_timer_window = h; g_remaining_ms = (ULONGLONG)g_settings.duration_seconds * 1000;
            ResizeOverlay(); PositionInitially(h); ApplyClickThrough(); AddTrayIcon();
            RegisterHotKey(h, HK_TOGGLE, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE);
            RegisterHotKey(h, HK_RESET, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'R');
            RegisterHotKey(h, HK_SETTINGS, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'S');
            RegisterHotKey(h, HK_ADD, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_UP);
            RegisterHotKey(h, HK_SUB, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_DOWN);
            return 0;
        case WM_PAINT: PaintTimer(h); return 0;
        case WM_TIMER:
            if (Remaining() == 0 && g_running) {
                g_running = FALSE; g_remaining_ms = 0; KillTimer(h, TIMER_ID);
                MessageBeep(MB_ICONEXCLAMATION);
            }
            InvalidateRect(h, NULL, FALSE); return 0;
        case WM_HOTKEY:
            if (wp == HK_TOGGLE) ToggleTimer(); else if (wp == HK_RESET) ResetTimer();
            else if (wp == HK_SETTINGS) OpenSettings(); else if (wp == HK_ADD) ChangeRemaining(60);
            else if (wp == HK_SUB) ChangeRemaining(-60); return 0;
        case WM_LBUTTONDBLCLK: ToggleTimer(); return 0;
        case WM_LBUTTONDOWN:
            if (!g_settings.click_through) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
            return 0;
        case WM_RBUTTONUP: OpenSettings(); return 0;
        case WM_EXITSIZEMOVE: {
            RECT r; GetWindowRect(h, &r); g_settings.x = r.left; g_settings.y = r.top; SaveSettings(); return 0;
        }
        case WM_TRAY:
            if (LOWORD(lp) == WM_CONTEXTMENU || LOWORD(lp) == WM_RBUTTONUP) ShowTrayMenu();
            else if (LOWORD(lp) == WM_LBUTTONDBLCLK) OpenSettings(); return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDM_TOGGLE) ToggleTimer(); else if (LOWORD(wp) == IDM_RESET) ResetTimer();
            else if (LOWORD(wp) == IDM_SETTINGS) OpenSettings();
            else if (LOWORD(wp) == IDM_SHOW) ShowWindow(h, IsWindowVisible(h) ? SW_HIDE : SW_SHOWNOACTIVATE);
            else if (LOWORD(wp) == IDM_EXIT) { g_allow_exit = TRUE; DestroyWindow(h); }
            return 0;
        case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
        case WM_DESTROY:
            if (g_allow_exit) {
                Shell_NotifyIconW(NIM_DELETE, &g_nid);
                for (int i = HK_TOGGLE; i <= HK_SUB; i++) UnregisterHotKey(h, i);
                if (g_timer_font) DeleteObject(g_timer_font);
                PostQuitMessage(0);
            } return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev; (void)cmd; (void)show;
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"PresentationTimer.SingleInstance.2026");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"Таймер уже запущен. Проверьте значок рядом с часами.", APP_NAME, MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex); return 0;
    }
    g_instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    LoadSettings();
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance; wc.lpfnWndProc = TimerProc; wc.lpszClassName = L"PresentationTimerOverlay";
    wc.hCursor = LoadCursorW(NULL, IDC_SIZEALL); wc.hIcon = LoadIconW(NULL, IDI_INFORMATION);
    wc.style = CS_DBLCLKS; RegisterClassExW(&wc);
    wc.lpfnWndProc = SettingsProc; wc.lpszClassName = L"PresentationTimerSettings";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.style = 0; RegisterClassExW(&wc);
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"PresentationTimerOverlay", APP_NAME, WS_POPUP,
        0, 0, 340, 130, NULL, NULL, instance, NULL);
    if (!window) { CloseHandle(mutex); return 1; }
    ShowWindow(window, SW_SHOWNOACTIVATE); UpdateWindow(window);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!g_settings_window || !IsDialogMessageW(g_settings_window, &msg)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
    }
    CloseHandle(mutex); return (int)msg.wParam;
}
