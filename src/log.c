#include "log.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

static bool logToTerminal = true;
static bool logToFile = true;

static bool logColourTerminal = true;
static bool logColourFile = false;

static const char* logFile = "./butterscotch.log";

#ifndef va_copy
#define va_copy(d, s) ((d) = (s))
#endif

#define ANSI_COLOUR_CODE_WHITE "\x1b[0;37m"
#define ANSI_COLOUR_CODE_BOLD_YELLOW "\x1b[1;33m"
#define ANSI_COLOUR_CODE_BOLD_RED "\x1b[1;31m"
#define ANSI_COLOUR_CODE_BOLD_PURPLE "\x1b[1;35m"

// In the platform main.c
void platformLog(const logType type, const logOutType out, const char *format, va_list va);
// Example impl:
// void platformLog(const logType type, const logOutType out, const char *format, va_list va) {
// 	if (out == LOG_OUT_ALL) {
// 		va_list va2;
// 		va_copy(va2, va);
// 		vfprintf(type == LOG_TYPE_NORMAL ? stdout : stderr, format, va);
// 		vfprintf(logFileHandle, format, va2);
// 		va_end(va2);
// 	}
// 	else if (out == LOG_OUT_TERMINAL) {
// 		vfprintf(type == LOG_TYPE_NORMAL ? stdout : stderr, format, va);
// 	}
// 	else { // LOG_OUT_FILE
// 		vfprintf(logFileHandle, format, va);
// 	}
// }

static void vLogInternal(const logType type, const logOutType out, const char* fmt, va_list va) {
	// TODO: Seperate logColour less hackily
	if (out == LOG_OUT_ALL) {
		va_list va2;
		va_copy(va2, va);
		vLogInternal(type, LOG_OUT_TERMINAL, fmt, va);
		vLogInternal(type, LOG_OUT_FILE, fmt, va2);
		va_end(va2);
		return;
	}

	const bool logColour = out == LOG_OUT_TERMINAL ? logColourTerminal : logColourFile;

	const char* prefix = "";
	if (type == LOG_TYPE_WARNING) {
		prefix = "Warning: ";
	}
	else if (type == LOG_TYPE_ERROR) {
		prefix = "Error: ";
	}
	else if (type == LOG_TYPE_DEBUG) {
		prefix = "Debug: ";
	}

	const char* colourPrefix = "";
	const char* colourPostfix = "";

	if(logColour) {
		colourPrefix = (type == LOG_TYPE_NORMAL ? ANSI_COLOUR_CODE_WHITE : (type == LOG_TYPE_WARNING ? ANSI_COLOUR_CODE_BOLD_YELLOW : (type == LOG_TYPE_ERROR ? ANSI_COLOUR_CODE_BOLD_RED : ANSI_COLOUR_CODE_BOLD_PURPLE)));
		colourPostfix = ANSI_COLOUR_CODE_WHITE;
	}

	size_t newFmtSize = strlen(colourPrefix) + strlen(prefix) + strlen(fmt) + strlen(colourPostfix) + 1;

	char* newFmt = (char*)safeMalloc(newFmtSize);

	snprintf(newFmt, newFmtSize, "%s%s%s%s", colourPrefix, prefix, fmt, colourPostfix);

	newFmt[newFmtSize-1] = '\0';

	platformLog(type, out, newFmt, va);

	free(newFmt);
}

static void vLogToTerminal(const int type, const char* fmt, va_list va) {
	if (!logToTerminal) return;

	vLogInternal(type, LOG_OUT_TERMINAL, fmt, va);
}

static void vLogToFile(const int type, const char* fmt, va_list va) {
	if (!logToFile) return;

	vLogInternal(type, LOG_OUT_FILE, fmt, va);
}

static void vLog(const int type, const char* fmt, va_list va) {
	if (!logToTerminal && !logToFile) return;

	vLogInternal(type, LOG_OUT_ALL, fmt, va);
}

FILE* Log_init() {
	if (logFile == nullptr) return nullptr;

	FILE* logFileHandle = fopen(logFile, "w");
	if (logFileHandle != nullptr) {
		setvbuf(logFileHandle, nullptr, _IONBF, 0);
		return logFileHandle;
	}

	return nullptr;
}

void Log_setOptions(bool bLogToTerminal, bool bLogToFile, bool bLogColourTerminal, bool bLogColourFile, const char* pLogFile) {
	logToTerminal = bLogToTerminal;
	logToFile = bLogToFile;
	logColourTerminal = bLogColourTerminal;
	logColourFile = bLogColourFile;
	if (pLogFile != nullptr) {
		logFile = pLogFile;
	}
}

void logInfoToTerminal(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToTerminal(LOG_TYPE_NORMAL, fmt, va);
	va_end(va);
}

void logInfoToFile(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToFile(LOG_TYPE_NORMAL, fmt, va);
	va_end(va);
}

void logInfo(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLog(LOG_TYPE_NORMAL, fmt, va);
	va_end(va);
}

void vLogInfoToTerminal(const char* fmt, va_list va) {
	vLogToTerminal(LOG_TYPE_NORMAL, fmt, va);
}

void vLogInfoToFile(const char* fmt, va_list va) {
	vLogToFile(LOG_TYPE_NORMAL, fmt, va);
}

void vLogInfo(const char* fmt, va_list va) {
	vLog(LOG_TYPE_NORMAL, fmt, va);
}

void logWarnToTerminal(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToTerminal(LOG_TYPE_WARNING, fmt, va);
	va_end(va);
}

void logWarnToFile(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToFile(LOG_TYPE_WARNING, fmt, va);
	va_end(va);
}

void logWarn(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLog(LOG_TYPE_WARNING, fmt, va);
	va_end(va);
}

void vLogWarnToTerminal(const char* fmt, va_list va) {
	vLogToTerminal(LOG_TYPE_WARNING, fmt, va);
}

void vLogWarnToFile(const char* fmt, va_list va) {
	vLogToFile(LOG_TYPE_WARNING, fmt, va);
}

void vLogWarn(const char* fmt, va_list va) {
	vLog(LOG_TYPE_WARNING, fmt, va);
}

void logErrorToTerminal(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToTerminal(LOG_TYPE_ERROR, fmt, va);
	va_end(va);
}

void logErrorToFile(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToFile(LOG_TYPE_ERROR, fmt, va);
	va_end(va);
}

void logError(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLog(LOG_TYPE_ERROR, fmt, va);
	va_end(va);
}

void vLogErrorToTerminal(const char* fmt, va_list va) {
	vLogToTerminal(LOG_TYPE_ERROR, fmt, va);
}

void vLogErrorToFile(const char* fmt, va_list va) {
	vLogToFile(LOG_TYPE_ERROR, fmt, va);
}

void vLogError(const char* fmt, va_list va) {
	vLog(LOG_TYPE_ERROR, fmt, va);
}

void logDebugToTerminal(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToTerminal(LOG_TYPE_DEBUG, fmt, va);
	va_end(va);
}

void logDebugToFile(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToFile(LOG_TYPE_DEBUG, fmt, va);
	va_end(va);
}

void logDebug(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLog(LOG_TYPE_DEBUG, fmt, va);
	va_end(va);
}

void vLogDebugToTerminal(const char* fmt, va_list va) {
	vLogToTerminal(LOG_TYPE_DEBUG, fmt, va);
}

void vLogDebugToFile(const char* fmt, va_list va) {
	vLogToFile(LOG_TYPE_DEBUG, fmt, va);
}

void vLogDebug(const char* fmt, va_list va) {
	vLog(LOG_TYPE_DEBUG, fmt, va);
}
