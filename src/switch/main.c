#include <loop.h>
#include <switch.h>
#include <sys/stat.h>
#include <GLES3/gl3.h>

/* For SDL_main */
#if defined(USE_SDL1)
#include <SDL/SDL_main.h>
#elif defined(USE_SDL2)
#include <SDL2/SDL_main.h>
#elif defined(USE_SDL3)
#include <SDL3/SDL_main.h>
#endif

int main(int argc, char* argv[]) {
    (void)argc;
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    fsdevMountSdmc();

    CommandLineArgs args = {0};

    args.exitAtFrame = -1;
#ifdef ENABLE_VM_TRACING
    args.traceBytecodeAfterFrame = 0;
#endif
    args.speedMultiplier = 1.0;
    args.fastForwardSpeed = 0.0;
    args.osType = OS_WINDOWS;
    args.profilerFramesBetween = 0;
    args.loadType = DATAWINLOADTYPE_LOAD_IN_MEMORY_AHEAD_OF_TIME;
#if defined(ENABLE_MODERN_GL)
    args.renderer = MODERN_GL;
#else
    args.renderer = SOFTWARE;
#endif
    args.dataWinPath = "sdmc:/switch/butterscotch/data.win";
    args.saveFolder = "sdmc:/switch/butterscotch";

    int ret = loop(args, argv[0]);
    freeCommandLineArgs(&args);
    return ret;
}
