#include <windows.h>

#include "log.h"

void platformLog(const logType type, const char* format, va_list va) {
    const char* prefix = "";
    switch (type) {
        case LOG_TYPE_NORMAL:
            break;
        case LOG_TYPE_WARNING:
            prefix = "Warning: ";
            break;
        case LOG_TYPE_ERROR:
            prefix = "Error: ";
            break;
        case LOG_TYPE_DEBUG:
            prefix = "Debug: ";
            break;
    }

    char buf[4096];
    vsnprintf(buf, sizeof(buf), format, va);
    OutputDebugStringA(prefix);
    OutputDebugStringA(buf);
}
