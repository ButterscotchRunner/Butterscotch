#include <loop.h>
#include <stdio.h>
#include <stdarg.h>
#include "log.h"

#if defined(USE_SDL3)
#include <SDL3/SDL_main.h>
#endif

void platformLog(const logType type, const char *format, va_list va) {
    FILE *out = stderr;
    const char *prefix = "";

    switch (type) {
        case LOG_TYPE_NORMAL:
            out = stdout;
            break;
        case LOG_TYPE_WARNING:
            prefix = "Warning: ";
            break;
        case LOG_TYPE_ERROR:
            prefix = "Error: ";
            break;
        case LOG_TYPE_DEBUG:
            prefix = "Debug: ";
            break;
    }

    fputs(prefix, out);
    vfprintf(out, format, va);
}

int main(int argc, char* argv[]) {
    (void)argc;
    printf("hello\n");
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    CommandLineArgs args = {0};

    args.exitAtFrame = -1;
#ifdef ENABLE_VM_TRACING
    args.traceBytecodeAfterFrame = 0;
#endif
    args.speedMultiplier = 1.0;
    args.fastForwardSpeed = 0.0;
    args.osType = OS_UWP;
    args.profilerFramesBetween = 0;
    args.loadType = DATAWINLOADTYPE_LOAD_IN_MEMORY_AHEAD_OF_TIME;
    args.lazyRooms = true;
    args.lazyTextures = true;
    args.lazyAudio = true;
#if defined(ENABLE_MODERN_GL)
    args.renderer = MODERN_GL;
#else
    args.renderer = SOFTWARE;
#endif

    char resolvedDataWinPath[2048] = {0};
    const char *basePath = argv[0] ? argv[0] : "";
    if (basePath[0] != '\0') {
        strncpy(resolvedDataWinPath, basePath, sizeof(resolvedDataWinPath) - 1);
        bsGetDirname(resolvedDataWinPath);
        if (resolvedDataWinPath[0] != '\0' && resolvedDataWinPath[strlen(resolvedDataWinPath) - 1] != '/' && resolvedDataWinPath[strlen(resolvedDataWinPath) - 1] != '\\') {
            strncat(resolvedDataWinPath, "/", sizeof(resolvedDataWinPath) - strlen(resolvedDataWinPath) - 1);
        }
        strncat(resolvedDataWinPath, "data.win", sizeof(resolvedDataWinPath) - strlen(resolvedDataWinPath) - 1);
        if (access(resolvedDataWinPath, F_OK) == 0) {
            args.dataWinPath = resolvedDataWinPath;
        }
    }

    if (args.dataWinPath == NULL) {
        args.dataWinPath = "data.win";
    }

    int ret = loop(args, argv[0]);
    freeCommandLineArgs(&args);
    return ret;
}
