#ifndef _BS_OVERLAY_H_
#define _BS_OVERLAY_H_

#include "runner.h"

typedef enum {
    OVERLAY_STATS_ENABLED = 0,
    OVERLAY_STATS_ENABLED_WITH_PROFILER = 1,
    OVERLAY_STATS_DISABLED = 2,
    OVERLAY_STATS_MAX
} OverlayState;

void Overlay_init(OverlayState initialState);
void Overlay_deinit(void);
OverlayState Overlay_getState(void);
void Overlay_toggle(Runner* runner);
void Overlay_draw(Runner* runner, uint32_t fps, int32_t fbWidth, int32_t fbHeight, size_t memBytes);

#endif
