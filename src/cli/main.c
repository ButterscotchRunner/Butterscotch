#include "args.h"
#include "menu.h"
#include <loop.h>
#include <getopt.h>
#if defined(__linux__) || defined(__APPLE__)
#include <limits.h>
#include <spawn.h>
#include <sys/types.h>
#include <unistd.h>
extern char **environ;
#endif

/* For SDL_main */
#if defined(USE_SDL1)
#include <SDL/SDL_main.h>
#elif defined(USE_SDL2)
#include <SDL2/SDL_main.h>
#elif defined(USE_SDL3)
#include <SDL3/SDL_main.h>
#endif

static bool logColour;

#if defined(__linux__) || defined(__APPLE__)
static bool launchMenuInTerminal(const char* executablePath) {
#ifdef __linux__
    static const struct {
        const char* name;
        const char* executeOption;
    } terminals[] = {
        {"konsole", "-e"},
        {"gnome-terminal", "--"},
        {"kgx", "--"},
        {"xfce4-terminal", "--execute"},
        {"x-terminal-emulator", "-e"},
        {"xterm", "-e"},
        {"alacritty", "-e"},
        {"kitty", "--"},
        {"foot", "-e"},
    };
#else
    char workingDirectory[PATH_MAX];
    char executable[PATH_MAX];
    pid_t process;
#endif
#ifdef __linux__
    if (getenv("DISPLAY") == NULL && getenv("WAYLAND_DISPLAY") == NULL) {
        fprintf(stderr, "No interactive terminal is available for the game menu.\n");
        return false;
    }
#endif

#ifdef __linux__
    char executable[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length >= 0) {
        executable[length] = '\0';
    } else {
        snprintf(executable, sizeof(executable), "%s", executablePath);
    }
#else
    if (getcwd(workingDirectory, sizeof(workingDirectory)) == NULL || realpath(executablePath, executable) == NULL) {
        fprintf(stderr, "Could not resolve the launcher path.\n");
        return false;
    }
#endif

#ifdef __linux__
    for (size_t i = 0; i < sizeof(terminals) / sizeof(terminals[0]); i++) {
        char* arguments[5];
        arguments[0] = (char*) terminals[i].name;
        arguments[1] = (char*) terminals[i].executeOption;
        arguments[2] = executable;
        arguments[3] = "--launcher-menu";
        arguments[4] = NULL;
        pid_t process;
        if (posix_spawnp(&process, terminals[i].name, NULL, NULL, arguments, environ) == 0) return true;
    }
    fprintf(stderr, "Could not find a supported terminal emulator for the game menu.\n");
    return false;
#else
    char* arguments[] = {
        "/usr/bin/osascript",
        "-e", "on run argv",
        "-e", "set shellCommand to \"cd \" & quoted form of (item 1 of argv) & \" && exec \" & quoted form of (item 2 of argv) & \" --launcher-menu\"",
        "-e", "tell application \"Terminal\" to do script shellCommand",
        "-e", "end run",
        workingDirectory,
        executable,
        NULL
    };
    if (posix_spawn(&process, "/usr/bin/osascript", NULL, NULL, arguments, environ) == 0) return true;
    fprintf(stderr, "Could not open Terminal.app for the game menu.\n");
    return false;
#endif
}
#endif

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

    char** menuArguments = NULL;
    int menuArgumentCount = 0;
    bool openMenu = argc == 1 || (argc == 2 && strcmp(argv[1], "--launcher-menu") == 0);
    if (openMenu) {
#if defined(__linux__) || defined(__APPLE__)
        if (argc == 1 && (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))) {
            return launchMenuInTerminal(argv[0]) ? 0 : 1;
        }
#endif
        if (!runLauncherMenu(argv[0], &menuArguments, &menuArgumentCount)) return 0;
        argv = menuArguments;
        argc = menuArgumentCount;
    }

    CommandLineArgs args;
    parseCommandLineArgs(&args, argc, argv);
    logColour = !args.disableLogColours;
    int ret = loop(args, argv[0]);
    freeCommandLineArgs(&args);
    freeLauncherMenuArguments(menuArguments, menuArgumentCount);
    return ret;
}
