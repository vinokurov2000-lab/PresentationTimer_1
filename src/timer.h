#ifndef PRESENTATION_TIMER_TIMER_H
#define PRESENTATION_TIMER_TIMER_H

#include "app.h"

typedef struct {
    ULONGLONG duration_ms;
    ULONGLONG remaining_ms;
    ULONGLONG started_at;
    BOOL running;
    BOOL expired;
    LONGLONG visual_token;
} TimerState;

void TimerInit(TimerState *timer, int duration_seconds);
void TimerReset(TimerState *timer, int duration_seconds);
void TimerToggle(TimerState *timer);
void TimerChange(TimerState *timer, int delta_seconds);
BOOL TimerTick(TimerState *timer);
ULONGLONG TimerRemaining(const TimerState *timer);
ULONGLONG TimerShownSeconds(const TimerState *timer);
BOOL TimerVisualChanged(TimerState *timer);

#endif
