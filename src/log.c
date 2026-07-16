#include "log.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

#include "common.h"

static bool logToTerminal = true;
static bool logToFile = true;

static bool logColourTerminal = true;
static bool logColourFile = false;

static const char* logFile = "./butterscotch.log";
static const char* logFileBackup = "./butterscotch.log";

static FILE* logFileHandleBackup = nullptr;
static FILE* logFileHandle = nullptr;

static FILE* logFiles[LOG_MAX_FILES];

enum {
	LOG_TYPE_NORMAL=0,
	LOG_TYPE_WARNING=1,
	LOG_TYPE_ERROR=2,
	LOG_TYPE_DEBUG=3
};

#ifndef va_copy
#define va_copy(d, s) ((d) = (s))
#endif

#define ANSI_COLOUR_CODE_WHITE "\x1b[0;37m"
#define ANSI_COLOUR_CODE_BOLD_YELLOW "\x1b[1;33m"
#define ANSI_COLOUR_CODE_BOLD_RED "\x1b[1;31m"
#define ANSI_COLOUR_CODE_BOLD_PURPLE "\x1b[1;35m"

static void vLogInternal(FILE* file, bool logColour, const int type, const char* fmt, va_list va) {
	if (logColour) {
		fprintf(file, (type == LOG_TYPE_NORMAL ? ANSI_COLOUR_CODE_WHITE : (type == LOG_TYPE_WARNING ? ANSI_COLOUR_CODE_BOLD_YELLOW : (type == LOG_TYPE_ERROR ? ANSI_COLOUR_CODE_BOLD_RED : ANSI_COLOUR_CODE_BOLD_PURPLE))));
	}

	if (type != LOG_TYPE_NORMAL) {
		fprintf(file, (type == LOG_TYPE_WARNING ? "Warning: " : (type == LOG_TYPE_ERROR ? "Error: " : "Debug: ")));
	}

	vfprintf(file, fmt, va);

	if (logColour) {
		fprintf(file, ANSI_COLOUR_CODE_WHITE);
	}
}

static void vLogToTerminal(const int type, const char* fmt, va_list va) {
	if (!logToTerminal) return;

	vLogInternal(type == LOG_TYPE_NORMAL ? stdout : stderr, logColourTerminal, type, fmt, va);
}

static void vLogToFile(const int type, const char* fmt, va_list va) {
	if (!logToFile || !logFileHandle) return;

	vLogInternal(logFileHandle, logColourFile, type, fmt, va);

	for (int i=0; i < LOG_MAX_FILES; i++) {
		if (!logFiles[i]) continue;
		vLogInternal(logFiles[i], logColourFile, type, fmt, va);
	}
}

void Log_init() {
	memset(logFiles, 0, sizeof(logFiles));

	if (logFileHandle != nullptr) {
		fclose(logFileHandle);
	}

	if (logFile != nullptr) {
		logFileHandle = fopen(logFile, "w");
		if (logFileHandle != nullptr) {
			setvbuf(logFileHandle, nullptr, _IONBF, 0);
			logFileHandleBackup = logFileHandle;
			logFileBackup = logFile;
		}
	}
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

void Log_setFile(FILE* file, const char* path) {
	logFileHandle = file;
	logFile = path;
}

void Log_resetFile() {
	logFileHandle = logFileHandleBackup;
	logFile = logFileBackup;
}

bool Log_addFile(FILE* file) {
	if (file == nullptr) return false;

	int availableSlot = -1;
	for (int i=0; i < LOG_MAX_FILES; i++) {
		if (logFiles[i] == file) {
			return true;
		}
		if (logFiles[i] == nullptr) {
			availableSlot = i;
		}
	}

	if (availableSlot == -1) return false;

	logFiles[availableSlot] = file;
	return true;
}

bool Log_removeFile(FILE* file) {
	if (file == nullptr) return false;

	for (int i=0; i < LOG_MAX_FILES; i++) {
		if (logFiles[i] == file) {
			logFiles[i] = nullptr;
			return true;
		}
	}

	return false;
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

void vLogInfoToTerminal(const char* fmt, va_list va) {
	vLogToTerminal(LOG_TYPE_NORMAL, fmt, va);
}

void vLogInfoToFile(const char* fmt, va_list va) {
	vLogToFile(LOG_TYPE_NORMAL, fmt, va);
}

void vLogInfo(const char* fmt, va_list va) {
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

void vLogWarnToTerminal(const char* fmt, va_list va) {
	vLogToTerminal(LOG_TYPE_WARNING, fmt, va);
}

void vLogWarnToFile(const char* fmt, va_list va) {
	vLogToFile(LOG_TYPE_WARNING, fmt, va);
}

void vLogWarn(const char* fmt, va_list va) {
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

void vLogErrorToTerminal(const char* fmt, va_list va) {
	vLogToTerminal(LOG_TYPE_ERROR, fmt, va);
}

void vLogErrorToFile(const char* fmt, va_list va) {
	vLogToFile(LOG_TYPE_ERROR, fmt, va);
}

void vLogError(const char* fmt, va_list va) {
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
	va_list va, va2;

	va_start(va, fmt);
	va_copy(va2, va);

	if (logToTerminal) {
		vLogToTerminal(LOG_TYPE_DEBUG, fmt, va);
	}
	if (logToFile) {
		vLogToFile(LOG_TYPE_DEBUG, fmt, va2);
	}

	va_end(va);
	va_end(va2);
}

void vLogDebugToTerminal(const char* fmt, va_list va) {
	vLogToTerminal(LOG_TYPE_DEBUG, fmt, va);
}

void vLogDebugToFile(const char* fmt, va_list va) {
	vLogToFile(LOG_TYPE_DEBUG, fmt, va);
}

void vLogDebug(const char* fmt, va_list va) {
	va_list va2;
	va_copy(va2, va);

	if (logToTerminal) {
		vLogToTerminal(LOG_TYPE_DEBUG, fmt, va);
	}
	if (logToFile) {
		vLogToFile(LOG_TYPE_DEBUG, fmt, va2);
	}

	va_end(va2);
}
