#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "log.h"

#define LOG_BUFFER_SIZE 1024

void platformLog(const logType type, const char *format, va_list va) {
    const char* colourPrefix = ANSI_COLOUR_CODE_RESET;
    const char* textPrefix = "";
    char buffer[LOG_BUFFER_SIZE];
    
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
    int written = snprintf(buffer, sizeof(buffer), "%s", textPrefix);
    
    if (written >= 0 && written < (int)sizeof(buffer)) {
        vsnprintf(buffer + written, (int)sizeof(buffer) - written, format, va);
    }
    buffer[sizeof(buffer) - 1] = '\0';
    
    printf("%s%s%s", colourPrefix, buffer, ANSI_COLOUR_CODE_RESET);
    
    svcOutputDebugString(buffer, strlen(buffer));
}
