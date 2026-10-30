#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <windows.h>
#include <fileapifromapp.h>

#include "launcher.h"
#include "ui.h"
#include "file_io.h"
#include "log.h"
#include "runner_gamepad.h"
#include "stb_ds.h"
#include "utils.h"

typedef struct {
    char* title;
    char* label;
    char* path;
    char key[32];
    UwpLaunchOptions options;
} Game;

typedef struct {
    Game* games;
    const char* settings;
    char error[256];
    SDL_atomic_t ready;
} GameScan;

typedef enum {
    ACTION_NONE,
    ACTION_UP,
    ACTION_DOWN,
    ACTION_LEFT,
    ACTION_RIGHT,
    ACTION_PLAY,
    ACTION_REFRESH,
    ACTION_BACK,
    ACTION_OPTIONS,
    ACTION_QUIT
} LauncherAction;

typedef struct {
    SDL_Window* window;
    SDL_GLContext context;
    SDL_GameController* controllers[MAX_GAMEPADS];
    char* settings;
    GameScan scan;
    SDL_Thread* worker;
    Game* games;
    const char** labels;
    int selected;
    int option;
    int result;
    bool finished;
    bool optionsOpen;
    Uint32 lastMove;
    char status[256];
} Launcher;

static bool getOptionsPath(char* path, size_t size, const char* settings, const Game* game) {
    int length = snprintf(path, size, "%s%s.ini", settings, game->key);
    return length >= 0 && (size_t)length < size;
}

static bool findData(const char* dir, char* path, size_t len) {
    const char* folders[] = {"", "assets/", "gameData/"};
    const char* names[] = {"data.win", "game.unx", "game.droid", "game.ios"};
    for (size_t i = 0; i < SDL_arraysize(folders); i++) {
        for (size_t j = 0; j < SDL_arraysize(names); j++) {
            if (snprintf(path, len, "%s/%s%s", dir, folders[i], names[j]) >= (int)len) continue;
            FILE* file = uwpFopen(path, "rb");
            if (file) {
                fclose(file);
                return true;
            }
        }
    }
    return false;
}

static void addGame(GameScan* scan, const char* dir, const char* name, const char* source) {
    char path[4096];
    if (!findData(dir, path, sizeof(path))) return;
    for (int i = 0; i < arrlen(scan->games); i++) {
        if (!_stricmp(scan->games[i].path, path)) return;
    }

    // Keep options tied to the folder name, even if the drive letter changes.
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char* p = (const unsigned char*)name; *p; p++) {
        hash = (hash ^ *p) * 1099511628211ULL;
    }
    Game game = {0};
    size_t len = strlen(name) + strlen(source) + 4;
    game.title = safeStrdup(name);
    game.label = safeMalloc(len);
    snprintf(game.label, len, "%s [%s]", name, source);
    game.path = safeStrdup(path);
    snprintf(game.key, sizeof(game.key), "game-%016llx", (unsigned long long)hash);
    game.options = getOptionsPath(path, sizeof(path), scan->settings, &game)
        ? uwpLaunchOptionsLoad(path) : uwpLaunchOptionsDefault();
    arrput(scan->games, game);
}

static void scanFolder(GameScan* scan, const char* dir, const char* source) {
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/*", dir) >= (int)sizeof(path)) return;
    wchar_t pattern[4096];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, pattern, SDL_arraysize(pattern))) return;
    WIN32_FIND_DATAW entry;
    HANDLE search = FindFirstFileExFromAppW(pattern, FindExInfoBasic, &entry, FindExSearchNameMatch, NULL, 0);
    if (search == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED && !scan->error[0]) {
            snprintf(scan->error, sizeof(scan->error), "Cannot read %.180s. Check the drive's storage permissions.", dir);
        }
        return;
    }
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (!wcscmp(entry.cFileName, L".") || !wcscmp(entry.cFileName, L"..")) continue;
        char name[1024];
        if (!WideCharToMultiByte(CP_UTF8, 0, entry.cFileName, -1, name, sizeof(name), NULL, NULL)) continue;
        if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) continue;
        addGame(scan, path, name, source);
    } while (FindNextFileW(search, &entry));
    FindClose(search);
}

static int compareGames(const void* a, const void* b) {
    return _stricmp(((const Game*)a)->label, ((const Game*)b)->label);
}

static int scanGames(void* data) {
    GameScan* scan = data;
    const char* local = SDL_WinRTGetFSPathUTF8(SDL_WINRT_PATH_LOCAL_FOLDER);
    if (local) {
        char path[4096];
        if (snprintf(path, sizeof(path), "%s/butterscotch", local) < (int)sizeof(path)) {
            scanFolder(scan, path, "Internal");
        }
    }
    for (char drive = 'D'; drive <= 'Z'; drive++) {
        char path[] = "E:/butterscotch";
        char source[] = "USB E:";
        path[0] = source[4] = drive;
        scanFolder(scan, path, source);
    }
    if (arrlen(scan->games) > 1) qsort(scan->games, arrlen(scan->games), sizeof(Game), compareGames);
    SDL_AtomicSet(&scan->ready, 1);
    return 0;
}

static void freeGames(Game* games) {
    for (int i = 0; i < arrlen(games); i++) {
        free(games[i].title);
        free(games[i].label);
        free(games[i].path);
    }
    arrfree(games);
}

static void openController(SDL_GameController** controllers, int index) {
    if (!SDL_IsGameController(index)) return;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (controllers[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[i])) == id) return;
    }
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (!controllers[i]) {
            controllers[i] = SDL_GameControllerOpen(index);
            break;
        }
    }
}

static bool initLauncher(Launcher* launcher) {
    launcher->settings = SDL_GetPrefPath("Butterscotch", "Launcher");
    if (!launcher->settings) return false;
    launcher->scan.settings = launcher->settings;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER)) return false;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    launcher->window = SDL_CreateWindow("Butterscotch", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        UWP_UI_WIDTH, UWP_UI_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!launcher->window) return false;
    launcher->context = SDL_GL_CreateContext(launcher->window);
    if (!launcher->context) return false;
    if (!gladLoadGLES2Loader(SDL_GL_GetProcAddress)) {
        SDL_SetError("Could not load OpenGL ES entry points");
        return false;
    }
    if (!uwpUiInit()) return false;

    logInfo("GL: %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    if (SDL_GL_SetSwapInterval(1)) logWarn("Could not enable selector VSync: %s\n", SDL_GetError());
    for (int i = 0; i < SDL_NumJoysticks(); i++) openController(launcher->controllers, i);
    return true;
}

static void freeLauncher(Launcher* launcher, bool keepGraphics) {
    if (launcher->worker) SDL_WaitThread(launcher->worker, NULL);
    freeGames(launcher->scan.games);
    freeGames(launcher->games);
    arrfree(launcher->labels);
    SDL_free(launcher->settings);
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (launcher->controllers[i]) SDL_GameControllerClose(launcher->controllers[i]);
    }
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    if (!keepGraphics) {
        uwpUiShutdown();
        if (launcher->context) SDL_GL_DeleteContext(launcher->context);
        if (launcher->window) SDL_DestroyWindow(launcher->window);
        SDL_Quit();
    }
}

static void startScan(Launcher* launcher) {
    launcher->scan.error[0] = '\0';
    SDL_AtomicSet(&launcher->scan.ready, 0);
    launcher->worker = SDL_CreateThread(scanGames, "Game scan", &launcher->scan);
    if (launcher->worker) {
        snprintf(launcher->status, sizeof(launcher->status), "Scanning game folders...");
    } else {
        snprintf(launcher->status, sizeof(launcher->status), "Could not scan game folders: %s", SDL_GetError());
    }
}

static void finishScan(Launcher* launcher) {
    if (!launcher->worker || !SDL_AtomicGet(&launcher->scan.ready)) return;
    SDL_WaitThread(launcher->worker, NULL);
    launcher->worker = NULL;
    freeGames(launcher->games);
    arrfree(launcher->labels);
    launcher->games = launcher->scan.games;
    launcher->scan.games = NULL;
    for (int i = 0; i < arrlen(launcher->games); i++) arrput(launcher->labels, launcher->games[i].label);
    launcher->selected = 0;
    launcher->status[0] = '\0';
    if (!arrlen(launcher->games)) {
        const char* message = launcher->scan.error[0] ? launcher->scan.error
            : "No games found. Connect a USB drive and press X to refresh.";
        snprintf(launcher->status, sizeof(launcher->status), "%s", message);
    }
}

static LauncherAction readAction(Launcher* launcher, const SDL_Event* event) {
    switch (event->type) {
        case SDL_QUIT:
            return ACTION_QUIT;
        case SDL_CONTROLLERDEVICEADDED:
            openController(launcher->controllers, event->cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            for (int i = 0; i < MAX_GAMEPADS; i++) {
                SDL_GameController* controller = launcher->controllers[i];
                if (controller && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == event->cdevice.which) {
                    SDL_GameControllerClose(controller);
                    launcher->controllers[i] = NULL;
                }
            }
            break;
        case SDL_KEYDOWN:
            switch (event->key.keysym.sym) {
                case SDLK_UP: return ACTION_UP;
                case SDLK_DOWN: return ACTION_DOWN;
                case SDLK_LEFT: return ACTION_LEFT;
                case SDLK_RIGHT: return ACTION_RIGHT;
            }
            if (event->key.repeat) break;
            switch (event->key.keysym.sym) {
                case SDLK_RETURN: return ACTION_PLAY;
                case SDLK_r: return ACTION_REFRESH;
                case SDLK_ESCAPE: return ACTION_BACK;
                case SDLK_TAB: return ACTION_OPTIONS;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            switch (event->cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: return ACTION_UP;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return ACTION_DOWN;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return ACTION_LEFT;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return ACTION_RIGHT;
                case SDL_CONTROLLER_BUTTON_A: return ACTION_PLAY;
                case SDL_CONTROLLER_BUTTON_X: return ACTION_REFRESH;
                case SDL_CONTROLLER_BUTTON_B: return ACTION_BACK;
                case SDL_CONTROLLER_BUTTON_START: return ACTION_OPTIONS;
            }
            break;
        case SDL_CONTROLLERAXISMOTION: {
            if (event->caxis.axis != SDL_CONTROLLER_AXIS_LEFTY) break;
            Uint32 now = SDL_GetTicks();
            if (now - launcher->lastMove <= 180) break;
            if (event->caxis.value >= -16000 && event->caxis.value <= 16000) break;
            launcher->lastMove = now;
            return event->caxis.value < 0 ? ACTION_UP : ACTION_DOWN;
        }
    }
    return ACTION_NONE;
}

static void changeOption(Launcher* launcher, bool defaults) {
    Game* game = &launcher->games[launcher->selected];
    if (defaults) game->options = uwpLaunchOptionsDefault();
    else uwpLaunchOptionsChange(&game->options, launcher->option);
    char path[4096];
    bool saved = getOptionsPath(path, sizeof(path), launcher->settings, game)
        && uwpLaunchOptionsSave(path, &game->options);
    if (!saved) logWarn("Could not save launch options for %s\n", game->title);
}

static void handleAction(Launcher* launcher, LauncherAction action) {
    if (action == ACTION_QUIT || (action == ACTION_BACK && !launcher->optionsOpen)) {
        launcher->finished = true;
        return;
    }
    if (launcher->worker || action == ACTION_NONE) return;
    int move = action == ACTION_UP ? -1 : action == ACTION_DOWN ? 1 : 0;
    if (launcher->optionsOpen) {
        if (action == ACTION_BACK || action == ACTION_OPTIONS) {
            launcher->optionsOpen = false;
        } else if (move) {
            launcher->option = (launcher->option + move + UWP_OPTION_COUNT) % UWP_OPTION_COUNT;
        } else if (action == ACTION_PLAY || action == ACTION_LEFT || action == ACTION_RIGHT || action == ACTION_REFRESH) {
            changeOption(launcher, action == ACTION_REFRESH);
        }
        return;
    }
    if (action == ACTION_REFRESH) {
        startScan(launcher);
        return;
    }
    int count = (int)arrlen(launcher->games);
    if (!count) return;
    switch (action) {
        case ACTION_OPTIONS:
            launcher->optionsOpen = true;
            launcher->option = 0;
            break;
        case ACTION_PLAY: {
            Game* game = &launcher->games[launcher->selected];
            FILE* file = uwpFopen(game->path, "rb");
            if (!file) {
                snprintf(launcher->status, sizeof(launcher->status), "Cannot open this game. Check that its drive is still connected.");
                break;
            }
            fclose(file);
            launcher->result = 1;
            launcher->finished = true;
            break;
        }
        case ACTION_UP:
        case ACTION_DOWN:
            launcher->selected = (launcher->selected + move + count) % count;
            break;
        default:
            break;
    }
}

static void renderLauncher(const Launcher* launcher) {
    int width, height;
    SDL_GL_GetDrawableSize(launcher->window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(28 / 255.0f, 28 / 255.0f, 28 / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    uwpUiBegin();
    uwpUiSelector(launcher->labels, (int)arrlen(launcher->games), launcher->selected, launcher->status);
    if (launcher->optionsOpen) {
        const Game* game = &launcher->games[launcher->selected];
        uwpUiLaunchOptions(game->title, &game->options, launcher->option);
    }
    uwpUiRender(width, height);
    SDL_GL_SwapWindow(launcher->window);
}

int uwpChooseGame(char** dataPath, UwpLaunchOptions* options) {
    *dataPath = NULL;
    *options = uwpLaunchOptionsDefault();
    Launcher launcher = {0};
    if (!initLauncher(&launcher)) {
        logError("Could not initialize selector: %s\n", SDL_GetError());
        freeLauncher(&launcher, false);
        return -1;
    }
    startScan(&launcher);
    while (!launcher.finished) {
        finishScan(&launcher);
        SDL_Event event;
        while (!launcher.finished && SDL_PollEvent(&event)) {
            handleAction(&launcher, readAction(&launcher, &event));
        }
        if (!launcher.finished) renderLauncher(&launcher);
    }
    if (launcher.result) {
        const Game* game = &launcher.games[launcher.selected];
        *dataPath = safeStrdup(game->path);
        *options = game->options;
        SDL_SetWindowTitle(launcher.window, game->title);
    }
    freeLauncher(&launcher, launcher.result != 0);
    if (launcher.result) {
        glUseProgram(0);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_BLEND);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        uwpLoadingProgress(SDL_GetWindowTitle(launcher.window), "Reading game data...", 0, 0);
    }
    return launcher.result;
}
