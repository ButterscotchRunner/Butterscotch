#ifndef _BS_PS3_OVERLAY_H_
#define _BS_PS3_OVERLAY_H_

#include "runner.h"

typedef enum {
    STATS_ENABLED = 0,
    STATS_ENABLED_WITH_PROFILER = 1,
    STATS_DISABLED = 2,
    STATS_MAX
} DebugOverlayState;

void PS3Overlay_init(void);
void PS3Overlay_deinit(void);

DebugOverlayState PS3Overlay_getDebugOverlayState(void);
void PS3Overlay_toggleDebugOverlay(Runner* runner);
void PS3Overlay_drawDebugOverlay(const Runner* runner, float tickMs, float stepMs, float drawMs, float audioMs, int fbWidth, int fbHeight);

#endif /* _BS_PS3_OVERLAY_H_ */
