#include <syslog.h>

#include "log.h"

void platformLog(const logType type, const char *format, va_list va) {
    int priority;
    switch (type) {
        case LOG_TYPE_NORMAL:
            priority = LOG_INFO;
            break;
        case LOG_TYPE_WARNING:
            priority = LOG_WARNING;
            break;
        case LOG_TYPE_ERROR:
            priority = LOG_ERR;
            break;
        case LOG_TYPE_DEBUG:
            priority = LOG_DEBUG;
            break;
    }

    vsyslog(priority, format, va);
}
