#include "log.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>

bool logToTerminal = true;
bool logToFile = true;

bool logWithColours = true;

enum {
	LOG_TYPE_NORMAL=0,
	LOG_TYPE_WARNING=1,
	LOG_TYPE_ERROR=2
};

#define ANSI_COLOUR_CODE_WHITE "\e[0;37m"
#define ANSI_COLOUR_CODE_BOLD_YELLOW "\e[1;33m"
#define ANSI_COLOUR_CODE_BOLD_RED "\e[1;31m"

static void vLogToTerminal(const int type, const char* fmt, va_list va) {
	if (!logToTerminal) return;

	const FILE* out = type == LOG_TYPE_NORMAL ? stdout : stderr;

	if (logWithColours) {
		fprintf((FILE*)out, (type == LOG_TYPE_NORMAL ? ANSI_COLOUR_CODE_WHITE : (type == LOG_TYPE_WARNING ? ANSI_COLOUR_CODE_BOLD_YELLOW : ANSI_COLOUR_CODE_BOLD_RED)));
	}

	vfprintf((FILE*)out, fmt, va);

	if (logWithColours) {
		fprintf((FILE*)out, ANSI_COLOUR_CODE_WHITE);
	}
}

static void vLogToFile(const int type, const char* fmt, va_list va) {}

void Log_logToTerminal(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToTerminal(LOG_TYPE_NORMAL, fmt, va);
	va_end(va);
}

void Log_logToFile(const char* fmt, ...) {}

void Log_log(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);

	if (logToTerminal) {
		vLogToTerminal(LOG_TYPE_NORMAL, fmt, va);
	}
	if (logToFile) {
		vLogToFile(LOG_TYPE_NORMAL, fmt, va);
	}

	va_end(va);
}

void Log_logWarningToTerminal(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToTerminal(LOG_TYPE_WARNING, fmt, va);
	va_end(va);
}

void Log_logWarningToFile(const char* fmt, ...);
void Log_logWarning(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);

	if (logToTerminal) {
		vLogToTerminal(LOG_TYPE_WARNING, fmt, va);
	}
	if (logToFile) {
		vLogToFile(LOG_TYPE_WARNING, fmt, va);
	}

	va_end(va);
}

void Log_logErrorToTerminal(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToTerminal(LOG_TYPE_ERROR, fmt, va);
	va_end(va);
}

void Log_logErrorToFile(const char* fmt, ...);
void Log_logError(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);

	if (logToTerminal) {
		vLogToTerminal(LOG_TYPE_ERROR, fmt, va);
	}
	if (logToFile) {
		vLogToFile(LOG_TYPE_ERROR, fmt, va);
	}

	va_end(va);
}
