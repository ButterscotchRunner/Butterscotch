#include "menu.h"
#include <platformdefs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#define LAUNCHER_PATH_CAPACITY 4096
#else
#include <limits.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#define LAUNCHER_PATH_CAPACITY PATH_MAX
#endif

typedef struct LauncherGame {
    char name[256];
    char path[4096];
} LauncherGame;

typedef struct LauncherOption {
    const char* flag;
    const char* description;
    const char* valueName;
} LauncherOption;

static const LauncherOption launcherOptions[] = {
#ifdef ENABLE_SCREENSHOTS
    {"--screenshot", "Specify the filename for screenshots", "filename"},
    {"--screenshot-at-frame", "Take a screenshot at the specified frame", "frame"},
    {"--screenshot-surfaces", "Take a screenshot of all surfaces", "filename"},
    {"--screenshot-surfaces-at-frame", "Select the surface screenshot frame", "frame"},
#endif
#ifndef USE_GLFW2
    {"--headless", "Launch without a window", NULL},
#endif
    {"--print-rooms", "Print all rooms and exit", NULL},
    {"--print-objects", "Print all objects and exit", NULL},
    {"--print-shaders", "Print all shaders and exit", NULL},
    {"--print-declared-functions", "Print all declared functions and exit", NULL},
    {"--print-unknown-functions", "Print all unknown functions and exit", NULL},
#ifdef ENABLE_VM_TRACING
    {"--trace-variable-reads", "Trace variable reads (optional name filter)", "filter"},
    {"--trace-variable-writes", "Trace variable writes (optional name filter)", "filter"},
    {"--trace-function-calls", "Trace function calls (optional name filter)", "filter"},
    {"--trace-alarms", "Trace alarms (optional name filter)", "filter"},
    {"--trace-instance-lifecycles", "Trace instance creations and deletions (optional filter)", "filter"},
    {"--trace-events", "Trace events (optional name filter)", "filter"},
    {"--trace-collisions", "Trace collisions (optional filter)", "filter"},
    {"--trace-event-inherited", "Trace inherited event calls", NULL},
    {"--trace-tiles", "Trace drawn tiles (optional filter)", "filter"},
    {"--trace-opcodes", "Trace opcodes (optional filter)", "filter"},
    {"--trace-stack", "Trace stack (optional filter)", "filter"},
#endif
    {"--trace-frames", "Log frametimes", NULL},
    {"--always-log-unknown-functions", "Always log unknown function calls", NULL},
#ifdef ENABLE_VM_STUB_LOGS
    {"--always-log-stubbed-functions", "Always log stubbed function calls", NULL},
#endif
    {"--exit-at-frame", "Exit at the specified frame", "frame"},
#ifdef ENABLE_VM_TRACING
    {"--trace-bytecode-after-frame", "Delay stack and opcode tracing", "frame"},
#endif
    {"--dump-frame", "Dump the runner state", "frame"},
    {"--dump-frame-json", "Dump the runner state as JSON", "frame"},
    {"--dump-frame-json-file", "Specify an output file for state dumps", "file"},
    {"--speed", "Set a normal speed multiplier", "speed"},
    {"--fast-forward-speed", "Set a fast-forward speed multiplier", "speed"},
    {"--seed", "Seed the random number generator", "seed"},
    {"--debug", "Enable debug mode", NULL},
    {"--disassemble", "Disassemble the specified script (* for all)", "script"},
    {"--record-inputs", "Record keyboard inputs to a file", "file"},
    {"--playback-inputs", "Playback input from a file", "file"},
    {"--renderer", "Set the rendering API", "renderer"},
    {"--lazy-rooms", "Lazily load rooms", NULL},
    {"--eager-room", "Keep rooms always in memory", "rooms"},
    {"--os-type", "Set the reported OS type", "os"},
    {"--window-size", "Set a custom window size", "dimensions"},
    {"--widescreen-hack", "Set a custom aspect ratio", "aspect ratio"},
#ifdef ENABLE_VM_GML_PROFILER
    {"--profile-gml-scripts", "Log GML script performance", "frames between reports"},
#endif
    {"--save-folder", "Set the save directory", "directory"},
    {"--game-args", "Arguments to pass to the game", "args"},
#ifdef ENABLE_VM_OPCODE_PROFILER
    {"--profile-opcodes", "Rank the most executed GML opcodes", NULL},
#endif
    {"--lazy-textures", "Load textures into VRAM on first use", NULL},
    {"--load-type", "Specify how data.win is loaded", "type"},
    {"--disable-log-colours", "Disable colours for warning, error, and debug logs", NULL},
    {"--disable-log-colors", "Same as --disable-log-colours", NULL},
};

static char* duplicateString(const char* value) {
    size_t length = strlen(value) + 1;
    char* copy = (char*) malloc(length);
    if (copy != NULL) memcpy(copy, value, length);
    return copy;
}

static void getTerminalSize(int* width, int* height) {
    *width = 80;
    *height = 24;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(output, &info)) {
        *width = info.srWindow.Right - info.srWindow.Left + 1;
        *height = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
#else
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0) {
        *width = size.ws_col;
        *height = size.ws_row;
    }
#endif
}

static int readKey(void) {
#ifdef _WIN32
    int key = _getch();
    if (key == 0 || key == 224) {
        key = _getch();
        if (key == 72) return 'k';
        if (key == 80) return 'j';
        return 0;
    }
    if (key == 13) return '\n';
    return key;
#else
    struct termios original;
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &original) != 0) return -1;
    raw = original;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return -1;

    unsigned char input;
    int key = read(STDIN_FILENO, &input, 1) == 1 ? input : -1;
    if (key == '\033') {
        fd_set inputSet;
        struct timeval timeout = {0, 50000};
        unsigned char sequence[2];
        FD_ZERO(&inputSet);
        FD_SET(STDIN_FILENO, &inputSet);
        if (select(STDIN_FILENO + 1, &inputSet, NULL, NULL, &timeout) > 0 && read(STDIN_FILENO, &sequence[0], 1) == 1) {
            if (sequence[0] != '[') {
                key = 0;
            } else {
                FD_ZERO(&inputSet);
                FD_SET(STDIN_FILENO, &inputSet);
                timeout.tv_sec = 0;
                timeout.tv_usec = 50000;
                if (select(STDIN_FILENO + 1, &inputSet, NULL, NULL, &timeout) > 0 && read(STDIN_FILENO, &sequence[1], 1) == 1) {
                    switch (sequence[1]) {
                        case 'A': key = 'k'; break;
                        case 'B': key = 'j'; break;
                        case 'C': key = 'l'; break;
                        case 'D': key = 'h'; break;
                        default: key = 0; break;
                    }
                } else {
                    key = 0;
                }
            }
        }
    }
    if (key == '\r') key = '\n';
    tcsetattr(STDIN_FILENO, TCSANOW, &original);
    return key;
#endif
}

static void drawLine(const char* text, int width) {
    int length = (int) strlen(text);
    if (length > width) length = width;
    fwrite(text, 1, (size_t) length, stdout);
    for (int i = length; i < width; i++) putchar(' ');
    putchar('\n');
}

static void drawFrame(const char* title, const char* footer) {
    int width, height;
    int i;
    getTerminalSize(&width, &height);
    width = width > 1 ? width - 1 : 1;
    if (height < 5) height = 5;
    printf("\033[H\033[2J");
    drawLine("Butterscotch", width);
    for (i = 0; i < width; i++) putchar('=');
    putchar('\n');
    drawLine(title, width);
    for (i = 0; i < height - 6; i++) putchar('\n');
    for (i = 0; i < width; i++) putchar('=');
    putchar('\n');
    drawLine(footer, width);
    fflush(stdout);
}

static void drawGameMenu(const LauncherGame* games, int gameCount, int selected, int firstVisible) {
    int width, height;
    int i;
    int row;
    getTerminalSize(&width, &height);
    width = width > 1 ? width - 1 : 1;
    if (height < 8) height = 8;
    int visible = height - 6;
    printf("\033[H\033[2J");
    drawLine("Butterscotch", width);
    for (i = 0; i < width; i++) putchar('=');
    putchar('\n');
    drawLine("Select a game.", width);
    for (row = 0; row < visible; row++) {
        int index = firstVisible + row;
        if (gameCount == 0 && row == 0) {
            drawLine("No games added. Press 1 to add a game.", width);
        } else if (index < gameCount) {
            char line[4600];
            snprintf(line, sizeof(line), "%c %d. %s (%s)", index == selected ? '>' : ' ', index + 1, games[index].name, games[index].path);
            drawLine(line, width);
        } else {
            putchar('\n');
        }
    }
    for (i = 0; i < width; i++) putchar('=');
    putchar('\n');
    drawLine("Arrow Keys: Previous game, Next game  ||  1: Add a game  ||  2: Exit  ||  Enter: Run selected game", width);
    fflush(stdout);
}

static void drawOptionsMenu(int selected, int firstVisible, const bool* enabled, const char* const* values) {
    int width, height;
    int i;
    int row;
    getTerminalSize(&width, &height);
    width = width > 1 ? width - 1 : 1;
    if (height < 8) height = 8;
    int optionCount = (int) (sizeof(launcherOptions) / sizeof(launcherOptions[0]));
    int visible = height - 6;
    printf("\033[H\033[2J");
    drawLine("Butterscotch", width);
    for (i = 0; i < width; i++) putchar('=');
    putchar('\n');
    drawLine("Select launch flags.", width);
    for (row = 0; row < visible; row++) {
        int index = firstVisible + row;
        if (index < optionCount) {
            char line[1024];
            snprintf(line, sizeof(line), "%c [%c] %-36s %s%s%s", index == selected ? '>' : ' ', enabled[index] ? 'x' : ' ', launcherOptions[index].flag, launcherOptions[index].description,
                values[index] == NULL ? "" : " = ", values[index] == NULL ? "" : values[index]);
            drawLine(line, width);
        } else {
            putchar('\n');
        }
    }
    for (i = 0; i < width; i++) putchar('=');
    putchar('\n');
    drawLine("Up/Down: Select  ||  Space: Toggle  ||  Enter: Run  ||  Esc: Back", width);
    fflush(stdout);
}

static bool makeConfigPath(const char* executablePath, char* configPath, size_t capacity) {
    char executable[LAUNCHER_PATH_CAPACITY];
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, executable, sizeof(executable));
    if (length == 0 || length >= sizeof(executable)) {
        snprintf(executable, sizeof(executable), "%s", executablePath);
    }
#else
#ifdef __linux__
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length >= 0) executable[length] = '\0';
    else
#endif
    {
        if (realpath(executablePath, executable) == NULL) {
            snprintf(executable, sizeof(executable), "%s", executablePath);
        }
    }
#endif
    char* slash = strrchr(executable, '/');
#ifdef _WIN32
    char* backslash = strrchr(executable, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
#endif
    if (slash != NULL) *slash = '\0';
    else snprintf(executable, sizeof(executable), ".");
    const char* filename = "/butterscotch-games.txt";
    size_t directoryLength = strlen(executable);
    size_t filenameLength = strlen(filename);
    if (directoryLength + filenameLength + 1 > capacity) return false;
    memcpy(configPath, executable, directoryLength);
    memcpy(configPath + directoryLength, filename, filenameLength + 1);
    return true;
}

static bool saveGames(const char* configPath, const LauncherGame* games, int gameCount) {
    int i;
    FILE* file = fopen(configPath, "w");
    if (file == NULL) return false;
    bool saved = true;
    for (i = 0; i < gameCount; i++) {
        if (fprintf(file, "%s\t%s\n", games[i].name, games[i].path) < 0) saved = false;
    }
    if (fclose(file) != 0) saved = false;
    return saved;
}

static bool loadGames(const char* configPath, LauncherGame** games, int* gameCount) {
    FILE* file = fopen(configPath, "r");
    if (file == NULL) return false;
    char line[4608];
    while (fgets(line, sizeof(line), file) != NULL) {
        char* separator = strchr(line, '\t');
        if (separator == NULL) continue;
        *separator++ = '\0';
        separator[strcspn(separator, "\r\n")] = '\0';
        if (line[0] == '\0' || separator[0] == '\0') continue;
        LauncherGame game;
        snprintf(game.name, sizeof(game.name), "%.*s", (int) sizeof(game.name) - 1, line);
        snprintf(game.path, sizeof(game.path), "%s", separator);
        LauncherGame* expanded = (LauncherGame*) realloc(*games, (size_t) (*gameCount + 1) * sizeof(**games));
        if (expanded == NULL) {
            fclose(file);
            return false;
        }
        *games = expanded;
        (*games)[(*gameCount)++] = game;
    }
    fclose(file);
    return true;
}

static bool promptLine(const char* prompt, char* buffer, size_t capacity) {
    printf("\033[H\033[2JButterscotch\n\n%s", prompt);
    fflush(stdout);
    if (fgets(buffer, (int) capacity, stdin) == NULL) return false;
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return true;
}

#ifdef ENABLE_SCREENSHOTS
static int findOptionIndex(const char* flag) {
    int optionCount = (int) (sizeof(launcherOptions) / sizeof(launcherOptions[0]));
    int i;
    for (i = 0; i < optionCount; i++) {
        if (strcmp(launcherOptions[i].flag, flag) == 0) return i;
    }
    return -1;
}

static bool ensureScreenshotTarget(bool* enabled, char** values, const char* frameFlag, const char* targetFlag) {
    int frameIndex = findOptionIndex(frameFlag);
    int targetIndex = findOptionIndex(targetFlag);
    if (frameIndex < 0 || targetIndex < 0 || !enabled[frameIndex] || enabled[targetIndex]) return true;
    char prompt[128];
    char filename[1024];
    snprintf(prompt, sizeof(prompt), "Screenshot filename for %s: ", frameFlag);
    if (!promptLine(prompt, filename, sizeof(filename)) || filename[0] == '\0') return false;
    values[targetIndex] = duplicateString(filename);
    if (values[targetIndex] == NULL) return false;
    enabled[targetIndex] = true;
    return true;
}
#endif

static bool addGame(LauncherGame** games, int* gameCount, const char* configPath) {
    LauncherGame game;
    if (!promptLine("Game name: ", game.name, sizeof(game.name)) || game.name[0] == '\0') return false;
    if (!promptLine("Path to data.win (or game folder): ", game.path, sizeof(game.path)) || game.path[0] == '\0') return false;
    if (strchr(game.name, '\t') != NULL || strchr(game.name, '\n') != NULL || strchr(game.path, '\t') != NULL) return false;
    LauncherGame* expanded = (LauncherGame*) realloc(*games, (size_t) (*gameCount + 1) * sizeof(**games));
    if (expanded == NULL) return false;
    *games = expanded;
    (*games)[(*gameCount)++] = game;
    if (!saveGames(configPath, *games, *gameCount)) {
        (*gameCount)--;
        drawFrame("Could not save the game list.", "Press any key to return.");
        readKey();
        return false;
    }
    return true;
}

static bool resolveDataWinPath(const char* path, char* resolved, size_t capacity) {
    int i;
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
#else
    struct stat info;
    if (stat(path, &info) != 0) return false;
    if (!S_ISDIR(info.st_mode)) {
#endif
        size_t pathLength = strlen(path);
        if (pathLength >= capacity) return false;
        memcpy(resolved, path, pathLength + 1);
        return true;
    }
    static const char* candidates[] = {"data.win", "assets/game.unx", "assets/game.droid", "../Resources/game.ios"};
    for (i = 0; i < (int) (sizeof(candidates) / sizeof(candidates[0])); i++) {
        size_t pathLength = strlen(path);
        size_t candidateLength = strlen(candidates[i]);
        if (pathLength + candidateLength + 2 > capacity) continue;
        memcpy(resolved, path, pathLength);
        resolved[pathLength] = '/';
        memcpy(resolved + pathLength + 1, candidates[i], candidateLength + 1);
    #ifdef _WIN32
        if (_access(resolved, 0) == 0) return true;
    #else
        if (access(resolved, F_OK) == 0) return true;
    #endif
    }
    return false;
}

static bool selectOptions(char*** arguments, int* argumentCount, const char* executablePath, const char* gamePath) {
    int optionCount = (int) (sizeof(launcherOptions) / sizeof(launcherOptions[0]));
    int i;
    bool enabled[sizeof(launcherOptions) / sizeof(launcherOptions[0])] = {false};
    char* values[sizeof(launcherOptions) / sizeof(launcherOptions[0])] = {NULL};
    int selected = 0;
    int firstVisible = 0;
    bool launch = false;
    while (true) {
        drawOptionsMenu(selected, firstVisible, enabled, (const char* const*) values);
        int key = readKey();
        if (key == 'q' || key == 27) break;
        if (key == 'k' && selected > 0) selected--;
        else if (key == 'j' && selected + 1 < optionCount) selected++;
        else if (key == ' ') {
            enabled[selected] = !enabled[selected];
            if (enabled[selected] && launcherOptions[selected].valueName != NULL) {
                char prompt[128];
                char input[1024];
                snprintf(prompt, sizeof(prompt), "%s for %s: ", launcherOptions[selected].valueName, launcherOptions[selected].flag);
                if (!promptLine(prompt, input, sizeof(input)) || input[0] == '\0') {
                    enabled[selected] = false;
                } else {
                    values[selected] = duplicateString(input);
                    if (values[selected] == NULL) enabled[selected] = false;
                }
            } else if (!enabled[selected]) {
                free(values[selected]);
                values[selected] = NULL;
            }
        } else if (key == '\n') {
            launch = true;
            break;
        }
        int width, height;
        getTerminalSize(&width, &height);
        int visible = height > 6 ? height - 6 : 1;
        if (selected < firstVisible) firstVisible = selected;
        if (selected >= firstVisible + visible) firstVisible = selected - visible + 1;
    }

#ifdef ENABLE_SCREENSHOTS
    if (launch && (!ensureScreenshotTarget(enabled, values, "--screenshot-at-frame", "--screenshot") ||
                   !ensureScreenshotTarget(enabled, values, "--screenshot-surfaces-at-frame", "--screenshot-surfaces"))) {
        launch = false;
    }
#endif

    if (launch) {
        int count = 2;
        for (i = 0; i < optionCount; i++) {
            if (!enabled[i]) continue;
            if (values[i] == NULL || strcmp(launcherOptions[i].flag, "--widescreen-hack") == 0) count++;
            else count += 2;
        }
        char** argv = (char**) calloc((size_t) count + 1, sizeof(*argv));
        if (argv != NULL) {
            int index = 0;
            argv[index++] = (char*) executablePath;
            argv[index++] = duplicateString(gamePath);
            for (i = 0; i < optionCount; i++) {
                if (!enabled[i]) continue;
                if (strcmp(launcherOptions[i].flag, "--widescreen-hack") == 0 && values[i] != NULL) {
                    size_t length = strlen(launcherOptions[i].flag) + strlen(values[i]) + 2;
                    argv[index] = (char*) malloc(length);
                    if (argv[index] != NULL) snprintf(argv[index], length, "%s=%s", launcherOptions[i].flag, values[i]);
                    index++;
                } else {
                    argv[index++] = duplicateString(launcherOptions[i].flag);
                    if (values[i] != NULL) argv[index++] = duplicateString(values[i]);
                }
            }
            bool valid = true;
            for (i = 1; i < index; i++) if (argv[i] == NULL) valid = false;
            if (valid) {
                *arguments = argv;
                *argumentCount = index;
            } else {
                freeLauncherMenuArguments(argv, index);
                launch = false;
            }
        } else {
            launch = false;
        }
    }

    for (i = 0; i < optionCount; i++) free(values[i]);
    return launch;
}

bool runLauncherMenu(const char* executablePath, char*** arguments, int* argumentCount) {
#ifdef _WIN32
    if (!_isatty(_fileno(stdin)) || !_isatty(_fileno(stdout))) {
        fprintf(stderr, "The game menu requires an interactive terminal.\n");
        return false;
    }
#else
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "The game menu requires an interactive terminal.\n");
        return false;
    }
#endif
    char configPath[LAUNCHER_PATH_CAPACITY];
    if (!makeConfigPath(executablePath, configPath, sizeof(configPath))) {
        fprintf(stderr, "The launcher path is too long to save the game list.\n");
        return false;
    }
    LauncherGame* games = NULL;
    int gameCount = 0;
    if (!loadGames(configPath, &games, &gameCount)) {
        free(games);
        games = NULL;
        gameCount = 0;
    }
    int selected = 0;
    int firstVisible = 0;
    bool launched = false;
#ifdef _WIN32
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD originalConsoleMode = 0;
    bool restoreConsoleMode = GetConsoleMode(output, &originalConsoleMode) != 0;
    if (restoreConsoleMode) SetConsoleMode(output, originalConsoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
    fputs("\033[?1049h\033[?25l", stdout);

    while (true) {
        drawGameMenu(games, gameCount, selected, firstVisible);
        int key = readKey();
        if (key == '2' || key == 'q' || key == 27) break;
        if ((key == 'k' || key == 'h') && selected > 0) selected--;
        else if ((key == 'j' || key == 'l') && selected + 1 < gameCount) selected++;
        else if (key == '1') {
            if (addGame(&games, &gameCount, configPath)) selected = gameCount - 1;
        } else if (key == '\n' && gameCount > 0) {
            char gamePath[LAUNCHER_PATH_CAPACITY];
            if (!resolveDataWinPath(games[selected].path, gamePath, sizeof(gamePath))) {
                drawFrame("The selected path does not contain a supported data.win file.", "Press any key to return.");
                readKey();
                continue;
            }
            if (selectOptions(arguments, argumentCount, executablePath, gamePath)) {
                launched = true;
                break;
            }
        }
        int width, height;
        getTerminalSize(&width, &height);
        int visible = height > 6 ? height - 6 : 1;
        if (selected < firstVisible) firstVisible = selected;
        if (selected >= firstVisible + visible) firstVisible = selected - visible + 1;
    }

    fputs("\033[?25h\033[?1049l", stdout);
    fflush(stdout);
#ifdef _WIN32
    if (restoreConsoleMode) SetConsoleMode(output, originalConsoleMode);
#endif
    free(games);
    return launched;
}

void freeLauncherMenuArguments(char** arguments, int argumentCount) {
    if (arguments == NULL) return;
    for (int i = 1; i < argumentCount; i++) free(arguments[i]);
    free(arguments);
}