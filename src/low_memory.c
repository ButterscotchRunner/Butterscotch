#include "utils.h"

#define MAX_CALLBACKS 64

typedef struct {
    bool (*callback)(void* context);
    void* context;
} LowMemoryAlarmCallback;

LowMemoryAlarmCallback lowMemoryAlarmCallbacks[MAX_CALLBACKS];
int lowMemoryAlarmCallbackCount = 0;

// Call when memory is running low.  This triggers low memory alarm callback
// functions which evict caches from memory to free it up.
bool lowMemoryAlarm()
{
    bool didSomething = false;
    for (int i = 0; i < lowMemoryAlarmCallbackCount; i++) {
        LowMemoryAlarmCallback* cb = &lowMemoryAlarmCallbacks[i];
        bool result = cb->callback(cb->context);
        if (result)
            didSomething = true;
    }
    return didSomething;
}

// Registers a low memory alarm callback.  When a low memory alarm is triggered,
// the function provided will be called, and it should free memory if possible.
void registerLowMemoryAlarmCallback(bool(*callbackFunction)(void*), void* context)
{
    if (lowMemoryAlarmCallbackCount >= MAX_CALLBACKS) {
        fprintf(stderr, "registerLowMemoryAlarmCallback: Cannot register another callback, %d already registered.\n", lowMemoryAlarmCallbackCount);
        return;
    }

    LowMemoryAlarmCallback cb;
    cb.callback = callbackFunction;
    cb.context = context;

    lowMemoryAlarmCallbacks[lowMemoryAlarmCallbackCount++] = cb;
}
