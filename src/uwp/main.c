#include <SDL2/SDL.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "loop.h"
#include "frontend/launcher.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    setbuf(stderr, NULL);
    setbuf(stdout, NULL);
    SDL_SetHint(SDL_HINT_WINRT_HANDLE_BACK_BUTTON, "1");

    char* path;
    UwpLaunchOptions options;
    int selected = uwpChooseGame(&path, &options);
    if (selected <= 0) return selected < 0 ? 1 : 0;

    CommandLineArgs args = {0};
    args.dataWinPath = path;
    args.exitAtFrame = -1;
    args.speedMultiplier = 1.0f;
    args.debug = options.debugStats;
    args.osType = OS_WINDOWS;
    args.loadType = DATAWINLOADTYPE_LOAD_IN_MEMORY_AHEAD_OF_TIME;
    args.lazyRooms = false;
    args.lazyTextures = options.lazyTextures;
    args.lazyAudio = false;
    args.renderer = MODERN_GL;
    args.glTextureFormat = options.texture16bpp ? GL_TEXTURE_FORMAT_RGBA4 : GL_TEXTURE_FORMAT_RGBA;
    args.disableLogColours = true;

    int ret = loop(args, "butterscotch.exe");
    freeCommandLineArgs(&args);
    free(path);
    return ret;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    return SDL_WinRTRunApp(SDL_main, NULL);
}
