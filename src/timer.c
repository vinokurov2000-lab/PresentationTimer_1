#include "timer.h"

ULONGLONG TimerRemaining(const TimerState *timer) {
    if (!timer->running) return timer->remaining_ms;
    ULONGLONG elapsed = GetTickCount64() - timer->started_at;
    return elapsed >= timer->remaining_ms ? 0 : timer->remaining_ms - elapsed;
}

ULONGLONG TimerShownSeconds(const TimerState *timer) {
    ULONGLONG ms = TimerRemaining(timer);
    return (ms + 999) / 1000;
}

void TimerInit(TimerState *timer, int duration_seconds) {
    ZeroMemory(timer, sizeof(*timer));
    TimerReset(timer, duration_seconds);
}

void TimerReset(TimerState *timer, int duration_seconds) {
    timer->duration_ms = (ULONGLONG)duration_seconds * 1000;
    timer->remaining_ms = timer->duration_ms;
    timer->started_at = 0;
    timer->running = FALSE;
    timer->expired = FALSE;
    timer->visual_token = -1;
}

void TimerToggle(TimerState *timer) {
    if (timer->running) {
        timer->remaining_ms = TimerRemaining(timer);
        timer->running = FALSE;
    } else if (timer->remaining_ms > 0) {
        timer->expired = FALSE;
        timer->started_at = GetTickCount64();
        timer->running = TRUE;
    }
    timer->visual_token = -1;
}

void TimerChange(TimerState *timer, int delta_seconds) {
    LONGLONG current = (LONGLONG)TimerRemaining(timer);
    LONGLONG changed = current + (LONGLONG)delta_seconds * 1000;
    if (changed < 0) changed = 0;
    if (changed > 59999000) changed = 59999000;
    timer->remaining_ms = (ULONGLONG)changed;
    timer->expired = FALSE;
    if (timer->running) timer->started_at = GetTickCount64();
    timer->visual_token = -1;
}

BOOL TimerTick(TimerState *timer) {
    if (timer->running && TimerRemaining(timer) == 0) {
        timer->running = FALSE;
        timer->remaining_ms = 0;
        timer->expired = TRUE;
        timer->visual_token = -1;
        return TRUE;
    }
    return FALSE;
}

BOOL TimerVisualChanged(TimerState *timer) {
    LONGLONG token;
    if (timer->expired) {
        token = 100000000LL + (LONGLONG)((GetTickCount64() / 500) & 1ULL);
    } else {
        token = (LONGLONG)TimerShownSeconds(timer);
    }
    if (token == timer->visual_token) return FALSE;
    timer->visual_token = token;
    return TRUE;
}
