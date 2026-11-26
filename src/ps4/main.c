#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>

#include <SDL2/SDL.h>

#include <loop.h>
#include <platformdefs.h>

int main(int argc, char** argv) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

#ifdef ENABLE_MODERN_GL
    struct stat moduleStat;
    if (stat("/app0/sce_module/libScePigletv2VSH.sprx", &moduleStat) == 0 &&
        stat("/app0/sce_module/libSceShaccVSH.sprx", &moduleStat) == 0) {
        SDL_SetHint("SDL_PS4_PIGLET_MODULES_PATH", "/app0/sce_module");
    }
#endif

    CommandLineArgs args = {0};
    args.exitAtFrame = -1;
    args.speedMultiplier = 1.0;
    args.fastForwardSpeed = 0.0;
    args.osType = OS_PS4;
    args.profilerFramesBetween = 0;
    args.loadType = DATAWINLOADTYPE_LOAD_IN_MEMORY_AHEAD_OF_TIME;
#ifdef ENABLE_SW_RENDERER
    args.renderer = SOFTWARE;
#else
    args.renderer = MODERN_GL;
#endif
    const char* dataWinPath = argc > 1 ? argv[1] : "/app0/data.win";
    if (argc <= 1) {
        struct stat st;
        if (stat(dataWinPath, &st) != 0 && stat("/data/butterscotch/data.win", &st) == 0) {
            dataWinPath = "/data/butterscotch/data.win";
        }
    }

    if (mkdir("/data/butterscotch", 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "Could not create /data/butterscotch: %s\n", strerror(errno));
    }

    args.dataWinPath = dataWinPath;
    args.saveFolder = "/data/butterscotch";

    int ret = loop(args, argc > 0 ? argv[0] : "butterscotch");
    freeCommandLineArgs(&args);
    return ret;
}
