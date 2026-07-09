#include "utils.h"

// Call when memory is running low.  This triggers low memory alarm callback
// functions which evict caches from memory to free it up.
bool lowMemoryAlarm()
{
    return false;
}

// Registers a low memory alarm callback.  When a low memory alarm is triggered,
// the function provided will be called, and it should free memory if possible.
void registerLowMemoryAlarmCallback(bool(*callbackFunction)(void))
{
    
}
