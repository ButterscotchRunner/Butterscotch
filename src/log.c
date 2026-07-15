#include "log.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>

#include "common.h"

bool logToTerminal = true;
bool logToFile = true;

bool logColourTerminal = true;
bool logColourFile = false;

char* logFile = "./butterscotch.log";

static FILE* logFileHandle = nullptr;

enum {
	LOG_TYPE_NORMAL=0,
	LOG_TYPE_WARNING=1,
	LOG_TYPE_ERROR=2
};

#define ANSI_COLOUR_CODE_WHITE "\x1b[0;37m"
#define ANSI_COLOUR_CODE_BOLD_YELLOW "\x1b[1;33m"
#define ANSI_COLOUR_CODE_BOLD_RED "\x1b[1;31m"

void Log_setOptions(bool bLogToTerminal, bool bLogToFile, bool bLogColourTerminal, bool bLogColourFile, char* pLogFile) {
	logToTerminal = bLogToTerminal;
	logToFile = bLogToFile;
	logColourTerminal = bLogColourTerminal;
	logColourFile = bLogColourFile;
	logFile = pLogFile;
}

static void vLogToTerminal(const int type, const char* fmt, va_list va) {
	if (!logToTerminal) return;

	FILE* out = type == LOG_TYPE_NORMAL ? stdout : stderr;

	if (logColourTerminal) {
		fprintf(out, (type == LOG_TYPE_NORMAL ? ANSI_COLOUR_CODE_WHITE : (type == LOG_TYPE_WARNING ? ANSI_COLOUR_CODE_BOLD_YELLOW : ANSI_COLOUR_CODE_BOLD_RED)));
	}

	vfprintf(out, fmt, va);

	if (logColourTerminal) {
		fprintf(out, ANSI_COLOUR_CODE_WHITE);
	}
}

static void vLogToFile(const int type, const char* fmt, va_list va) {
	if (!logToFile) return;

	if (logFileHandle == nullptr) {
		logFileHandle = fopen(logFile, "a");
		if (logFileHandle == nullptr) return;
		setvbuf(logFileHandle, nullptr, _IONBF, 0);
	}

	if (logColourFile) {
		fprintf(logFileHandle, (type == LOG_TYPE_NORMAL ? ANSI_COLOUR_CODE_WHITE : (type == LOG_TYPE_WARNING ? ANSI_COLOUR_CODE_BOLD_YELLOW : ANSI_COLOUR_CODE_BOLD_RED)));
	}

	vfprintf(logFileHandle, fmt, va);

	if (logColourFile) {
		fprintf(logFileHandle, ANSI_COLOUR_CODE_WHITE);
	}
}

void Log_init() {
	if (logFileHandle != nullptr) {
		fclose(logFileHandle);
	}
	logFileHandle = fopen(logFile, "w");
	if (logFileHandle != nullptr) {
		setvbuf(logFileHandle, nullptr, _IONBF, 0);
	}
}

void Log_logToTerminal(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToTerminal(LOG_TYPE_NORMAL, fmt, va);
	va_end(va);
}

void Log_logToFile(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToFile(LOG_TYPE_NORMAL, fmt, va);
	va_end(va);
}

void Log_log(const char* fmt, ...) {
	va_list va, va2;

	va_start(va, fmt);
	va_copy(va2, va);

	if (logToTerminal) {
		vLogToTerminal(LOG_TYPE_NORMAL, fmt, va);
	}
	if (logToFile) {
		vLogToFile(LOG_TYPE_NORMAL, fmt, va2);
	}

	va_end(va);
	va_end(va2);
}

void Log_vLogToTerminal(const char* fmt, va_list va) {
	vLogToTerminal(LOG_TYPE_NORMAL, fmt, va);
}

void Log_vLogToFile(const char* fmt, va_list va) {
	vLogToFile(LOG_TYPE_NORMAL, fmt, va);
}

void Log_vLog(const char* fmt, va_list va) {
	va_list va2;
	va_copy(va2, va);

	if (logToTerminal) {
		vLogToTerminal(LOG_TYPE_NORMAL, fmt, va);
	}
	if (logToFile) {
		vLogToFile(LOG_TYPE_NORMAL, fmt, va2);
	}

	va_end(va2);
}

void Log_logWarningToTerminal(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToTerminal(LOG_TYPE_WARNING, fmt, va);
	va_end(va);
}

void Log_logWarningToFile(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToFile(LOG_TYPE_WARNING, fmt, va);
	va_end(va);
}

void Log_logWarning(const char* fmt, ...) {
	va_list va, va2;

	va_start(va, fmt);
	va_copy(va2, va);

	if (logToTerminal) {
		vLogToTerminal(LOG_TYPE_WARNING, fmt, va);
	}
	if (logToFile) {
		vLogToFile(LOG_TYPE_WARNING, fmt, va2);
	}

	va_end(va);
	va_end(va2);
}

void Log_vLogWarningToTerminal(const char* fmt, va_list va) {
	vLogToTerminal(LOG_TYPE_WARNING, fmt, va);
}

void Log_vLogWarningToFile(const char* fmt, va_list va) {
	vLogToFile(LOG_TYPE_WARNING, fmt, va);
}

void Log_vLogWarning(const char* fmt, va_list va) {
	va_list va2;
	va_copy(va2, va);

	if (logToTerminal) {
		vLogToTerminal(LOG_TYPE_WARNING, fmt, va);
	}
	if (logToFile) {
		vLogToFile(LOG_TYPE_WARNING, fmt, va2);
	}

	va_end(va2);
}

void Log_logErrorToTerminal(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToTerminal(LOG_TYPE_ERROR, fmt, va);
	va_end(va);
}

void Log_logErrorToFile(const char* fmt, ...) {
	va_list va;

	va_start(va, fmt);
	vLogToFile(LOG_TYPE_ERROR, fmt, va);
	va_end(va);
}

void Log_logError(const char* fmt, ...) {
	va_list va, va2;

	va_start(va, fmt);
	va_copy(va2, va);

	if (logToTerminal) {
		vLogToTerminal(LOG_TYPE_ERROR, fmt, va);
	}
	if (logToFile) {
		vLogToFile(LOG_TYPE_ERROR, fmt, va2);
	}

	va_end(va);
	va_end(va2);
}

void Log_vLogErrorToTerminal(const char* fmt, va_list va) {
	vLogToTerminal(LOG_TYPE_ERROR, fmt, va);
}

void Log_vLogErrorToFile(const char* fmt, va_list va) {
	vLogToFile(LOG_TYPE_ERROR, fmt, va);
}

void Log_vLogError(const char* fmt, va_list va) {
	va_list va2;
	va_copy(va2, va);

	if (logToTerminal) {
		vLogToTerminal(LOG_TYPE_ERROR, fmt, va);
	}
	if (logToFile) {
		vLogToFile(LOG_TYPE_ERROR, fmt, va2);
	}

	va_end(va2);
}
