#ifndef PRESENTATION_TIMER_APP_H
#define PRESENTATION_TIMER_APP_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#define APP_NAME L"Таймер презентации"
#define APP_VERSION L"5.0.0"
#define APP_ICON 101

#define WM_TRAY (WM_APP + 1)
#define WM_HOTKEY_WARNING (WM_APP + 2)
#define TIMER_ID 1

#define HK_TOGGLE 101
#define HK_RESET 102
#define HK_SETTINGS 103
#define HK_ADD 104
#define HK_SUB 105
#define HK_SHOW 106
#define HK_HELP 107

#define IDM_TOGGLE 201
#define IDM_RESET 202
#define IDM_SETTINGS 203
#define IDM_SHOW 204
#define IDM_HELP 205
#define IDM_EXIT 206

#define IDC_MINUTES 301
#define IDC_SECONDS 302
#define IDC_FONTSIZE 303
#define IDC_CLICKTHROUGH 304
#define IDC_NORMAL_COLOR 305
#define IDC_WARNING_COLOR 306
#define IDC_BACKGROUND_COLOR 307
#define IDC_SAVE 308
#define IDC_CANCEL 309
#define IDC_TRANSPARENCY 310
#define IDC_SOUND 311
#define IDC_THEME 312
#define IDC_PREVIEW_SOUND 313
#define IDC_OPEN_HELP 314

typedef enum {
    THEME_GLASS = 0,
    THEME_MINIMAL = 1,
    THEME_RING = 2
} TimerTheme;

typedef struct {
    int duration_seconds;
    int font_size;
    COLORREF normal_color;
    COLORREF warning_color;
    COLORREF background_color;
    int background_transparency;
    int end_sound;
    TimerTheme theme;
    BOOL click_through;
    BOOL visible;
    int x;
    int y;
} Settings;

#endif
