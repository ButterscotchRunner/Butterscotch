#include "log.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

static bool logColour = true;

#define ANSI_COLOUR_CODE_WHITE "\x1b[0;37m"
#define ANSI_COLOUR_CODE_BOLD_YELLOW "\x1b[1;33m"
#define ANSI_COLOUR_CODE_BOLD_RED "\x1b[1;31m"
#define ANSI_COLOUR_CODE_BOLD_PURPLE "\x1b[1;35m"

// In the platform main.c
void platformLog(const logType type, const char *format, va_list va);

// Example impl:
// void platformLog(const logType type, const char *format, va_list va) {
// 	vfprintf(type == LOG_TYPE_NORMAL ? stdout : stderr, format, va);
// }

static void vLog(const logType type, const char* fmt, va_list va) {
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

	platformLog(type, newFmt, va);

	free(newFmt);
}

void Log_setColour(bool bLogColour) {
	logColour = bLogColour;
}

void logInfo(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLog(LOG_TYPE_NORMAL, fmt, va);
	va_end(va);
}

void vLogInfo(const char* fmt, va_list va) {
	vLog(LOG_TYPE_NORMAL, fmt, va);
}

void logWarn(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLog(LOG_TYPE_WARNING, fmt, va);
	va_end(va);
}

void vLogWarn(const char* fmt, va_list va) {
	vLog(LOG_TYPE_WARNING, fmt, va);
}


void logError(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLog(LOG_TYPE_ERROR, fmt, va);
	va_end(va);
}

void vLogError(const char* fmt, va_list va) {
	vLog(LOG_TYPE_ERROR, fmt, va);
}

void logDebug(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLog(LOG_TYPE_DEBUG, fmt, va);
	va_end(va);
}

void vLogDebug(const char* fmt, va_list va) {
	vLog(LOG_TYPE_DEBUG, fmt, va);
}
