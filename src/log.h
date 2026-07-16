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

typedef enum {
	LOG_OUT_ALL=0,
	LOG_OUT_TERMINAL=1,
	LOG_OUT_FILE=2
} logOutType;

FILE* Log_init();
void Log_setOptions(bool bLogToTerminal, bool bLogToFile, bool bLogColourTerminal, bool bLogColourFile, const char* pLogFile);

void logInfoToTerminal(const char* fmt, ...);
void logInfoToFile(const char* fmt, ...);
void logInfo(const char* fmt, ...);

void vLogInfoToTerminal(const char* fmt, va_list va);
void vLogInfoToFile(const char* fmt, va_list va);
void vLogInfo(const char* fmt, va_list va);

void logWarnToTerminal(const char* fmt, ...);
void logWarnToFile(const char* fmt, ...);
void logWarn(const char* fmt, ...);

void vLogWarnToTerminal(const char* fmt, va_list va);
void vLogWarnToFile(const char* fmt, va_list va);
void vLogWarn(const char* fmt, va_list va);

void logErrorToTerminal(const char* fmt, ...);
void logErrorToFile(const char* fmt, ...);
void logError(const char* fmt, ...);

void vLogErrorToTerminal(const char* fmt, va_list va);
void vLogErrorToFile(const char* fmt, va_list va);
void vLogError(const char* fmt, va_list va);

void logDebugToTerminal(const char* fmt, ...);
void logDebugToFile(const char* fmt, ...);
void logDebug(const char* fmt, ...);

void vLogDebugToTerminal(const char* fmt, va_list va);
void vLogDebugToFile(const char* fmt, va_list va);
void vLogDebug(const char* fmt, va_list va);

#endif /* _BS_LOG_H */
