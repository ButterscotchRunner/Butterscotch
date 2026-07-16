#ifndef _BS_LOG_H
#define _BS_LOG_H

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>

#define LOG_MAX_FILES 16

void Log_init();
void Log_setOptions(bool bLogToTerminal, bool bLogToFile, bool bLogColourTerminal, bool bLogColourFile, char* pLogFile);

void Log_setFile(FILE* file);
void Log_resetFile();

bool Log_addFile(FILE* file);
bool Log_removeFile(FILE* file);

void Log_logToTerminal(const char* fmt, ...);
void Log_logToFile(const char* fmt, ...);
void Log_log(const char* fmt, ...);

void Log_vLogToTerminal(const char* fmt, va_list va);
void Log_vLogToFile(const char* fmt, va_list va);
void Log_vLog(const char* fmt, va_list va);

void Log_logWarningToTerminal(const char* fmt, ...);
void Log_logWarningToFile(const char* fmt, ...);
void Log_logWarning(const char* fmt, ...);

void Log_vLogWarningToTerminal(const char* fmt, va_list va);
void Log_vLogWarningToFile(const char* fmt, va_list va);
void Log_vLogWarning(const char* fmt, va_list va);

void Log_logErrorToTerminal(const char* fmt, ...);
void Log_logErrorToFile(const char* fmt, ...);
void Log_logError(const char* fmt, ...);

void Log_vLogErrorToTerminal(const char* fmt, va_list va);
void Log_vLogErrorToFile(const char* fmt, va_list va);
void Log_vLogError(const char* fmt, va_list va);

#endif /* _BS_LOG_H */
