#include "platform.h"
#include <math.h>
#include <mmsystem.h>
#include <stdlib.h>

#define SOUND_RATE 22050
#define SOUND_MAX_SAMPLES 16000

typedef struct {
    BYTE riff[4]; DWORD size; BYTE wave[4]; BYTE fmt[4]; DWORD fmt_size;
    WORD format; WORD channels; DWORD sample_rate; DWORD byte_rate;
    WORD block_align; WORD bits; BYTE data[4]; DWORD data_size;
} WavHeader;

static BYTE g_wav[sizeof(WavHeader) + SOUND_MAX_SAMPLES * 2];

static void PutTag(BYTE target[4], const char *tag) {
    for (int i = 0; i < 4; ++i) target[i] = (BYTE)tag[i];
}

static double Envelope(int sample, int start, int length) {
    int local = sample - start;
    if (local < 0 || local >= length) return 0.0;
    double attack = local < 180 ? (double)local / 180.0 : 1.0;
    double release = (double)(length - local) / (double)length;
    return attack * release * release;
}

void PlatformPlaySound(int sound_id) {
    PlaySoundW(NULL, NULL, 0);
    int samples = sound_id == 1 ? 6200 : (sound_id == 2 ? 9000 : 12000);
    int16_t *pcm = (int16_t *)(g_wav + sizeof(WavHeader));
    const double tau = 6.28318530717958647692;
    for (int i = 0; i < samples; ++i) {
        double value = 0.0;
        if (sound_id == 1) {
            value = sin(tau * 660.0 * i / SOUND_RATE) * Envelope(i, 0, 6200);
        } else if (sound_id == 2) {
            value = sin(tau * 720.0 * i / SOUND_RATE) * Envelope(i, 0, 3600);
            value += sin(tau * 960.0 * i / SOUND_RATE) * Envelope(i, 3900, 5100);
            value *= 0.65;
        } else {
            value = sin(tau * 880.0 * i / SOUND_RATE) * Envelope(i, 0, 3000);
            value += sin(tau * 1080.0 * i / SOUND_RATE) * Envelope(i, 3500, 3200);
            value += sin(tau * 1320.0 * i / SOUND_RATE) * Envelope(i, 7200, 4800);
            value *= 0.55;
        }
        if (value > 1.0) value = 1.0;
        if (value < -1.0) value = -1.0;
        pcm[i] = (int16_t)(value * 24500.0);
    }

    WavHeader *header = (WavHeader *)g_wav;
    PutTag(header->riff, "RIFF"); PutTag(header->wave, "WAVE");
    PutTag(header->fmt, "fmt "); PutTag(header->data, "data");
    header->fmt_size = 16; header->format = 1; header->channels = 1;
    header->sample_rate = SOUND_RATE; header->bits = 16;
    header->block_align = 2; header->byte_rate = SOUND_RATE * 2;
    header->data_size = (DWORD)samples * 2;
    header->size = 36 + header->data_size;
    PlaySoundW((LPCWSTR)g_wav, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

typedef struct { int id; UINT key; LPCWSTR label; } HotkeyDef;
static const HotkeyDef g_hotkeys[] = {
    {HK_TOGGLE, VK_SPACE, L"Ctrl+Alt+Пробел — старт/пауза"},
    {HK_RESET, 'R', L"Ctrl+Alt+R — сброс"},
    {HK_SETTINGS, 'S', L"Ctrl+Alt+S — настройки"},
    {HK_ADD, VK_UP, L"Ctrl+Alt+Вверх — добавить минуту"},
    {HK_SUB, VK_DOWN, L"Ctrl+Alt+Вниз — убрать минуту"},
    {HK_SHOW, 'H', L"Ctrl+Alt+H — скрыть/показать"},
    {HK_HELP, VK_F1, L"Ctrl+Alt+F1 — справка"}
};

unsigned int PlatformRegisterHotkeys(HWND window) {
    unsigned int failures = 0;
    for (unsigned int i = 0; i < ARRAYSIZE(g_hotkeys); ++i) {
        if (!RegisterHotKey(window, g_hotkeys[i].id,
                            MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, g_hotkeys[i].key)) {
            failures |= 1U << i;
        }
    }
    return failures;
}

void PlatformUnregisterHotkeys(HWND window) {
    for (unsigned int i = 0; i < ARRAYSIZE(g_hotkeys); ++i) {
        UnregisterHotKey(window, g_hotkeys[i].id);
    }
}

void PlatformFormatHotkeyConflicts(unsigned int failures, WCHAR *buffer, int capacity) {
    lstrcpynW(buffer, L"Некоторые горячие клавиши уже заняты другой программой:\n\n", capacity);
    for (unsigned int i = 0; i < ARRAYSIZE(g_hotkeys); ++i) {
        if (failures & (1U << i)) {
            int used = lstrlenW(buffer);
            if (used < capacity - 4) {
                lstrcpynW(buffer + used, L"• ", capacity - used);
                used = lstrlenW(buffer);
                lstrcpynW(buffer + used, g_hotkeys[i].label, capacity - used);
                used = lstrlenW(buffer);
                lstrcpynW(buffer + used, L"\n", capacity - used);
            }
        }
    }
    int used = lstrlenW(buffer);
    if (used < capacity - 80) {
        lstrcpynW(buffer + used, L"\nОстальные сочетания работают. Управление также доступно через значок рядом с часами.", capacity - used);
    }
}

void PlatformClampWindow(HWND window) {
    RECT rect;
    GetWindowRect(window, &rect);
    HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info;
    ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    int x = rect.left, y = rect.top;
    if (x < info.rcWork.left) x = info.rcWork.left;
    if (y < info.rcWork.top) y = info.rcWork.top;
    if (x + width > info.rcWork.right) x = info.rcWork.right - width;
    if (y + height > info.rcWork.bottom) y = info.rcWork.bottom - height;
    SetWindowPos(window, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
}

void PlatformPositionInitially(HWND window, int x, int y) {
    RECT rect; GetWindowRect(window, &rect);
    if (x == CW_USEDEFAULT || y == CW_USEDEFAULT) {
        POINT point = {0, 0};
        HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info;
        ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
        GetMonitorInfoW(monitor, &info);
        x = info.rcWork.right - (rect.right - rect.left) - 30;
        y = info.rcWork.top + 30;
    }
    SetWindowPos(window, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
    PlatformClampWindow(window);
}

void PlatformAddTrayIcon(TrayIcon *tray, HWND window, HICON icon) {
    ZeroMemory(tray, sizeof(*tray));
    tray->data.cbSize = sizeof(tray->data);
    tray->data.hWnd = window; tray->data.uID = 1;
    tray->data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray->data.uCallbackMessage = WM_TRAY;
    tray->data.hIcon = icon;
    lstrcpyW(tray->data.szTip, APP_NAME);
    tray->added = Shell_NotifyIconW(NIM_ADD, &tray->data);
    if (tray->added) {
        tray->data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &tray->data);
    }
}

void PlatformRemoveTrayIcon(TrayIcon *tray) {
    if (tray->added) Shell_NotifyIconW(NIM_DELETE, &tray->data);
    tray->added = FALSE;
}
