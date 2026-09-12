#include <loop.h>

/* For SDL_main */
#if defined(USE_SDL1)
#include <SDL/SDL_main.h>
#elif defined(USE_SDL2)
#include <SDL2/SDL_main.h>
#elif defined(USE_SDL3)
#include <SDL3/SDL_main.h>
#endif

// Web (Emscripten + SDL2) entry point.
//
// This is intentionally kept close to src/embedded/main.c: the SDL2 backend
// (src/backends/sdl2.c) and the shared game loop (src/loop.c) do all of the
// heavy lifting, so the platform layer only has to provide fixed startup
// arguments. The blocking loop() call yields to the browser via -sASYNCIFY
// (see the PLATFORM STREQUAL "web" block in the top-level CMakeLists.txt).
//
// The game data is loaded from the Emscripten virtual filesystem at
// "data.win", which is where src/web/shell.html stages the folder the user
// picks (a --preload-file data.win baked into the build lands there too).
// The shell also accepts sibling names like game.unx and stages them as
// data.win, so the runner only ever has to look in one place.
int main(int argc, char* argv[]) {
    (void)argc;
    setbuf(stderr, NULL);

    CommandLineArgs args = {0};

    args.exitAtFrame = -1;
#ifdef ENABLE_VM_TRACING
    args.traceBytecodeAfterFrame = 0;
#endif
    args.speedMultiplier = 1.0;
    args.fastForwardSpeed = 2.0;
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
    args.dataWinPath = "data.win";

    int ret = loop(args, argv[0]);
    freeCommandLineArgs(&args);
    return ret;
}
