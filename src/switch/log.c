#include <stdio.h>

#include "log.h"

static FILE *logFile;

static void ensureLogFile(void) {
    if (!logFile) {
        // sdmc:/butterscotch.log so progress is visible on emulator/hardware without a console
        logFile = fopen("sdmc:/butterscotch.log", "w");
    }
}

void platformLog(const logType type, const char *format, va_list va) {
    const char* colourPrefix = ANSI_COLOUR_CODE_RESET;
    const char* textPrefix = "";
    switch (type) {
        case LOG_TYPE_NORMAL:
            break;
        case LOG_TYPE_WARNING:
            colourPrefix = ANSI_COLOUR_CODE_BOLD_YELLOW;
            textPrefix = "Warning: ";
            break;
        case LOG_TYPE_ERROR:
            colourPrefix = ANSI_COLOUR_CODE_BOLD_RED;
            textPrefix = "Error: ";
            break;
        case LOG_TYPE_DEBUG:
            colourPrefix = ANSI_COLOUR_CODE_BOLD_PURPLE;
            textPrefix = "Debug: ";
            break;
    }

    va_list vaFile;
    va_copy(vaFile, va);
    printf("%s%s%s", colourPrefix, textPrefix, ANSI_COLOUR_CODE_RESET);
    vprintf(format, va);

    ensureLogFile();
    if (logFile) {
        fprintf(logFile, "%s", textPrefix);
        vfprintf(logFile, format, vaFile);
        fflush(logFile);
    }
    va_end(vaFile);
}