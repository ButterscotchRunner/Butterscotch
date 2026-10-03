#include "args.h"
#include <loop.h>
#include <getopt.h>

/* For SDL_main */
#if defined(USE_SDL1)
#include <SDL/SDL_main.h>
#elif defined(USE_SDL2)
#include <SDL2/SDL_main.h>
#elif defined(USE_SDL3)
#include <SDL3/SDL_main.h>
#endif

static bool logColour;

void platformLog(const logType type, const char *format, va_list va) {
    FILE *out = stderr;
    const char* colourPrefix = ANSI_COLOUR_CODE_RESET;
    const char* textPrefix = "";
    switch (type) {
        case LOG_TYPE_NORMAL:
            out = stdout;
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

    if (logColour) fputs(colourPrefix, out);
    fputs(textPrefix, out);
    if (logColour) fputs(ANSI_COLOUR_CODE_RESET, out);
    vfprintf(out, format, va);
}

int main(int argc, char* argv[]) {
    setbuf(stderr, NULL);

    CommandLineArgs args;
    parseCommandLineArgs(&args, argc, argv);
    logColour = !args.disableLogColours;
    int ret = loop(args, argv[0]);
    freeCommandLineArgs(&args);
    return ret;
}
