#include <stdio.h>

#include "log.h"

void platformLog(const logType type, const char* format, va_list va) {
    FILE* out = type == LOG_TYPE_WARNING || type == LOG_TYPE_ERROR ? stderr : stdout;
    vfprintf(out, format, va);
    fflush(out);
}
