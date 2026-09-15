#include <loop.h>
#include <vitaGL.h>
#include <psp2/ctrl.h>

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
    setbuf(stderr, NULL);
    setbuf(stdout, NULL);

    vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
    vglSetupGarbageCollector(127, 0x20000);
    vglUseTripleBuffering(GL_FALSE);
    vglSetCircularPoolSize(128 * 1024 * 1024);
    vglSetupDisplayRenderTarget(2);
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
