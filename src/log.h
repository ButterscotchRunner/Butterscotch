#ifndef _BS_LOG_H
#define _BS_LOG_H

#include <stdio.h>
#include <stdarg.h>

void Log_init();

void Log_logToTerminal(const char* fmt, ...);
void Log_logToFile(const char* fmt, ...);
void Log_log(const char* fmt, ...);

void Log_logWarningToTerminal(const char* fmt, ...);
void Log_logWarningToFile(const char* fmt, ...);
void Log_logWarning(const char* fmt, ...);

void Log_logErrorToTerminal(const char* fmt, ...);
void Log_logErrorToFile(const char* fmt, ...);
void Log_logError(const char* fmt, ...);

#endif /* _BS_LOG_H */
