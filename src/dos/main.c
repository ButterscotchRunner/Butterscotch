#include "../cli/args.h"
#include "log.h"
#include <loop.h>

#include <SDL3/SDL_main.h>

#include <stdarg.h>
#include <stdio.h>

void platformLog(const logType type, const char* format, va_list va) {
    FILE* out = type == LOG_TYPE_NORMAL ? stdout : stderr;
    switch (type) {
        case LOG_TYPE_WARNING:
            fputs("Warning: ", out);
            break;
        case LOG_TYPE_ERROR:
            fputs("Error: ", out);
            break;
        case LOG_TYPE_DEBUG:
            fputs("Debug: ", out);
            break;
        case LOG_TYPE_NORMAL:
            break;
    }
    vfprintf(out, format, va);
}

int main(int argc, char* argv[]) {
    setbuf(stderr, NULL);

    CommandLineArgs args;
    parseCommandLineArgs(&args, argc, argv);
    int result = loop(args, argv[0]);
    freeCommandLineArgs(&args);
    return result;
}
