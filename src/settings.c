#include "settings.h"
#include <shlobj.h>

static WCHAR g_ini_path[MAX_PATH];

static void MakeIniPath(void) {
    WCHAR directory[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL,
                                SHGFP_TYPE_CURRENT, directory))) {
        GetCurrentDirectoryW(MAX_PATH, directory);
    }
    if (lstrlenW(directory) < MAX_PATH - 32) {
        lstrcatW(directory, L"\\PresentationTimer");
        CreateDirectoryW(directory, NULL);
        wsprintfW(g_ini_path, L"%s\\settings.ini", directory);
    } else {
        lstrcpyW(g_ini_path, L"settings.ini");
    }
}

static int ReadInt(LPCWSTR key, int fallback) {
    return GetPrivateProfileIntW(L"Timer", key, fallback, g_ini_path);
}

static BOOL WriteInt(LPCWSTR key, int value) {
    WCHAR text[32];
    wsprintfW(text, L"%d", value);
    return WritePrivateProfileStringW(L"Timer", key, text, g_ini_path);
}

void SettingsLoad(Settings *s) {
    MakeIniPath();
    s->duration_seconds = ReadInt(L"DurationSeconds", 300);
    s->font_size = ReadInt(L"FontSize", 72);
    s->normal_color = (COLORREF)ReadInt(L"NormalColor", RGB(255, 255, 255));
    s->warning_color = (COLORREF)ReadInt(L"WarningColor", RGB(255, 91, 84));
    s->background_color = (COLORREF)ReadInt(L"BackgroundColor", RGB(17, 24, 39));
    s->background_transparency = ReadInt(L"BackgroundTransparency", 12);
    s->end_sound = ReadInt(L"EndSound", 2);
    s->theme = (TimerTheme)ReadInt(L"Theme", THEME_GLASS);
    s->click_through = ReadInt(L"ClickThrough", 0) != 0;
    s->visible = ReadInt(L"Visible", 1) != 0;
    s->x = ReadInt(L"X", CW_USEDEFAULT);
    s->y = ReadInt(L"Y", CW_USEDEFAULT);

    if (s->duration_seconds < 1 || s->duration_seconds > 59999) s->duration_seconds = 300;
    if (s->font_size < 28 || s->font_size > 180) s->font_size = 72;
    if (s->background_transparency < 0 || s->background_transparency > 100) s->background_transparency = 12;
    if (s->end_sound < 1 || s->end_sound > 3) s->end_sound = 2;
    if (s->theme < THEME_GLASS || s->theme > THEME_RING) s->theme = THEME_GLASS;
}

BOOL SettingsSave(const Settings *s) {
    BOOL ok = TRUE;
    ok = WriteInt(L"DurationSeconds", s->duration_seconds) && ok;
    ok = WriteInt(L"FontSize", s->font_size) && ok;
    ok = WriteInt(L"NormalColor", (int)s->normal_color) && ok;
    ok = WriteInt(L"WarningColor", (int)s->warning_color) && ok;
    ok = WriteInt(L"BackgroundColor", (int)s->background_color) && ok;
    ok = WriteInt(L"BackgroundTransparency", s->background_transparency) && ok;
    ok = WriteInt(L"EndSound", s->end_sound) && ok;
    ok = WriteInt(L"Theme", (int)s->theme) && ok;
    ok = WriteInt(L"ClickThrough", s->click_through) && ok;
    ok = WriteInt(L"Visible", s->visible) && ok;
    ok = WriteInt(L"X", s->x) && ok;
    ok = WriteInt(L"Y", s->y) && ok;
    return ok;
}
