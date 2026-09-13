#ifndef PRESENTATION_TIMER_RENDERER_H
#define PRESENTATION_TIMER_RENDERER_H

#include "app.h"

BOOL RendererInitialize(HWND window, const Settings *settings);
BOOL RendererApplySettings(HWND window, const Settings *settings);
void RendererDraw(HWND window, const Settings *settings, ULONGLONG shown_seconds,
                  ULONGLONG remaining_ms, ULONGLONG duration_ms,
                  BOOL expired, BOOL blink_white);
void RendererShutdown(void);

#endif
