#ifndef _BS_LOG_H
#define _BS_LOG_H

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>

#define LOG_MAX_FILES 16

typedef enum {
	LOG_TYPE_NORMAL=0,
	LOG_TYPE_WARNING=1,
	LOG_TYPE_ERROR=2,
	LOG_TYPE_DEBUG=3
} logType;

void Log_setColour(bool bColour);

void logInfo(const char* fmt, ...);
void vLogInfo(const char* fmt, va_list va);

void logWarn(const char* fmt, ...);
void vLogWarn(const char* fmt, va_list va);

void logError(const char* fmt, ...);
void vLogError(const char* fmt, va_list va);

void logDebug(const char* fmt, ...);
void vLogDebug(const char* fmt, va_list va);

#endif /* _BS_LOG_H */
