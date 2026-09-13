#ifndef PRESENTATION_TIMER_PLATFORM_H
#define PRESENTATION_TIMER_PLATFORM_H

#include "app.h"
#include <shellapi.h>

typedef struct {
    NOTIFYICONDATAW data;
    BOOL added;
} TrayIcon;

unsigned int PlatformRegisterHotkeys(HWND window);
void PlatformUnregisterHotkeys(HWND window);
void PlatformFormatHotkeyConflicts(unsigned int failures, WCHAR *buffer, int capacity);
void PlatformPlaySound(int sound_id);
void PlatformClampWindow(HWND window);
void PlatformPositionInitially(HWND window, int x, int y);
void PlatformAddTrayIcon(TrayIcon *tray, HWND window, HICON icon);
void PlatformRemoveTrayIcon(TrayIcon *tray);

#endif
