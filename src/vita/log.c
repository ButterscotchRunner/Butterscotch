#include <stdio.h>
#include <psp2/kernel/clib.h>

#include "log.h"

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

    sceClibPrintf("%s%s%s", colourPrefix, textPrefix, ANSI_COLOUR_CODE_RESET);
    sceClibVprintf(format, va);
}
