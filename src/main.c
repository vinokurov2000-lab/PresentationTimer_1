#include "app.h"
#include "platform.h"
#include "renderer.h"
#include "settings.h"
#include "timer.h"
#include <commdlg.h>
#include <stdlib.h>

static HINSTANCE g_instance;
static HWND g_timer_window;
static HWND g_settings_window;
static HWND g_help_window;
static Settings g_settings;
static Settings g_edit_settings;
static TimerState g_timer;
static TrayIcon g_tray;
static BOOL g_allow_exit;
static BOOL g_restore_timer_visibility;
static unsigned int g_hotkey_failures;

static LRESULT CALLBACK TimerProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK SettingsProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK HelpProc(HWND, UINT, WPARAM, LPARAM);

static int ScaleFor(HWND window, int value) {
    UINT dpi = GetDpiForWindow(window);
    return MulDiv(value, dpi ? (int)dpi : 96, 96);
}

static void SetControlFont(HWND parent, int id) {
    HWND control = GetDlgItem(parent, id);
    if (control) SendMessageW(control, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

static HWND AddControl(HWND parent, LPCWSTR cls, LPCWSTR text, DWORD style,
                       int x, int y, int width, int height, int id) {
    HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        ScaleFor(parent, x), ScaleFor(parent, y), ScaleFor(parent, width), ScaleFor(parent, height),
        parent, (HMENU)(INT_PTR)id, g_instance, NULL);
    if (id) SetControlFont(parent, id);
    else SendMessageW(control, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return control;
}

static void SetNumber(HWND window, int id, int value) {
    WCHAR text[24]; wsprintfW(text, L"%d", value); SetDlgItemTextW(window, id, text);
}

static int GetNumber(HWND window, int id, BOOL *ok) {
    WCHAR text[32]; GetDlgItemTextW(window, id, text, ARRAYSIZE(text));
    WCHAR *end = NULL; long value = wcstol(text, &end, 10);
    *ok = end != text && *end == 0;
    return (int)value;
}

static void ColorButtonText(HWND window, int id, COLORREF color) {
    WCHAR text[40];
    wsprintfW(text, L"Выбрать…   #%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    SetDlgItemTextW(window, id, text);
}

static BOOL PickColor(HWND owner, COLORREF *color) {
    static COLORREF custom[16];
    CHOOSECOLORW picker;
    ZeroMemory(&picker, sizeof(picker));
    picker.lStructSize = sizeof(picker); picker.hwndOwner = owner;
    picker.rgbResult = *color; picker.lpCustColors = custom;
    picker.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&picker)) return FALSE;
    *color = picker.rgbResult; return TRUE;
}

static void ApplyClickThrough(void) {
    LONG_PTR style = GetWindowLongPtrW(g_timer_window, GWL_EXSTYLE);
    if (g_settings.click_through) style |= WS_EX_TRANSPARENT;
    else style &= ~((LONG_PTR)WS_EX_TRANSPARENT);
    SetWindowLongPtrW(g_timer_window, GWL_EXSTYLE, style);
}

static void EnsureTimerTicking(void) {
    if (g_timer.running || g_timer.expired) SetTimer(g_timer_window, TIMER_ID, 100, NULL);
    else KillTimer(g_timer_window, TIMER_ID);
}

static void RenderTimer(BOOL force) {
    if (force) g_timer.visual_token = -1;
    if (!force && !TimerVisualChanged(&g_timer)) return;
    if (force) TimerVisualChanged(&g_timer);
    ULONGLONG remaining = TimerRemaining(&g_timer);
    BOOL blink_white = ((GetTickCount64() / 500) & 1ULL) != 0;
    RendererDraw(g_timer_window, &g_settings, TimerShownSeconds(&g_timer), remaining,
                 g_timer.duration_ms, g_timer.expired, blink_white);
}

static void ResetTimer(void) {
    TimerReset(&g_timer, g_settings.duration_seconds);
    EnsureTimerTicking(); RenderTimer(TRUE);
}

static void ToggleTimer(void) {
    TimerToggle(&g_timer); EnsureTimerTicking(); RenderTimer(TRUE);
}

static void ChangeRemaining(int seconds) {
    TimerChange(&g_timer, seconds); EnsureTimerTicking(); RenderTimer(TRUE);
}

static void ToggleVisibility(void) {
    g_settings.visible = !IsWindowVisible(g_timer_window);
    ShowWindow(g_timer_window, g_settings.visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    SettingsSave(&g_settings);
    if (g_settings.visible) RenderTimer(TRUE);
}

static void CaptureWindowPosition(void) {
    RECT rect;
    GetWindowRect(g_timer_window, &rect);
    g_settings.x = rect.left;
    g_settings.y = rect.top;
}

static void ApplyVisualSettings(void) {
    ApplyClickThrough();
    RendererApplySettings(g_timer_window, &g_settings);
    PlatformClampWindow(g_timer_window);
    CaptureWindowPosition();
    RenderTimer(TRUE);
}

static void ShowTrayMenu(void) {
    POINT point; GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TOGGLE, g_timer.running ? L"Пауза" : L"Старт");
    AppendMenuW(menu, MF_STRING, IDM_RESET, L"Сбросить");
    AppendMenuW(menu, MF_STRING, IDM_SHOW, IsWindowVisible(g_timer_window) ? L"Скрыть таймер" : L"Показать таймер");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Настройки…");
    AppendMenuW(menu, MF_STRING, IDM_HELP, L"Справка и горячие клавиши");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Выход");
    SetForegroundWindow(g_timer_window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, g_timer_window, NULL);
    DestroyMenu(menu);
}

static void CreateSettingsControls(HWND window) {
    AddControl(window, L"STATIC", L"Время обратного отсчёта", 0, 24, 18, 250, 22, 0);
    AddControl(window, L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_CENTER, 24, 43, 76, 28, IDC_MINUTES);
    AddControl(window, L"STATIC", L"минут", 0, 108, 48, 55, 22, 0);
    AddControl(window, L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_CENTER, 170, 43, 76, 28, IDC_SECONDS);
    AddControl(window, L"STATIC", L"секунд", 0, 254, 48, 70, 22, 0);

    AddControl(window, L"STATIC", L"Тема оформления", 0, 24, 88, 180, 22, 0);
    AddControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 214, 80, 290, 120, IDC_THEME);
    AddControl(window, L"STATIC", L"Размер цифр (28–180)", 0, 24, 130, 200, 22, 0);
    AddControl(window, L"EDIT", L"", WS_BORDER | ES_NUMBER, 394, 122, 110, 28, IDC_FONTSIZE);

    AddControl(window, L"STATIC", L"Обычный цвет цифр", 0, 24, 172, 180, 22, 0);
    AddControl(window, L"BUTTON", L"", BS_PUSHBUTTON, 214, 164, 290, 32, IDC_NORMAL_COLOR);
    AddControl(window, L"STATIC", L"Цвет последней минуты", 0, 24, 214, 185, 22, 0);
    AddControl(window, L"BUTTON", L"", BS_PUSHBUTTON, 214, 206, 290, 32, IDC_WARNING_COLOR);
    AddControl(window, L"STATIC", L"Цвет фона", 0, 24, 256, 160, 22, 0);
    AddControl(window, L"BUTTON", L"", BS_PUSHBUTTON, 214, 248, 290, 32, IDC_BACKGROUND_COLOR);

    AddControl(window, L"STATIC", L"Прозрачность фона (0–100%)", 0, 24, 298, 260, 22, 0);
    AddControl(window, L"EDIT", L"", WS_BORDER | ES_NUMBER, 394, 290, 110, 28, IDC_TRANSPARENCY);
    AddControl(window, L"STATIC", L"Сигнал по окончании", 0, 24, 340, 180, 22, 0);
    AddControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 214, 332, 178, 120, IDC_SOUND);
    AddControl(window, L"BUTTON", L"Прослушать", BS_PUSHBUTTON, 400, 332, 104, 30, IDC_PREVIEW_SOUND);

    AddControl(window, L"BUTTON", L"Пропускать клики мыши сквозь таймер",
               BS_AUTOCHECKBOX, 24, 384, 420, 26, IDC_CLICKTHROUGH);
    AddControl(window, L"STATIC",
        L"Горячие клавиши работают во всех программах. Если сочетание занято,\nтаймер покажет предупреждение при запуске.",
        0, 24, 425, 480, 42, 0);
    AddControl(window, L"BUTTON", L"Открыть наглядную справку  ·  Ctrl+Alt+F1",
               BS_PUSHBUTTON, 24, 480, 480, 36, IDC_OPEN_HELP);
    AddControl(window, L"BUTTON", L"Отмена", BS_PUSHBUTTON, 280, 555, 104, 36, IDC_CANCEL);
    AddControl(window, L"BUTTON", L"Сохранить", BS_DEFPUSHBUTTON, 396, 555, 108, 36, IDC_SAVE);

    SetNumber(window, IDC_MINUTES, g_edit_settings.duration_seconds / 60);
    SetNumber(window, IDC_SECONDS, g_edit_settings.duration_seconds % 60);
    SetNumber(window, IDC_FONTSIZE, g_edit_settings.font_size);
    SetNumber(window, IDC_TRANSPARENCY, g_edit_settings.background_transparency);
    SendDlgItemMessageW(window, IDC_THEME, CB_ADDSTRING, 0, (LPARAM)L"Умное стекло");
    SendDlgItemMessageW(window, IDC_THEME, CB_ADDSTRING, 0, (LPARAM)L"Эфирный минимализм");
    SendDlgItemMessageW(window, IDC_THEME, CB_ADDSTRING, 0, (LPARAM)L"Кольцо прогресса");
    SendDlgItemMessageW(window, IDC_THEME, CB_SETCURSEL, g_edit_settings.theme, 0);
    SendDlgItemMessageW(window, IDC_SOUND, CB_ADDSTRING, 0, (LPARAM)L"Мягкий сигнал");
    SendDlgItemMessageW(window, IDC_SOUND, CB_ADDSTRING, 0, (LPARAM)L"Двойной сигнал");
    SendDlgItemMessageW(window, IDC_SOUND, CB_ADDSTRING, 0, (LPARAM)L"Тройной сигнал");
    SendDlgItemMessageW(window, IDC_SOUND, CB_SETCURSEL, g_edit_settings.end_sound - 1, 0);
    CheckDlgButton(window, IDC_CLICKTHROUGH, g_edit_settings.click_through ? BST_CHECKED : BST_UNCHECKED);
    ColorButtonText(window, IDC_NORMAL_COLOR, g_edit_settings.normal_color);
    ColorButtonText(window, IDC_WARNING_COLOR, g_edit_settings.warning_color);
    ColorButtonText(window, IDC_BACKGROUND_COLOR, g_edit_settings.background_color);
}

static void OpenSettings(void) {
    if (g_settings_window) { SetForegroundWindow(g_settings_window); return; }
    g_edit_settings = g_settings;
    g_restore_timer_visibility = IsWindowVisible(g_timer_window);
    if (g_restore_timer_visibility) EnableWindow(g_timer_window, FALSE);
    int width = MulDiv(550, (int)GetDpiForSystem(), 96);
    int height = MulDiv(640, (int)GetDpiForSystem(), 96);
    g_settings_window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"PresentationTimerSettings", L"Настройки таймера",
        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        g_timer_window, NULL, g_instance, NULL);
    if (g_settings_window) {
        ShowWindow(g_settings_window, SW_SHOW); UpdateWindow(g_settings_window);
    }
}

static void CloseSettings(HWND window) {
    DestroyWindow(window);
    g_settings_window = NULL;
    if (g_restore_timer_visibility) {
        EnableWindow(g_timer_window, TRUE);
        ShowWindow(g_timer_window, SW_SHOWNOACTIVATE);
    }
}

static void SaveSettingsDialog(HWND window) {
    BOOL ok1, ok2, ok3, ok4;
    int minutes = GetNumber(window, IDC_MINUTES, &ok1);
    int seconds = GetNumber(window, IDC_SECONDS, &ok2);
    int font_size = GetNumber(window, IDC_FONTSIZE, &ok3);
    int transparency = GetNumber(window, IDC_TRANSPARENCY, &ok4);
    if (!ok1 || !ok2 || !ok3 || !ok4 || minutes < 0 || minutes > 999 ||
        seconds < 0 || seconds > 59 || minutes * 60 + seconds < 1 ||
        font_size < 28 || font_size > 180 || transparency < 0 || transparency > 100) {
        MessageBoxW(window,
            L"Проверьте значения: время — от 1 секунды до 999:59, размер цифр — от 28 до 180, прозрачность — от 0 до 100%.",
            L"Некорректные настройки", MB_OK | MB_ICONWARNING);
        return;
    }
    int old_duration = g_settings.duration_seconds;
    g_edit_settings.duration_seconds = minutes * 60 + seconds;
    g_edit_settings.font_size = font_size;
    g_edit_settings.background_transparency = transparency;
    int theme = (int)SendDlgItemMessageW(window, IDC_THEME, CB_GETCURSEL, 0, 0);
    int sound = (int)SendDlgItemMessageW(window, IDC_SOUND, CB_GETCURSEL, 0, 0);
    g_edit_settings.theme = theme >= 0 ? (TimerTheme)theme : THEME_GLASS;
    g_edit_settings.end_sound = sound >= 0 ? sound + 1 : 2;
    g_edit_settings.click_through = IsDlgButtonChecked(window, IDC_CLICKTHROUGH) == BST_CHECKED;
    g_edit_settings.x = g_settings.x; g_edit_settings.y = g_settings.y;
    g_edit_settings.visible = g_settings.visible;
    g_settings = g_edit_settings;

    ApplyVisualSettings();
    if (old_duration != g_settings.duration_seconds) ResetTimer();
    if (!SettingsSave(&g_settings)) {
        MessageBoxW(window, L"Не удалось сохранить настройки. Проверьте доступ к папке AppData.",
                    APP_NAME, MB_OK | MB_ICONWARNING);
    }
    CloseSettings(window);
}

static void DrawHelpCard(HDC dc, RECT rect, LPCWSTR title, LPCWSTR keys, LPCWSTR details, UINT dpi) {
    HBRUSH brush = CreateSolidBrush(RGB(246, 248, 252));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(220, 226, 236));
    HGDIOBJ old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, MulDiv(16, dpi, 96), MulDiv(16, dpi, 96));
    SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(brush); DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);
    HFONT heading = CreateFontW(-MulDiv(12, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT body = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    RECT text = rect; int pad = MulDiv(18, dpi, 96);
    text.left += pad; text.right -= pad; text.top += MulDiv(14, dpi, 96);
    HGDIOBJ old_font = SelectObject(dc, heading); SetTextColor(dc, RGB(25, 35, 52));
    DrawTextW(dc, title, -1, &text, DT_LEFT | DT_SINGLELINE);
    text.top += MulDiv(27, dpi, 96); SelectObject(dc, body); SetTextColor(dc, RGB(33, 95, 190));
    DrawTextW(dc, keys, -1, &text, DT_LEFT | DT_WORDBREAK);
    text.top += MulDiv(48, dpi, 96); SetTextColor(dc, RGB(77, 88, 105));
    DrawTextW(dc, details, -1, &text, DT_LEFT | DT_WORDBREAK);
    SelectObject(dc, old_font); DeleteObject(heading); DeleteObject(body);
}

static void PaintHelp(HWND window) {
    PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint);
    RECT client; GetClientRect(window, &client);
    FillRect(dc, &client, (HBRUSH)(COLOR_WINDOW + 1));
    UINT dpi = GetDpiForWindow(window); if (!dpi) dpi = 96;
    int margin = MulDiv(24, dpi, 96), gap = MulDiv(14, dpi, 96);
    HFONT title = CreateFontW(-MulDiv(20, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT subtitle = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, title); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(20, 29, 45));
    RECT line = {margin, margin, client.right - margin, margin + MulDiv(38, dpi, 96)};
    DrawTextW(dc, L"Как управлять таймером", -1, &line, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, subtitle); SetTextColor(dc, RGB(91, 101, 117));
    line.top += MulDiv(38, dpi, 96); line.bottom += MulDiv(58, dpi, 96);
    DrawTextW(dc, L"Все команды работают поверх PowerPoint, браузера и PDF-презентаций.", -1, &line, DT_LEFT | DT_SINGLELINE);

    int top = margin + MulDiv(82, dpi, 96);
    int card_width = (client.right - margin * 2 - gap) / 2;
    int card_height = MulDiv(160, dpi, 96);
    RECT first = {margin, top, margin + card_width, top + card_height};
    RECT second = {first.right + gap, top, client.right - margin, top + card_height};
    RECT third = {margin, first.bottom + gap, client.right - margin, first.bottom + gap + MulDiv(150, dpi, 96)};
    DrawHelpCard(dc, first, L"Отсчёт",
        L"Ctrl+Alt+Пробел   Старт / пауза\nCtrl+Alt+R              Сброс",
        L"Ctrl+Alt+↑ или ↓ добавляет или убирает одну минуту.", dpi);
    DrawHelpCard(dc, second, L"Окно таймера",
        L"Ctrl+Alt+H              Скрыть / показать\nCtrl+Alt+S              Настройки",
        L"Перетащите таймер мышью. Двойной щелчок запускает или ставит отсчёт на паузу.", dpi);
    DrawHelpCard(dc, third, L"Во время выступления",
        L"Обычный цвет → последняя минута → мигающее 00:00",
        L"Правый щелчок открывает настройки. В режиме пропуска кликов используйте горячие клавиши или значок рядом с часами. Справка: Ctrl+Alt+F1.", dpi);
    SelectObject(dc, old); DeleteObject(title); DeleteObject(subtitle);
    EndPaint(window, &paint);
}

static void OpenHelp(void) {
    if (g_help_window) { SetForegroundWindow(g_help_window); return; }
    int width = MulDiv(760, (int)GetDpiForSystem(), 96);
    int height = MulDiv(570, (int)GetDpiForSystem(), 96);
    g_help_window = CreateWindowExW(WS_EX_TOPMOST, L"PresentationTimerHelp",
        L"Справка — Таймер презентации", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, g_instance, NULL);
    if (g_help_window) { ShowWindow(g_help_window, SW_SHOW); UpdateWindow(g_help_window); }
}

static LRESULT CALLBACK SettingsProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    (void)lp;
    switch (message) {
        case WM_CREATE: CreateSettingsControls(window); return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_NORMAL_COLOR:
                    if (PickColor(window, &g_edit_settings.normal_color)) ColorButtonText(window, IDC_NORMAL_COLOR, g_edit_settings.normal_color); return 0;
                case IDC_WARNING_COLOR:
                    if (PickColor(window, &g_edit_settings.warning_color)) ColorButtonText(window, IDC_WARNING_COLOR, g_edit_settings.warning_color); return 0;
                case IDC_BACKGROUND_COLOR:
                    if (PickColor(window, &g_edit_settings.background_color)) ColorButtonText(window, IDC_BACKGROUND_COLOR, g_edit_settings.background_color); return 0;
                case IDC_PREVIEW_SOUND: {
                    int sound = (int)SendDlgItemMessageW(window, IDC_SOUND, CB_GETCURSEL, 0, 0);
                    PlatformPlaySound(sound >= 0 ? sound + 1 : 2); return 0;
                }
                case IDC_OPEN_HELP: OpenHelp(); return 0;
                case IDC_SAVE: SaveSettingsDialog(window); return 0;
                case IDC_CANCEL: CloseSettings(window); return 0;
            } break;
        case WM_DPICHANGED: {
            RECT *suggested = (RECT *)lp;
            SetWindowPos(window, NULL, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE); return 0;
        }
        case WM_CLOSE: CloseSettings(window); return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

static LRESULT CALLBACK HelpProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_PAINT: PaintHelp(window); return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE || wp == VK_RETURN) { DestroyWindow(window); return 0; }
            break;
        case WM_DPICHANGED: {
            RECT *suggested = (RECT *)lp;
            SetWindowPos(window, NULL, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE); return 0;
        }
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_DESTROY: g_help_window = NULL; return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

static LRESULT CALLBACK TimerProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_CREATE: {
            g_timer_window = window;
            TimerInit(&g_timer, g_settings.duration_seconds);
            if (!RendererInitialize(window, &g_settings)) return -1;
            PlatformPositionInitially(window, g_settings.x, g_settings.y);
            ApplyClickThrough();
            HICON icon = LoadIconW(g_instance, MAKEINTRESOURCEW(APP_ICON));
            if (!icon) icon = LoadIconW(NULL, IDI_APPLICATION);
            PlatformAddTrayIcon(&g_tray, window, icon);
            g_hotkey_failures = PlatformRegisterHotkeys(window);
            if (g_hotkey_failures) PostMessageW(window, WM_HOTKEY_WARNING, 0, 0);
            RenderTimer(TRUE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint; BeginPaint(window, &paint); EndPaint(window, &paint);
            RenderTimer(TRUE); return 0;
        }
        case WM_TIMER:
            if (TimerTick(&g_timer)) PlatformPlaySound(g_settings.end_sound);
            if (TimerVisualChanged(&g_timer)) RenderTimer(TRUE);
            EnsureTimerTicking(); return 0;
        case WM_HOTKEY:
            if (wp == HK_TOGGLE) ToggleTimer();
            else if (wp == HK_RESET) ResetTimer();
            else if (wp == HK_SETTINGS) OpenSettings();
            else if (wp == HK_ADD) ChangeRemaining(60);
            else if (wp == HK_SUB) ChangeRemaining(-60);
            else if (wp == HK_SHOW) ToggleVisibility();
            else if (wp == HK_HELP) OpenHelp();
            return 0;
        case WM_HOTKEY_WARNING: {
            WCHAR details[1200]; PlatformFormatHotkeyConflicts(g_hotkey_failures, details, ARRAYSIZE(details));
            MessageBoxW(NULL, details, L"Конфликт горячих клавиш", MB_OK | MB_ICONWARNING); return 0;
        }
        case WM_LBUTTONDBLCLK: ToggleTimer(); return 0;
        case WM_LBUTTONDOWN:
            if (!g_settings.click_through) { ReleaseCapture(); SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
            return 0;
        case WM_RBUTTONUP: OpenSettings(); return 0;
        case WM_EXITSIZEMOVE: {
            CaptureWindowPosition(); SettingsSave(&g_settings); return 0;
        }
        case WM_DPICHANGED: {
            RECT *suggested = (RECT *)lp;
            SetWindowPos(window, HWND_TOPMOST, suggested->left, suggested->top, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);
            RendererApplySettings(window, &g_settings); PlatformClampWindow(window);
            CaptureWindowPosition(); SettingsSave(&g_settings); RenderTimer(TRUE); return 0;
        }
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            PlatformClampWindow(window); CaptureWindowPosition(); SettingsSave(&g_settings);
            RenderTimer(TRUE); return 0;
        case WM_TRAY:
            if (LOWORD(lp) == WM_CONTEXTMENU || LOWORD(lp) == WM_RBUTTONUP) ShowTrayMenu();
            else if (LOWORD(lp) == WM_LBUTTONDBLCLK) OpenSettings();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDM_TOGGLE) ToggleTimer();
            else if (LOWORD(wp) == IDM_RESET) ResetTimer();
            else if (LOWORD(wp) == IDM_SETTINGS) OpenSettings();
            else if (LOWORD(wp) == IDM_SHOW) ToggleVisibility();
            else if (LOWORD(wp) == IDM_HELP) OpenHelp();
            else if (LOWORD(wp) == IDM_EXIT) { g_allow_exit = TRUE; DestroyWindow(window); }
            return 0;
        case WM_CLOSE:
            if (IsWindowVisible(window)) ToggleVisibility();
            return 0;
        case WM_DESTROY:
            if (g_allow_exit) {
                PlatformRemoveTrayIcon(&g_tray); PlatformUnregisterHotkeys(window);
                RendererShutdown(); PostQuitMessage(0);
            }
            return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    (void)previous; (void)command; (void)show;
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"PresentationTimer.SingleInstance.2026");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"Таймер уже запущен. Проверьте значок рядом с часами.",
                    APP_NAME, MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex); return 0;
    }
    g_instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SettingsLoad(&g_settings);

    HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(APP_ICON));
    if (!icon) icon = LoadIconW(NULL, IDI_APPLICATION);
    WNDCLASSEXW cls;
    ZeroMemory(&cls, sizeof(cls)); cls.cbSize = sizeof(cls); cls.hInstance = instance;
    cls.hIcon = icon; cls.hIconSm = icon;
    cls.lpfnWndProc = TimerProc; cls.lpszClassName = L"PresentationTimerOverlay";
    cls.hCursor = LoadCursorW(NULL, IDC_SIZEALL); cls.style = CS_DBLCLKS;
    if (!RegisterClassExW(&cls)) { CloseHandle(mutex); return 1; }
    cls.lpfnWndProc = SettingsProc; cls.lpszClassName = L"PresentationTimerSettings";
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW); cls.style = 0;
    if (!RegisterClassExW(&cls)) { CloseHandle(mutex); return 1; }
    cls.lpfnWndProc = HelpProc; cls.lpszClassName = L"PresentationTimerHelp";
    if (!RegisterClassExW(&cls)) { CloseHandle(mutex); return 1; }

    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        L"PresentationTimerOverlay", APP_NAME, WS_POPUP,
        0, 0, 340, 130, NULL, NULL, instance, NULL);
    if (!window) { CloseHandle(mutex); return 1; }
    if (g_settings.visible) ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (!g_settings_window || !IsDialogMessageW(g_settings_window, &message)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    CloseHandle(mutex); return (int)message.wParam;
}
