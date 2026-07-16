#ifndef _BS_LOG_H
#define _BS_LOG_H

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>

#define LOG_MAX_FILES 16

void Log_init();
void Log_setOptions(bool bLogToTerminal, bool bLogToFile, bool bLogColourTerminal, bool bLogColourFile, const char* pLogFile);

void Log_setFile(FILE* file, const char* path);
void Log_resetFile();

bool Log_addFile(FILE* file);
bool Log_removeFile(FILE* file);

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
