#include <loop.h>
#include <stdbool.h>
#include <nds.h>
#include <filesystem.h>
#include "stb_image.h"
#include "nds_image.h"
#include <fat.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

static FILE* logFile = NULL;

//nitro:/friend.png
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

    if (logFile) {
        fputs(buffer, logFile);
        fflush(logFile);
        fsync(fileno(logFile));
    }
}

int main(int argc, char* argv[]){
    //Init DS stuff
    consoleDemoInit(); //Bottom screen log
    fatInitDefault();
    logFile = fopen("sd:/butterscotch_log.txt", "w");

    logInfo("Hello butterscotchDS!\n");
    if (!nitroFSInit(NULL)){
        logInfo("nitroFSInit failed!!\n");
    }

    CommandLineArgs args = {0};

#ifdef ENABLE_VM_TRACING
    args.traceBytecodeAfterFrame = 0;
#endif
    args.speedMultiplier = 1.0;
    args.fastForwardSpeed = 0.0;
    args.osType = OS_WINDOWS;
    args.profilerFramesBetween = 0;
    args.dataWinPath = "nitro:/data.win";
    args.saveFolder = "";
    args.headless = false;
    args.lazyTextures = true;
    args.lazyRooms = true;
    args.lazyAudio = true;
    args.eagerRooms = NULL;
    args.exitAtFrame = -1;
    args.renderer = NOOP;
    args.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;


    int ret = loop(args, argv[0]);
    freeCommandLineArgs(&args);
    return ret;

    logInfo("loop return: %d\n", ret);

    while (true)
        swiWaitForVBlank();
}

/*
#include <loop.h>
#include <vitaGL.h>
#include <psp2/ctrl.h>

/* For SDL_main *//*
#if defined(USE_SDL1)
#include <SDL/SDL_main.h>
#elif defined(USE_SDL2)
#include <SDL2/SDL_main.h>
#elif defined(USE_SDL3)
#include <SDL3/SDL_main.h>
#endif

int main(int argc, char* argv[]) {
    (void)argc;
    setbuf(stderr, NULL);
    setbuf(stdout, NULL);

    vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
    vglSetupGarbageCollector(127, 0x20000);
    vglUseTripleBuffering(GL_FALSE);
    vglSetCircularPoolSize(128 * 1024 * 1024);
    vglSetupRenderTargetScenesNum(2, 1);
    vglSetParamBufferSize(6 * 1024 * 1024);
    vglSetShaderCachePath("ux0:data/butterscotch/shader_cache");
    vglInitWithCustomThreshold(0, 960, 544, 8 * 1024 * 1024, 0, 0, 0, SCE_GXM_MULTISAMPLE_NONE);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    CommandLineArgs args = {0};

    args.exitAtFrame = -1;
#ifdef ENABLE_VM_TRACING
    args.traceBytecodeAfterFrame = 0;
#endif
    args.speedMultiplier = 1.0;
    args.fastForwardSpeed = 0.0;
    args.osType = OS_WINDOWS;
    args.profilerFramesBetween = 0;
    args.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;
    args.lazyRooms = true;
    args.lazyTextures = true;
    args.lazyAudio = true;
#if defined(ENABLE_MODERN_GL)
    args.renderer = MODERN_GL;
#elif defined(ENABLE_LEGACY_GL)
    args.renderer = LEGACY_GL;
#else
    args.renderer = SOFTWARE;
#endif
    args.dataWinPath = "ux0:data/butterscotch/data.win";
    args.saveFolder = "ux0:data/butterscotch";

    int ret = loop(args, argv[0]);
    freeCommandLineArgs(&args);
    return ret;
}

*/