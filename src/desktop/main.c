#include "data_win.h"
#include "vm.h"

#include "platformdefs.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif
#ifdef __GLIBC__
#include <malloc.h>
#ifdef __GLIBC_PREREQ
#if __GLIBC_PREREQ(2, 33)
#define HAVE_MALLINFO2
#endif
#endif
#endif

#include "runner_keyboard.h"
#include "runner.h"
#include "input_recording.h"
#include "debug_overlay.h"
#if defined(ENABLE_LEGACY_GL) || defined(ENABLE_MODERN_GL) || ((defined(USE_GLFW3) || defined(USE_GLFW2)) && defined(ENABLE_SW_RENDERER) )
#include <glad/glad.h>
#endif
#if defined(ENABLE_LEGACY_GL) || defined(ENABLE_MODERN_GL)
#include "gl_renderer.h"
#ifdef ENABLE_LEGACY_GL
#include "gl_legacy_renderer.h"
#endif
#endif
#ifdef ENABLE_SW_RENDERER
#include "sw_renderer.h"
#endif
#include "overlay_file_system.h"
#if defined(USE_OPENAL)
#include "al_audio_system.h"
#elif defined(USE_MINIAUDIO)
#include "ma_audio_system.h"
#endif
#include "noop_audio_system.h"
#include "stb_ds.h"
#include "stb_image_write.h"

#include "utils.h"
#include "profiler.h"

/* For SDL_main */
#if defined(USE_SDL1)
#include <SDL/SDL_main.h>
#elif defined(USE_SDL2)
#include <SDL2/SDL_main.h>
#endif

enum GraphicsAPI gfx;

#if !defined(ENABLE_GLES) && (defined(ENABLE_MODERN_GL) || defined(ENABLE_LEGACY_GL))
static void APIENTRY glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, MAYBE_UNUSED GLsizei length, const GLchar* message, MAYBE_UNUSED const void* userParam) {
    const char* sourceStr;
    switch (source) {
        case GL_DEBUG_SOURCE_API: sourceStr = "API"; break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM: sourceStr = "Window System"; break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: sourceStr = "Shader Compiler"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY: sourceStr = "Third Party"; break;
        case GL_DEBUG_SOURCE_APPLICATION: sourceStr = "Application"; break;
        case GL_DEBUG_SOURCE_OTHER: sourceStr = "Other"; break;
        default: sourceStr = "Unknown"; break;
    }

    const char* typeStr;
    switch (type) {
        case GL_DEBUG_TYPE_ERROR: typeStr = "Error"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeStr = "Deprecated Behaviour"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: typeStr = "Undefined Behaviour"; break;
        case GL_DEBUG_TYPE_PORTABILITY: typeStr = "Portability"; break;
        case GL_DEBUG_TYPE_PERFORMANCE: typeStr = "Performance"; break;
        case GL_DEBUG_TYPE_MARKER: typeStr = "Marker"; break;
        case GL_DEBUG_TYPE_PUSH_GROUP: typeStr = "Push Group"; break;
        case GL_DEBUG_TYPE_POP_GROUP: typeStr = "Pop Group"; break;
        case GL_DEBUG_TYPE_OTHER: typeStr = "Other"; break;
        default: typeStr = "Unknown"; break;
    }

    const char* severityStr;
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH: severityStr = "High"; break;
        case GL_DEBUG_SEVERITY_MEDIUM: severityStr = "Medium"; break;
        case GL_DEBUG_SEVERITY_LOW: severityStr = "Low"; break;
        case GL_DEBUG_SEVERITY_NOTIFICATION: severityStr = "Notification"; break;
        default: severityStr = "Unknown"; break;
    }

    fprintf(stderr, "[OpenGL %s] id=%u Type: %s; Severity: %s; Message: %.*s\n", sourceStr, id, typeStr, severityStr, (int) length, message);
}

static void installGLDebugCallback(void) {
    if (!GLAD_GL_KHR_debug) {
        fprintf(stderr, "OpenGL debug callback not available (driver does not expose GL_KHR_debug)\n");
        return;
    }

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallbackKHR(glDebugCallback, nullptr);
    glDebugMessageControlKHR(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
}
#endif

// ===[ COMMAND LINE ARGUMENTS ]===
typedef struct {
    int key;
    // We need this dummy value, think that the ds_map is like a Java HashMap NOT a HashSet
    // (Which is funny, because in Java HashSets are backed by HashMaps lol)
    bool value;
} FrameSetEntry;

typedef struct {
    const char* dataWinPath;
    const char* saveFolder; // null = default to the directory containing dataWinPath
    const char* screenshotPattern;
    FrameSetEntry* screenshotFrames;
    const char* screenshotSurfacesPattern;
    FrameSetEntry* screenshotSurfacesFrames;
    FrameSetEntry* dumpFrames;
    FrameSetEntry* dumpJsonFrames;
    const char* dumpJsonFilePattern;
    StringBooleanEntry* varReadsToBeTraced;
    StringBooleanEntry* varWritesToBeTraced;
    StringBooleanEntry* functionCallsToBeTraced;
    StringBooleanEntry* alarmsToBeTraced;
    StringBooleanEntry* instanceLifecyclesToBeTraced;
    StringBooleanEntry* eventsToBeTraced;
    StringBooleanEntry* collisionsToBeTraced;
    StringBooleanEntry* opcodesToBeTraced;
    StringBooleanEntry* stackToBeTraced;
    StringBooleanEntry* disassemble;
    StringBooleanEntry* tilesToBeTraced;
    bool alwaysLogUnknownFunctions;
    bool alwaysLogStubbedFunctions;
    bool headless;
    bool traceFrames;
    bool printRooms;
    bool printObjects;
    bool printDeclaredFunctions;
    bool printUnknownFunctions;
    int exitAtFrame;
    int traceBytecodeAfterFrame;
    double speedMultiplier;
    double fastForwardSpeed;
    int seed;
    bool hasSeed;
    bool debug;
    bool traceEventInherited;
    const char* recordInputsPath;
    const char* playbackInputsPath;
    const char* renderer;
    YoYoOperatingSystem osType;
    int32_t windowWidth, windowHeight; // 0 = auto (gen8 default, or the console-native size for console os-types)
    float widescreenAspect; // "widescreen hack" target aspect ratio (width/height), 0 = disabled
    char** gameArgs; // stb_ds array of owned strings, gameArgs[0] = runner executable path
    bool lazyRooms;
    StringBooleanEntry* eagerRooms; // stb_ds string-keyed set of room names
    bool lazyTextures;
    DataWinLoadType loadType;
    int profilerFramesBetween; // 0 = disabled
#ifdef ENABLE_VM_OPCODE_PROFILER
    bool opcodeProfiler;
#endif
} CommandLineArgs;

typedef struct { const char* name; YoYoOperatingSystem value; } OsTypeNameEntry;

static const OsTypeNameEntry OS_TYPE_NAMES[] = {
    {"unknown",       OS_UNKNOWN},
    {"windows",       OS_WINDOWS},
    {"win32",         OS_WINDOWS},
    {"macosx",        OS_MACOSX},
    {"macos",         OS_MACOSX},
    {"psp",           OS_PSP},
    {"ios",           OS_IOS},
    {"android",       OS_ANDROID},
    {"symbian",       OS_SYMBIAN},
    {"linux",         OS_LINUX},
    {"winphone",      OS_WINPHONE},
    {"tizen",         OS_TIZEN},
    {"win8native",    OS_WIN8NATIVE},
    {"wiiu",          OS_WIIU},
    {"3ds",           OS_3DS},
    {"psvita",        OS_PSVITA},
    {"bb10",          OS_BB10},
    {"ps4",           OS_PS4},
    {"xboxone",       OS_XBOXONE},
    {"ps3",           OS_PS3},
    {"xbox360",       OS_XBOX360},
    {"uwp",           OS_UWP},
    {"amazon",        OS_AMAZON},
    {"switch",        OS_SWITCH},
};
#define OS_TYPE_NAMES_COUNT (sizeof(OS_TYPE_NAMES)/sizeof(OS_TYPE_NAMES[0]))

static bool parseOsTypeArg(const char* s, YoYoOperatingSystem* out) {
    forEach(const OsTypeNameEntry, entry, OS_TYPE_NAMES, OS_TYPE_NAMES_COUNT) {
        if (strcmp(s, entry->name) == 0) {
            *out = entry->value;
            return true;
        }
    }
    return false;
}

static void printOsTypeNames(FILE* out) {
    forEachIndexed(const OsTypeNameEntry, entry, i, OS_TYPE_NAMES, OS_TYPE_NAMES_COUNT) {
        fprintf(out, "%s%s", i > 0 ? ", " : "", entry->name);
    }
}

// Resolves the window size for the specified operating system.
// The "--window-size" argument takes precedence over the default resolution for each platform.
static void resolveWindowSize(const CommandLineArgs* args, uint32_t gen8Width, uint32_t gen8Height, int32_t* outW, int32_t* outH) {
    if (args->windowWidth > 0 && args->windowHeight > 0) {
        *outW = args->windowWidth;
        *outH = args->windowHeight;
        return;
    }

    switch (args->osType) {
        case OS_PS4:
        case OS_XBOXONE:
        case OS_PS3:
        case OS_XBOX360:
            *outW = 1920;
            *outH = 1080;
            break;
        case OS_SWITCH:
            *outW = 1280;
            *outH = 720;
            break;
        case OS_PSVITA:
            *outW = 960;
            *outH = 544;
            break;
        default:
            *outW = (int32_t) gen8Width;
            *outH = (int32_t) gen8Height;
            break;
    }

    // Widescreen hack handling to grow the window size to match
    if (args->widescreenAspect > 0.0f && *outW > 0 && *outH > 0) {
        float nativeAspect = (float) *outW / (float) *outH;
        if (args->widescreenAspect > nativeAspect) {
            int widened = (int) ((float) *outH * args->widescreenAspect + 0.5f);
            if (widened > *outW) *outW = widened;
        } else if (args->widescreenAspect < nativeAspect) {
            int heightened = (int) ((float) *outW / args->widescreenAspect + 0.5f);
            if (heightened > *outH) *outH = heightened;
        }
    }
}

// Extracts the Runner arguments from a string, returning the values on stb_ds array
// The "Runner arguments" is used for the "--game-args" and for the game_change GML function
// Returns the modified array
static char** extractRunnerArguments(char* rawArguments) {
    // The "saveptr" is used for strtok_r to store its state
    // So it is thread safe™
    char *saveptr;
    // We create a copy because strtok_r completely obliterates the original char buffer
    char* copy = safeStrdup(rawArguments);
    char* token = strtok_r(copy, " \t\r\n", &saveptr);
    char** array = nullptr;

    while (token != nullptr) {
        arrput(array, safeStrdup(token));
        token = strtok_r(nullptr, " \t\r\n", &saveptr);
    }

    free(copy);

    return array;
}

static void parseCommandLineArgs(CommandLineArgs* args, int argc, char* argv[]) {
    memset(args, 0, sizeof(CommandLineArgs));

    static struct option longOptions[] = {
        {"screenshot",          required_argument, nullptr, 's'},
        {"screenshot-at-frame", required_argument, nullptr, 'f'},
        {"screenshot-surfaces", required_argument, nullptr, 'U'},
        {"screenshot-surfaces-at-frame", required_argument, nullptr, 'V'},
        {"headless",            no_argument,       nullptr, 'h'},
        {"print-rooms", no_argument,               nullptr, 'r'},
        {"print-objects", no_argument,             nullptr, 'b'},
        {"print-declared-functions", no_argument,  nullptr, 'p'},
        {"print-unknown-functions", no_argument, nullptr, 'u'},
        {"trace-variable-reads", required_argument,  nullptr, 'R'},
        {"trace-variable-writes", required_argument, nullptr, 'W'},
        {"trace-function-calls", required_argument,         nullptr, 'c'},
        {"trace-alarms", required_argument,         nullptr, 'a'},
        {"trace-instance-lifecycles", required_argument,         nullptr, 'l'},
        {"trace-events", required_argument,         nullptr, 'e'},
        {"trace-collisions", required_argument,     nullptr, 'C'},
        {"trace-event-inherited", no_argument, nullptr, 'E'},
        {"trace-tiles", required_argument, nullptr, 'T'},
        {"trace-opcodes", required_argument,       nullptr, 'o'},
        {"trace-stack", required_argument,         nullptr, 'S'},
        {"trace-frames", no_argument, nullptr, 'k'},
        {"always-log-unknown-functions", no_argument, nullptr, 'y'},
        {"always-log-stubbed-functions", no_argument, nullptr, 'Y'},
        {"exit-at-frame", required_argument, nullptr, 'x'},
        {"trace-bytecode-after-frame", required_argument, nullptr, 'F'},
        {"dump-frame", required_argument, nullptr, 'd'},
        {"dump-frame-json", required_argument, nullptr, 'j'},
        {"dump-frame-json-file", required_argument, nullptr, 'J'},
        {"speed", required_argument, nullptr, 'M'},
        {"fast-forward-speed", required_argument, nullptr, 'X'},
        {"seed", required_argument, nullptr, 'Z'},
        {"debug", no_argument, nullptr, 'D'},
        {"disassemble", required_argument, nullptr, 'A'},
        {"record-inputs", required_argument, nullptr, 'I'},
        {"playback-inputs", required_argument, nullptr, 'P'},
        {"renderer", required_argument, nullptr, 'g'},
        {"lazy-rooms", no_argument, nullptr, 'z'},
        {"eager-room", required_argument, nullptr, 'G'},
        {"os-type", required_argument, nullptr, 'O'},
        {"window-size", required_argument, nullptr, 'w'},
        {"widescreen-hack", optional_argument, nullptr, 1000},
        {"profile-gml-scripts", required_argument, nullptr, 'q'},
        {"save-folder", required_argument, nullptr, 'B'},
        {"game-args", required_argument, nullptr, 'N'},
        {"lazy-textures", no_argument, nullptr, 'L'},
        {"load-type", required_argument, nullptr, 999},
#ifdef ENABLE_VM_OPCODE_PROFILER
        {"profile-opcodes", no_argument, nullptr, 'Q'},
#endif
        {nullptr,               0,                 nullptr,  0 }
    };

    args->screenshotFrames = nullptr;
    args->exitAtFrame = -1;
    args->traceBytecodeAfterFrame = 0;
    args->speedMultiplier = 1.0;
    args->fastForwardSpeed = 0.0;
    args->osType = OS_WINDOWS;
    args->profilerFramesBetween = 0;
    args->loadType = DATAWINLOADTYPE_LOAD_IN_MEMORY_AHEAD_OF_TIME;
    // TODO: detect available driver features
    // at runtime to improve defaults.
#if defined(ENABLE_MODERN_GL) && (defined(USE_GLFW3) || defined(USE_SDL2))
    args->renderer = "modern-gl";
#elif defined(ENABLE_LEGACY_GL)
    args->renderer = "legacy-gl";
#else
    args->renderer = "software";
#endif

    int opt;
    while ((opt = getopt_long(argc, argv, "", longOptions, nullptr)) != -1) {
        switch (opt) {
            case 's':
                args->screenshotPattern = optarg;
                break;
            case 'f': {
                char* endPtr;
                int frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s'\n", optarg);
                    exit(1);
                }

                hmput(args->screenshotFrames, frame, true);
                break;
            }
            case 'U':
                args->screenshotSurfacesPattern = optarg;
                break;
            case 'V': {
                char* endPtr;
                int frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --screenshot-surfaces-at-frame\n", optarg);
                    exit(1);
                }
                hmput(args->screenshotSurfacesFrames, frame, true);
                break;
            }
            case 'h':
                args->headless = true;
                break;
            case 'r':
                args->printRooms = true;
                break;
            case 'b':
                args->printObjects = true;
                break;
            case 'p':
                args->printDeclaredFunctions = true;
                break;
            case 'u':
                args->printUnknownFunctions = true;
                break;
            case 'R':
                shput(args->varReadsToBeTraced, optarg, true);
                break;
            case 'W':
                shput(args->varWritesToBeTraced, optarg, true);
                break;
            case 'c':
                shput(args->functionCallsToBeTraced, optarg, true);
                break;
            case 'a':
                shput(args->alarmsToBeTraced, optarg, true);
                break;
            case 'l':
                shput(args->instanceLifecyclesToBeTraced, optarg, true);
                break;
            case 'L':
                args->lazyTextures = true;
                break;
            case 'e':
                shput(args->eventsToBeTraced, optarg, true);
                break;
            case 'C':
                shput(args->collisionsToBeTraced, optarg, true);
                break;
            case 'o':
                shput(args->opcodesToBeTraced, optarg, true);
                break;
            case 'S':
                shput(args->stackToBeTraced, optarg, true);
                break;
            case 'k':
                args->traceFrames = true;
                break;
            case 'y':
                args->alwaysLogUnknownFunctions = true;
                break;
            case 'Y':
                args->alwaysLogStubbedFunctions = true;
                break;
            case 'x': {
                char* endPtr;
                int frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --exit-at-frame\n", optarg);
                    exit(1);
                }
                args->exitAtFrame = frame;
                break;
            }
            case 'F': {
                char* endPtr;
                int frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --trace-bytecode-after-frame\n", optarg);
                    exit(1);
                }
                args->traceBytecodeAfterFrame = frame;
                break;
            }
            case 'd': {
                char* endPtr;
                int frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --dump-frame\n", optarg);
                    exit(1);
                }
                hmput(args->dumpFrames, frame, true);
                break;
            }
            case 'j': {
                char* endPtr;
                int frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --dump-frame-json\n", optarg);
                    exit(1);
                }
                hmput(args->dumpJsonFrames, frame, true);
                break;
            }
            case 'J':
                args->dumpJsonFilePattern = optarg;
                break;
            case 'M': {
                char* endPtr;
                double speed = strtod(optarg, &endPtr);
                if (*endPtr != '\0' || speed <= 0.0) {
                    fprintf(stderr, "Error: Invalid speed multiplier '%s' for --speed (must be > 0)\n", optarg);
                    exit(1);
                }
                args->speedMultiplier = speed;
                break;
            }
            case 'X': {
                char* endPtr;
                double speed = strtod(optarg, &endPtr);
                if (*endPtr != '\0' || speed <= 0.0) {
                    fprintf(stderr, "Error: Invalid speed '%s' for --fast-forward-speed (must be > 0)\n", optarg);
                    exit(1);
                }
                args->fastForwardSpeed = speed;
                break;
            }
            case 'D':
                args->debug = true;
                break;
            case 'g':
                args->renderer = optarg;
                break;
            case 'z':
                args->lazyRooms = true;
                break;
            case 'G':
                shput(args->eagerRooms, optarg, true);
                break;
            case 'A':
                shput(args->disassemble, optarg, true);
                break;
            case 'T':
                shput(args->tilesToBeTraced, optarg, true);
                break;
            case 'E':
                args->traceEventInherited = true;
                break;
            case 'Z': {
                char* endPtr;
                int seedVal = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0') {
                    fprintf(stderr, "Error: Invalid seed value '%s' for --seed\n", optarg);
                    exit(1);
                }
                args->seed = seedVal;
                args->hasSeed = true;
                break;
            }
            case 'I':
                args->recordInputsPath = optarg;
                break;
            case 'P':
                args->playbackInputsPath = optarg;
                break;
            case 'q': {
                char* endPtr;
                int framesBetween = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || framesBetween <= 0) {
                    fprintf(stderr, "Error: Invalid frame count '%s' for --profile-gml-scripts (must be > 0)\n", optarg);
                    exit(1);
                }
                args->profilerFramesBetween = framesBetween;
                break;
            }
            case 'B':
                args->saveFolder = optarg;
                break;
            case 'N': {
                repeat(arrlen(args->gameArgs), i) {
                    free(args->gameArgs[i]);
                }
                arrfree(args->gameArgs);
                args->gameArgs = extractRunnerArguments(optarg);
                break;
            }
#ifdef ENABLE_VM_OPCODE_PROFILER
            case 'Q':
                args->opcodeProfiler = true;
                break;
#endif
            case 'O':
                if (!parseOsTypeArg(optarg, &args->osType)) {
                    fprintf(stderr, "Error: Invalid --os-type value '%s' (expected: ", optarg);
                    printOsTypeNames(stderr);
                    fprintf(stderr, ")\n");
                    exit(1);
                }
                break;
            case 'w': {
                int32_t w = 0, h = 0;
                if (sscanf(optarg, "%dx%d", &w, &h) != 2 || 0 >= w || 0 >= h) {
                    fprintf(stderr, "Error: Invalid --window-size value '%s' (expected WxH, e.g. 960x544)\n", optarg);
                    exit(1);
                }
                args->windowWidth = w;
                args->windowHeight = h;
                break;
            }
            case 999: {
                if (strcmp(optarg, "load-in-memory-ahead-of-time") == 0) {
                    args->loadType = DATAWINLOADTYPE_LOAD_IN_MEMORY_AHEAD_OF_TIME;
                } else if (strcmp(optarg, "load-per-chunk") == 0) {
                    args->loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;
                } else {
                    fprintf(stderr, "Error: Unknown load type '%s'\n", optarg);
                    exit(1);
                }
                break;
            }
            case 1000: {
                if (optarg == nullptr) {
                    args->widescreenAspect = 16.0f / 9.0f;
                    break;
                }
                int aw = 0, ah = 0;
                double ratio = 0.0;
                char* endPtr;
                if (sscanf(optarg, "%d:%d", &aw, &ah) == 2 && aw > 0 && ah > 0) {
                    args->widescreenAspect = (float) aw / (float) ah;
                } else if ((ratio = strtod(optarg, &endPtr)), *endPtr == '\0' && ratio > 0.0) {
                    args->widescreenAspect = (float) ratio;
                } else {
                    fprintf(stderr, "Error: Invalid --widescreen-hack value '%s' (expected W:H like 16:9, or a decimal like 1.7778)\n", optarg);
                    exit(1);
                }
                break;
            }
            default:
                fprintf(stderr, "Usage: %s <path to data.win or game.unx>\n", argv[0]);
                exit(1);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Usage: %s <path to data.win or game.unx>\n", argv[0]);
        exit(1);
    }

    args->dataWinPath = argv[optind];

    if (hmlen(args->screenshotFrames) > 0 && args->screenshotPattern == nullptr) {
        fprintf(stderr, "Error: --screenshot-at-frame requires --screenshot to be set\n");
        exit(1);
    }

    if (hmlen(args->screenshotSurfacesFrames) > 0 && args->screenshotSurfacesPattern == nullptr) {
        fprintf(stderr, "Error: --screenshot-surfaces-at-frame requires --screenshot-surfaces to be set\n");
        exit(1);
    }

    if (args->headless && args->speedMultiplier != 1.0) {
        fprintf(stderr, "You can't set the speed multiplier while running in headless mode! Headless mode always run in real time\n");
        exit(1);
    }
}

static void freeCommandLineArgs(CommandLineArgs* args) {
    hmfree(args->screenshotFrames);
    hmfree(args->screenshotSurfacesFrames);
    hmfree(args->dumpFrames);
    hmfree(args->dumpJsonFrames);
    shfree(args->varReadsToBeTraced);
    shfree(args->varWritesToBeTraced);
    shfree(args->functionCallsToBeTraced);
    shfree(args->alarmsToBeTraced);
    shfree(args->instanceLifecyclesToBeTraced);
    shfree(args->eventsToBeTraced);
    shfree(args->collisionsToBeTraced);
    shfree(args->opcodesToBeTraced);
    shfree(args->stackToBeTraced);
    shfree(args->disassemble);
    shfree(args->tilesToBeTraced);
    repeat(arrlen(args->gameArgs), i) free(args->gameArgs[i]);
    arrfree(args->gameArgs);
}

// ===[ SCREENSHOT ]===
// Reads the contents of an FBO (use 0 for the default framebuffer) into a PNG file.
// If forceOpaque is true, the alpha channel is overwritten with 255, fixing any clobbering done by blending modes.
#if defined(ENABLE_LEGACY_GL) || defined(ENABLE_MODERN_GL)
static void writeFramebufferAsPng(GLuint fbo, int width, int height, const char* filename, const char* logPrefix, bool forceOpaque) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);

    int stride = width * 4;
    unsigned char* pixels = safeMalloc(stride * height);
    if (pixels == nullptr) {
        fprintf(stderr, "Error: Failed to allocate memory for %s (%dx%d)\n", logPrefix, width, height);
        return;
    }

    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    if (forceOpaque) {
        int totalPixels = width * height;
        repeat(totalPixels, i) pixels[i * 4 + 3] = 255;
    }

    // OpenGL reads bottom-to-top, but PNG is top-to-bottom.
    // Use stb's negative stride trick: point to the last row and use a negative stride to flip vertically.
    unsigned char* lastRow = pixels + (height - 1) * stride;
    stbi_write_png(filename, width, height, 4, lastRow, -stride);

    free(pixels);
    printf("%s: %s (%dx%d)\n", logPrefix, filename, width, height);
}

static void captureScreenshot(GLuint fbo, const char* filenamePattern, int frameNumber, int width, int height) {
    char filename[512];
    snprintf(filename, sizeof(filename), filenamePattern, frameNumber);
    writeFramebufferAsPng(fbo, width, height, filename, "Screenshot saved", true);
}

// Dumps every live surface in the GL renderer as a PNG.
// Filename pattern takes two %d slots: frame number, then surface ID.
static void dumpAllSurfaces(GLRenderer* gl, const char* filenamePattern, int frameNumber) {
    repeat(gl->surfaceCount, surfaceId) {
        if (gl->surfaces[surfaceId] == 0)
            continue;

        int width = gl->surfaceWidth[surfaceId];
        int height = gl->surfaceHeight[surfaceId];
        if (0 >= width || 0 >= height) continue;

        char filename[512];
        snprintf(filename, sizeof(filename), filenamePattern, frameNumber, (int) surfaceId);
        writeFramebufferAsPng(gl->surfaces[surfaceId], width, height, filename, "Surface dump", false);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}
#endif

// ===[ KEYBOARD INPUT ]===

InputRecording* globalInputRecording = nullptr;

#if defined(__has_feature)
    #if __has_feature(address_sanitizer)
        #define BUTTERSCOTCH_HAS_ASAN 1
    #endif
#endif
#if defined(__SANITIZE_ADDRESS__)
    #define BUTTERSCOTCH_HAS_ASAN 1
#endif

#if BUTTERSCOTCH_HAS_ASAN
void __asan_set_death_callback(void (*callback)(void));
#endif

static volatile sig_atomic_t crashSaveInProgress = 0;

static void saveRecordingOnCrash(void) {
    if (crashSaveInProgress) return;
    crashSaveInProgress = 1;
    if (globalInputRecording != nullptr && globalInputRecording->isRecording) {
        InputRecording_save(globalInputRecording);
    }
}

static void crashSignalHandler(int sig) {
    saveRecordingOnCrash();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void installCrashHandlers(void) {
#if BUTTERSCOTCH_HAS_ASAN
    __asan_set_death_callback(saveRecordingOnCrash);
#endif
    signal(SIGSEGV, crashSignalHandler);
    signal(SIGABRT, crashSignalHandler);
#ifdef SIGBUS
    signal(SIGBUS,  crashSignalHandler);
#endif
    signal(SIGFPE,  crashSignalHandler);
    signal(SIGILL,  crashSignalHandler);
}

void saveInputRecording() {
    // Save input recording if active, then free
    if (globalInputRecording != nullptr) {
        if (globalInputRecording->isRecording) {
            InputRecording_save(globalInputRecording);
        }
        InputRecording_free(globalInputRecording);
        globalInputRecording = nullptr;
    }
}

#ifndef _WIN32
typedef struct { int key; struct sigaction value; } PreviousSignalActionEntry;
static PreviousSignalActionEntry* previousSignalActions = nullptr;

static void onCrashSignal(int sig) {
    saveInputRecording();
    // Restore the previous handler (ASAN) and re-raise so it can report the fault
    ssize_t idx = hmgeti(previousSignalActions, sig);
    sigaction(sig, &previousSignalActions[idx].value, nullptr);
    raise(sig);
}
#endif

// ===[ MAIN ]===
int main(int argc, char* argv[]) {
    setbuf(stderr, NULL);
#ifdef _WIN32
    timeBeginPeriod(1);
#endif

    CommandLineArgs args;
    parseCommandLineArgs(&args, argc, argv);

    char* currentDataWinPath = safeStrdup(args.dataWinPath);
    char** currentGameArgs = args.gameArgs;
    repeat(arrlen(args.gameArgs), i) {
        arrput(currentGameArgs, args.gameArgs[i]);
    }
    // The first argument will ALWAYS be the argv[0]
    arrins(currentGameArgs, 0, safeStrdup(argv[0]));

    bool platformInitialized = false;

    while (true) {
        printf("Loading %s...\n", args.dataWinPath);

        DataWinParserOptions options = {0};
        options.parseGen8 = true;
        options.parseOptn = true;
        options.parseLang = true;
        options.parseExtn = true;
        options.parseSond = true;
        options.parseAgrp = true;
        options.parseSprt = true;
        options.parseBgnd = true;
        options.parsePath = true;
        options.parseScpt = true;
        options.parseGlob = true;
        options.parseShdr = true;
        options.parseFont = true;
        options.parseTmln = true;
        options.parseObjt = true;
        options.parseRoom = true;
        options.parseTpag = true;
        options.parseCode = true;
        options.parseVari = true;
        options.parseFunc = true;
        options.parseStrg = true;
        options.parseTxtr = true;
        options.parseAudo = true;
        options.skipLoadingPreciseMasksForNonPreciseSprites = true;
        options.loadType = args.loadType;
        options.lazyLoadRooms = args.lazyRooms;
        options.eagerlyLoadedRooms = args.eagerRooms;
        DataWin* dataWin = DataWin_parse(currentDataWinPath, options);

        Gen8* gen8 = &dataWin->gen8;
        printf("Loaded \"%s\" (%d) successfully! [WAD Version %u / GameMaker version %u.%u.%u.%u]\n", gen8->name, gen8->gameID, gen8->wadVersion, dataWin->detectedFormat.major, dataWin->detectedFormat.minor, dataWin->detectedFormat.release, dataWin->detectedFormat.build);

#ifdef HAVE_MALLINFO2
        {
            struct mallinfo2 mi = mallinfo2();
            printf("Memory after data.win parsing: used=%zu bytes (%.1f KB)\n", mi.uordblks, mi.uordblks / 1024.0f);
        }
#endif

        // Build window title
        char windowTitle[256];
        snprintf(windowTitle, sizeof(windowTitle), "Butterscotch - %s", gen8->displayName);

        // Initialize VM
        VMContext* vm = VM_create(dataWin);

        Profiler_setEnabled(&vm->profiler, args.profilerFramesBetween > 0);
#ifdef ENABLE_VM_OPCODE_PROFILER
        vm->opcodeProfilerEnabled = args.opcodeProfiler;
        if (vm->opcodeProfilerEnabled) {
            vm->opcodeVariantCounts = safeCalloc(256 * 256, sizeof(uint64_t));
            vm->opcodeRValueTypeCounts = safeCalloc(256 * 256, sizeof(uint64_t));
        }
#endif

        if (args.hasSeed) {
            srand((unsigned int) args.seed);
            vm->hasFixedSeed = true;
            printf("Using fixed RNG seed: %d\n", args.seed);
        }

        if (args.printRooms) {
            // Under --lazy-rooms we load each room for display and then free it again so the dump
            // reflects what each room contains without keeping all of them resident simultaneously.
            forEachIndexed(Room, room, idx, dataWin->room.rooms, dataWin->room.count) {
                if (!room->present) {
                    printf("[%d] <absent>\n", idx);
                    continue;
                }
                bool loadedHere = false;
                if (!room->payloadLoaded) {
                    DataWin_loadRoomPayload(dataWin, (int32_t) idx);
                    loadedHere = true;
                }

                printf("[%d] %s ()\n", idx, room->name);

                forEachIndexed(RoomGameObject, roomGameObject, idx2, room->gameObjects, room->gameObjectCount) {
                    if (roomGameObject->objectDefinition < 0 || (uint32_t) roomGameObject->objectDefinition >= dataWin->objt.count) {
                        printf("  [%d] <no object> (x=%d,y=%d)\n", idx2, roomGameObject->x, roomGameObject->y);
                        continue;
                    }
                    GameObject* gameObject = &dataWin->objt.objects[roomGameObject->objectDefinition];
                    printf(
                        "  [%d] %s (x=%d,y=%d,persistent=%d,solid=%d,spriteId=%d,preCreateCode=%d,creationCode=%d)\n",
                        idx2,
                        gameObject->name,
                        roomGameObject->x,
                        roomGameObject->y,
                        gameObject->persistent,
                        gameObject->solid,
                        gameObject->spriteId,
                        roomGameObject->preCreateCode,
                        roomGameObject->creationCode
                    );
                }

                if (loadedHere && !room->eagerlyLoaded) {
                    DataWin_freeRoomPayload(room);
                }
            }
            VM_free(vm);
            DataWin_free(dataWin);
            return 0;
        }

        if (args.printObjects) {
            forEachIndexed(GameObject, obj, idx, dataWin->objt.objects, dataWin->objt.count) {
                uint32_t totalEvents = 0;
                repeat(OBJT_EVENT_TYPE_COUNT, e) {
                    totalEvents += obj->eventLists[e].eventCount;
                }
                printf("[%u] %s:\n", idx, obj->name);
                if (obj->parentId >= 0 && (uint32_t) obj->parentId < dataWin->objt.count) {
                    printf("  Parent: %s (%d)\n", dataWin->objt.objects[obj->parentId].name, obj->parentId);
                } else {
                    printf("  Parent: none\n");
                }
                if (obj->spriteId >= 0 && (uint32_t) obj->spriteId < dataWin->sprt.count) {
                    printf("  Sprite: %s (%d)\n", dataWin->sprt.sprites[obj->spriteId].name, obj->spriteId);
                } else {
                    printf("  Sprite: none\n");
                }
                printf("  Solid: %d\n", obj->solid);
                printf("  Persistent: %d\n", obj->persistent);
                printf("  Visible: %d\n", obj->visible);
                printf("  Depth: %d\n", obj->depth);
                printf("  Events (%u):\n", totalEvents);
                repeat(OBJT_EVENT_TYPE_COUNT, e) {
                    ObjectEventList* list = &obj->eventLists[e];
                    repeat(list->eventCount, eIdx) {
                        ObjectEvent* event = &list->events[eIdx];
                        const char* eventName = Runner_getEventName((int32_t) e, (int32_t) event->eventSubtype);
                        int32_t codeId = -1;
                        if (event->actionCount > 0) codeId = event->actions[0].codeId;
                        printf("    %s:\n", eventName);
                        printf("      Sub Type: %u\n", event->eventSubtype);
                        printf("      Code ID: %d\n", codeId);
                        printf("      Actions: %u\n", event->actionCount);
                    }
                }
            }
            VM_free(vm);
            DataWin_free(dataWin);
            return 0;
        }

        if (args.printDeclaredFunctions) {
            repeat(hmlen(vm->codeIndexByName), i) {
                printf("[%d] %s\n", vm->codeIndexByName[i].value, vm->codeIndexByName[i].key);
            }
            VM_free(vm);
            DataWin_free(dataWin);
            return 0;
        }

        if (args.printUnknownFunctions) {
            uint32_t unimplementedCount = 0;
            fprintf(stderr, "Unknown Functions:\n");
            repeat(dataWin->func.functionCount, i) {
                const char* name = dataWin->func.functions[i].name;
                if (name == nullptr)
                    continue;

                // Implemented as a user script/code entry?
                if (shgeti(vm->codeIndexByName, (char*) name) >= 0)
                    continue;

                // Implemented as a registered builtin?
                if (VM_findBuiltin(vm, name) != nullptr)
                    continue;

                fprintf(stderr, "- %s\n", name);
                unimplementedCount++;
            }

            if (unimplementedCount == 0) {
                fprintf(stderr, "All %u referenced functions are implemented! :3\n", dataWin->func.functionCount);
            } else {
                fprintf(stderr, "%u unknown function(s) out of %u referenced\n", unimplementedCount, dataWin->func.functionCount);
            }
            VM_free(vm);
            DataWin_free(dataWin);
            return 0;
        }

        if (shlen(args.disassemble) > 0) {
            VM_buildCrossReferences(vm);
            if (shgeti(args.disassemble, "*") >= 0) {
                repeat(dataWin->code.count, i) {
                    VM_disassemble(vm, (int32_t) i);
                }
            } else {
                for (ptrdiff_t i = 0; shlen(args.disassemble) > i; i++) {
                    const char* name = args.disassemble[i].key;
                    ptrdiff_t idx = shgeti(vm->codeIndexByName, (char*) name);
                    if (idx >= 0) {
                        VM_disassemble(vm, vm->codeIndexByName[idx].value);
                    } else {
                        fprintf(stderr, "Error: Script '%s' not found in funcMap\n", name);
                    }
                }
            }
            VM_free(vm);
            DataWin_free(dataWin);
            freeCommandLineArgs(&args);
            return 0;
        }

        // Initialize the file system
        char* dataWinDir = nullptr;
        {
            const char* lastSlash = strrchr(args.dataWinPath, '/');
            const char* lastBackslash = strrchr(args.dataWinPath, '\\');
            if (lastBackslash != nullptr && (lastSlash == nullptr || lastBackslash > lastSlash))
                lastSlash = lastBackslash;
            if (lastSlash != nullptr) {
                size_t len = (size_t) (lastSlash - args.dataWinPath + 1);
                dataWinDir = safeMalloc(len + 1);
                memcpy(dataWinDir, args.dataWinPath, len);
                dataWinDir[len] = '\0';
            } else {
                dataWinDir = safeStrdup("./");
            }
        }
        const char* savePath = args.saveFolder != nullptr ? args.saveFolder : dataWinDir;
        OverlayFileSystem* overlayFs = OverlayFileSystem_create(dataWinDir, savePath);
        free(dataWinDir);

        if (strcmp(args.renderer, "modern-gl") == 0)
            gfx = MODERN_GL;
        else if (strcmp(args.renderer, "legacy-gl") == 0)
            gfx = LEGACY_GL;
        else if (strcmp(args.renderer, "software") == 0)
            gfx = SOFTWARE;
        else {
            fprintf(stderr, "Unknown renderer: %s!\n", args.renderer);
            return 1;
        }

#ifndef ENABLE_LEGACY_GL
        if (gfx == LEGACY_GL) {
            fprintf(stderr, "The legacy gl renderer is not available in this build!\n");
            return 0;
        }
#endif
#ifndef ENABLE_MODERN_GL
        if (gfx == MODERN_GL) {
            fprintf(stderr, "The modern gl renderer is not available in this build!\n");
            return 0;
        }
#endif
#ifndef ENABLE_SW_RENDERER
        if (gfx == SOFTWARE) {
            fprintf(stderr, "The software renderer is not available in this build!\n");
            return 0;
        }
#endif

        if (gfx != MODERN_GL && hmlen(args.screenshotSurfacesFrames)) {
            fprintf(stderr, "You can only use --screenshot-surfaces with the modern gl renderer!\n");
            return 0;
        }


        int32_t windowW, windowH;
        resolveWindowSize(&args, gen8->defaultWindowWidth, gen8->defaultWindowHeight, &windowW, &windowH);

        if (!platformInitialized) {
            if (!platformInit(windowW, windowH, windowTitle, args.headless)) {
                DataWin_free(dataWin);
                freeCommandLineArgs(&args);
                return 1;
            }

#if defined(ENABLE_LEGACY_GL) || defined(ENABLE_MODERN_GL) || ((defined(USE_GLFW3) || defined(USE_GLFW2)) && defined(ENABLE_SW_RENDERER) )
#if defined(USE_GLFW3) || defined(USE_GLFW2)
            if (gfx == LEGACY_GL || gfx == MODERN_GL || gfx == SOFTWARE) {
#else
            if (gfx == LEGACY_GL || gfx == MODERN_GL) {
#endif
                // Load OpenGL function pointers via GLAD
#ifdef ENABLE_GLES
                if (!gladLoadGLES2Loader((GLADloadproc)platformGetProcAddress)) {
#else
                if (!gladLoadGLLoader((GLADloadproc)platformGetProcAddress)) {
#endif
                    fprintf(stderr, "Failed to initialize GLAD\n");
                    platformExit();
                    DataWin_free(dataWin);
                    freeCommandLineArgs(&args);
                    return 1;
                }
            }
#endif

            // Install the OpenGL debug message callback
#if !defined(ENABLE_GLES) && (defined(ENABLE_MODERN_GL) || defined(ENABLE_LEGACY_GL))
            if (gfx == MODERN_GL)
                installGLDebugCallback();
#endif

            platformInitialized = true;
        } else {
            // game_change path: reuse the existing window/GL context, just retitle and resize for the new game.
            platformSetWindowTitle(gen8->displayName);
            platformSetWindowSize(windowW, windowH);
        }

        // Initialize the renderer
        Renderer* renderer = nullptr;
#ifdef ENABLE_SW_RENDERER
        if (gfx == SOFTWARE)
            renderer = SWRenderer_create();
#endif
#ifdef ENABLE_LEGACY_GL
        if (gfx == LEGACY_GL)
            renderer = GLLegacyRenderer_create();
#endif
#ifdef ENABLE_MODERN_GL
        if (gfx == MODERN_GL)
            renderer = GLRenderer_create();
#endif
        if (!renderer) {
            fprintf(stderr, "Failed to initialize a renderer\n");
            platformExit();
            DataWin_free(dataWin);
            freeCommandLineArgs(&args);
            return 1;
        }

        // Initialize the audio system
        AudioSystem* audioSystem = nullptr;
        if (args.headless) {
            audioSystem = (AudioSystem*) NoopAudioSystem_create();
        } else {
#if defined(USE_OPENAL)
            audioSystem = (AudioSystem*) AlAudioSystem_create();
#elif defined(USE_MINIAUDIO)
            audioSystem = (AudioSystem*) MaAudioSystem_create();
#else
            audioSystem = (AudioSystem*) NoopAudioSystem_create();
#endif
        }

        // Initialize the runner
        Runner* runner = Runner_create(dataWin, vm, renderer, (FileSystem*) overlayFs, audioSystem);

        if (!args.lazyTextures) {
            repeat(runner->dataWin->txtr.count, i) {
#ifdef ENABLE_MODERN_GL
                if (gfx == MODERN_GL)
                    GLRenderer_ensureTextureLoaded((GLRenderer*) renderer, (int32_t) i);
#endif

#ifdef ENABLE_LEGACY_GL
                if (gfx == LEGACY_GL)
                    GLLegacyRenderer_ensureTextureLoaded((GLLegacyRenderer*) renderer, (int32_t) i);
#endif
            }
        }
        runner->debugMode = args.debug;
        runner->osType = args.osType;
        runner->setWindowSize = platformSetWindowSize;
        runner->getWindowSize = platformGetWindowSize;
        runner->setWindowTitle = platformSetWindowTitle;
        Runner_setGameArgs(runner, currentGameArgs, (int32_t) arrlen(currentGameArgs));
        platformInitFunctions(runner);

        // Set up input recording/playback (both can be active: playback then continue recording)
        if (args.playbackInputsPath != nullptr) {
            globalInputRecording = InputRecording_createPlayer(args.playbackInputsPath, args.recordInputsPath);
        } else if (args.recordInputsPath != nullptr) {
            globalInputRecording = InputRecording_createRecorder(args.recordInputsPath);
        }
        if (globalInputRecording != nullptr) {
            globalInputRecording->filterDebugKeys = args.debug;
            installCrashHandlers();
        }
        shcopyFromTo(args.varReadsToBeTraced, runner->vmContext->varReadsToBeTraced);
        shcopyFromTo(args.varWritesToBeTraced, runner->vmContext->varWritesToBeTraced);
        shcopyFromTo(args.functionCallsToBeTraced, runner->vmContext->functionCallsToBeTraced);
        shcopyFromTo(args.alarmsToBeTraced, runner->vmContext->alarmsToBeTraced);
        shcopyFromTo(args.instanceLifecyclesToBeTraced, runner->vmContext->instanceLifecyclesToBeTraced);
        shcopyFromTo(args.eventsToBeTraced, runner->vmContext->eventsToBeTraced);
        shcopyFromTo(args.collisionsToBeTraced, runner->vmContext->collisionsToBeTraced);
        shcopyFromTo(args.opcodesToBeTraced, runner->vmContext->opcodesToBeTraced);
        shcopyFromTo(args.stackToBeTraced, runner->vmContext->stackToBeTraced);
        shcopyFromTo(args.tilesToBeTraced, runner->vmContext->tilesToBeTraced);
        runner->vmContext->traceBytecodeAfterFrame = args.traceBytecodeAfterFrame;
        runner->vmContext->alwaysLogUnknownFunctions = args.alwaysLogUnknownFunctions;
        runner->vmContext->alwaysLogStubbedFunctions = args.alwaysLogStubbedFunctions;
        runner->vmContext->traceEventInherited = args.traceEventInherited;

#ifndef _WIN32
        struct sigaction sa = {0};
        sa.sa_handler = onCrashSignal;
        sigemptyset(&sa.sa_mask);
        struct sigaction prev;
        sigaction(SIGABRT, &sa, &prev);
        PreviousSignalActionEntry p;
        p.key = SIGABRT;
        p.value = prev;
        hmputs(previousSignalActions, p);
        sigaction(SIGSEGV, &sa, &prev);
        PreviousSignalActionEntry p2;
        p.key = SIGSEGV;
        p.value = prev;
        hmputs(previousSignalActions, p2);
#endif

        // Initialize the first room and fire Game Start / Room Start events
        Runner_initFirstRoom(runner);

        // Main loop
        bool debugPaused = false;
        bool debugShowCollisionMasks = false;
        bool freeCamActive = false;
        bool actuallyShuttingDown = false;
        double lastFrameTime = platformGetTime();
        double lastFrameStartTime = platformGetTime(); // for delta_time
        bool shouldWindowClose = false;
        while (true) {
            if (runner->shouldExit || shouldWindowClose) {
                actuallyShuttingDown = true;
                break;
            }

            if (runner->pendingWorkingDirectory != nullptr && runner->pendingLaunchParameters != nullptr) {
                // Break from the game loop, we'll handle this later
                break;
            }

            double frameStartNow = platformGetTime();
            runner->deltaTime = (frameStartNow - lastFrameStartTime) * 1000000.0;
            lastFrameStartTime = frameStartNow;

            // Clear last frame's pressed/released state, then poll new input events
            RunnerKeyboard_beginFrame(runner->keyboard);
            RunnerGamepad_beginFrame(runner->gamepads);
            RunnerMouse_beginFrame(runner->mouse);
            if (platformHandleEvents()) {
                shouldWindowClose = true;
                continue;
            }

            // Debug key bindings
            if (runner->debugMode) {
                // Pause
                if (RunnerKeyboard_checkPressed(runner->keyboard, 'P')) {
                    debugPaused = !debugPaused;
                    fprintf(stderr, "Debug: %s\n", debugPaused ? "Paused" : "Resumed");
                }
            }

            // Run the game step if the game is paused
            bool shouldStep = true;
            if (runner->debugMode && debugPaused) {
                shouldStep = RunnerKeyboard_checkPressed(runner->keyboard, 'O');
                if (shouldStep) fprintf(stderr, "Debug: Frame advance (frame %d)\n", runner->frameCount);
            }

            double frameStartTime = 0;

            if (shouldStep) {
                if (args.traceFrames) {
                    frameStartTime = platformGetTime();
                    fprintf(stderr, "Frame %d (Start)\n", runner->frameCount);
                }

                // Process input recording/playback (must happen after platformHandleEvents, before Runner_step)
                InputRecording_processFrame(globalInputRecording, runner->keyboard, runner->frameCount);

                // Go to next room
                if (RunnerKeyboard_checkPressed(runner->keyboard, VK_PAGEUP)) {
                    DataWin* dw = runner->dataWin;
                    if ((int32_t) dw->gen8.roomOrderCount > runner->currentRoomOrderPosition + 1) {
                        int32_t nextIdx = dw->gen8.roomOrder[runner->currentRoomOrderPosition + 1];
                        runner->pendingRoom = nextIdx;
                        runner->audioSystem->vtable->stopAll(runner->audioSystem);
                        fprintf(stderr, "Debug: Going to next room -> %s\n", dw->room.rooms[nextIdx].name);
                    }
                }

                // Go to previous room
                if (RunnerKeyboard_checkPressed(runner->keyboard, VK_PAGEDOWN)) {
                    DataWin* dw = runner->dataWin;
                    if (runner->currentRoomOrderPosition > 0) {
                        int32_t prevIdx = dw->gen8.roomOrder[runner->currentRoomOrderPosition - 1];
                        runner->pendingRoom = prevIdx;
                        runner->audioSystem->vtable->stopAll(runner->audioSystem);
                        fprintf(stderr, "Debug: Going to previous room -> %s\n", dw->room.rooms[prevIdx].name);
                    }
                }

                // Dump runner state to console
                if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F12)) {
                    fprintf(stderr, "Debug: Dumping runner state at frame %d\n", runner->frameCount);
                    Runner_dumpState(runner);
                }

                if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F11)) {
                    fprintf(stderr, "Debug: Dumping runner state at frame %d\n", runner->frameCount);
                    char* json = Runner_dumpStateJson(runner);

                    if (args.dumpJsonFilePattern != nullptr) {
                        char filename[512];
                        snprintf(filename, sizeof(filename), args.dumpJsonFilePattern, runner->frameCount);
                        FILE* f = fopen(filename, "w");
                        if (f != nullptr) {
                            fwrite(json, 1, strlen(json), f);
                            fputc('\n', f);
                            fclose(f);
                            printf("JSON dump saved: %s\n", filename);
                        } else {
                            fprintf(stderr, "Error: Could not write JSON dump to '%s'\n", filename);
                        }
                    } else {
                        printf("%s\n", json);
                    }

                    free(json);
                }

                // Toggle the collision mask debug overlay
                if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F2)) {
                    debugShowCollisionMasks = !debugShowCollisionMasks;
                    fprintf(stderr, "Debug: Collision mask overlay %s!\n", debugShowCollisionMasks ? "enabled" : "disabled");
                }

                // Enable free cam
                if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F3)) {
                    runner->freeCamPanX = 0.0f;
                    runner->freeCamPanY = 0.0f;
                    runner->freeCamZoom = 1.0f;

                    freeCamActive = !freeCamActive;
                    fprintf(stderr, "Debug: Free cam %s!\n", freeCamActive ? "enabled" : "disabled");
                }

                if (freeCamActive) {
                    if (RunnerKeyboard_check(runner->keyboard, VK_UP)) {
                        runner->freeCamPanY -= (float) (0.000005f * runner->deltaTime);
                    }

                    if (RunnerKeyboard_check(runner->keyboard, VK_DOWN)) {
                        runner->freeCamPanY += (float) (0.000005f * runner->deltaTime);
                    }

                    if (RunnerKeyboard_check(runner->keyboard, VK_LEFT)) {
                        runner->freeCamPanX -= (float) (0.000005f * runner->deltaTime);
                    }

                    if (RunnerKeyboard_check(runner->keyboard, VK_RIGHT)) {
                        runner->freeCamPanX += (float) (0.000005f * runner->deltaTime);
                    }
                }

                // Reset global interact state because I HATE when I get stuck while moving through rooms
                if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F10)) {
                    int32_t interactVarId = shget(runner->vmContext->globalVarNameMap, "interact");

                    runner->vmContext->globalVars[interactVarId] = RValue_makeInt32(0);
                    printf("Changed global.interact [%d] value!\n", interactVarId);
                }

                bool* currentKeyDown = safeCalloc(GML_KEY_COUNT, sizeof(bool));
                bool* currentKeyPressed = safeCalloc(GML_KEY_COUNT, sizeof(bool));
                bool* currentKeyReleased = safeCalloc(GML_KEY_COUNT, sizeof(bool));

                if (freeCamActive) {
                    // THIS IS A HACK!! We don't want to pass keys to the runner, but we DO want to keep it so we can hold the arrow keys to move the camera
                    memcpy(currentKeyDown, runner->keyboard->keyDown, sizeof(runner->keyboard->keyDown));
                    memcpy(currentKeyPressed, runner->keyboard->keyPressed, sizeof(runner->keyboard->keyPressed));
                    memcpy(currentKeyReleased, runner->keyboard->keyReleased, sizeof(runner->keyboard->keyReleased));

                    memset(runner->keyboard->keyDown, 0, sizeof(runner->keyboard->keyDown));
                    memset(runner->keyboard->keyPressed, 0, sizeof(runner->keyboard->keyPressed));
                    memset(runner->keyboard->keyReleased, 0, sizeof(runner->keyboard->keyReleased));
                }

                // Run one game step (Begin Step, Keyboard, Alarms, Step, End Step, room transitions)
                Runner_step(runner);

                if (freeCamActive) {
                    memcpy(runner->keyboard->keyDown, currentKeyDown, sizeof(runner->keyboard->keyDown));
                    memcpy(runner->keyboard->keyPressed, currentKeyPressed, sizeof(runner->keyboard->keyPressed));
                    memcpy(runner->keyboard->keyReleased, currentKeyReleased, sizeof(runner->keyboard->keyReleased));
                }

                free(currentKeyDown);
                free(currentKeyPressed);
                free(currentKeyReleased);

                if (args.profilerFramesBetween > 0 && runner->frameCount > 0 && runner->frameCount % args.profilerFramesBetween == 0) {
                    char* profilerReport = Profiler_createReport(vm->profiler, 20, args.profilerFramesBetween);
                    if (profilerReport != nullptr) {
                        fprintf(stderr, "%s\n", profilerReport);
                        free(profilerReport);
                    }
                    Profiler_reset(vm->profiler);
                }

                // Update audio system (gain fading, cleanup ended sounds)
                float dt = (float) (runner->deltaTime / 1000000.0);
                if (0.0f > dt) dt = 0.0f;
                if (dt > 0.1f) dt = 0.1f; // cap delta to avoid huge fades on lag spikes
                runner->audioSystem->vtable->update(runner->audioSystem, dt);

                // Dump full runner state if this frame was requested
                if (hmget(args.dumpFrames, runner->frameCount)) {
                    Runner_dumpState(runner);
                }

                // Dump runner state as JSON if this frame was requested
                if (hmget(args.dumpJsonFrames, runner->frameCount)) {
                    char* json = Runner_dumpStateJson(runner);
                    if (args.dumpJsonFilePattern != nullptr) {
                        char filename[512];
                        snprintf(filename, sizeof(filename), args.dumpJsonFilePattern, runner->frameCount);
                        FILE* f = fopen(filename, "w");
                        if (f != nullptr) {
                            fwrite(json, 1, strlen(json), f);
                            fputc('\n', f);
                            fclose(f);
                            printf("JSON dump saved: %s\n", filename);
                        } else {
                            fprintf(stderr, "Error: Could not write JSON dump to '%s'\n", filename);
                        }
                    } else {
                        printf("%s\n", json);
                    }
                    free(json);
                }

                // Clear the default framebuffer (window background) to black
#ifdef ENABLE_SW_RENDERER
                if (gfx == SOFTWARE)
                    SWRenderer_clearFrameBuffer(renderer, 0);
#endif
#if defined(ENABLE_LEGACY_GL) || defined(ENABLE_MODERN_GL)
                if (gfx == LEGACY_GL || gfx == MODERN_GL) {
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glClear(GL_COLOR_BUFFER_BIT);
                }
#endif

                // Query actual framebuffer size
                int32_t fbWidth, fbHeight;
                platformGetWindowSize(&fbWidth, &fbHeight);

                if (!runner->appSurfaceEnabled) {
                    runner->applicationWidth = fbWidth;
                    runner->applicationHeight = fbHeight;
                    runner->usingAppSurface = false;
                } else {
                    if (runner->applicationWidth <= 0 || runner->applicationHeight <= 0) {
                        runner->applicationWidth = (int32_t) gen8->defaultWindowWidth;
                        runner->applicationHeight = (int32_t) gen8->defaultWindowHeight;
                    }
                    runner->usingAppSurface = true;
                }

                int32_t gameW = runner->applicationWidth;
                int32_t gameH = runner->applicationHeight;

                // Widescreen hack: render into a surface grown toward the requested aspect to fake a different aspect
                // ratio. The game's logical applicationWidth/Height is left untouched (so the reads above stay the real
                // size and this never compounds frame-to-frame); only the local gameW/gameH used for the projection/FBO
                // grow. A wider-than-native target grows width (reveal left/right); a taller one grows height (reveal
                // top/bottom). Runner_drawViews reads widescreenExtraWidth/Height to expand each view to match.
                runner->widescreenExtraWidth = 0;
                runner->widescreenExtraHeight = 0;
                if (args.widescreenAspect > 0.0f && runner->usingAppSurface && gameW > 0 && gameH > 0) {
                    float nativeAspect = (float) gameW / (float) gameH;
                    if (args.widescreenAspect > nativeAspect) {
                        int32_t targetW = (int32_t) ((float) gameH * args.widescreenAspect + 0.5f);
                        if (targetW > gameW) {
                            runner->widescreenExtraWidth = targetW - gameW;
                            gameW = targetW;
                        }
                    } else if (args.widescreenAspect < nativeAspect) {
                        int32_t targetH = (int32_t) ((float) gameW / args.widescreenAspect + 0.5f);
                        if (targetH > gameH) {
                            runner->widescreenExtraHeight = targetH - gameH;
                            gameH = targetH;
                        }
                    }
                }

                // The application surface (FBO) is sized to defaultWindowWidth x defaultWindowHeight.
                // It is a bit hard to understand, but here's how it works:
                // The Port X/Port Y controls the position of the game viewport within the application surface.
                // The Port W/Port H controls the size of the game viewport within the application surface.
                // Think of it like if you had an image (or... well, a framebuffer) and you are "pasting" it over the application surface.
                // And the Port W/Port H are scaled by the window size too (set by the GEN8 chunk)
                float displayScaleX;
                float displayScaleY;

                Runner_drawPre(runner, fbWidth, fbHeight);
                Runner_computeViewDisplayScale(runner, gameW, gameH, &displayScaleX, &displayScaleY);

                runner->renderGameW = gameW;
                runner->renderGameH = gameH;

                // Calculate viewport (letterboxing) in screen coordinates for mouse mapping
                int32_t winW, winH, scaledW, scaledH;
                platformGetScaledWindowSize(&winW, &winH);
                if ((gameW * winH) / gameH < winW) {
                    scaledW = (gameW * winH) / gameH;
                    scaledH = winH;
                } else {
                    scaledW = winW;
                    scaledH = (gameH * winW) / gameW;
                }
                runner->viewportX = (winW - scaledW) / 2;
                runner->viewportY = (winH - scaledH) / 2;
                runner->viewportW = scaledW;
                runner->viewportH = scaledH;

                double mx, my;
                platformGetMousePos(&mx, &my);
                Runner_updateMousePosition(runner, winW, winH, mx, my);

                Runner_beginFrame(runner, gameW, gameH, fbWidth, fbHeight);

                // Clear FBO with room background color
#ifdef ENABLE_SW_RENDERER
                if (gfx == SOFTWARE) {
                    if (runner->drawBackgroundColor)
                        SWRenderer_clearFrameBuffer(renderer, runner->backgroundColor);
                    else
                        SWRenderer_clearFrameBuffer(renderer, 0);
                }
#endif
#if defined(ENABLE_LEGACY_GL) || defined(ENABLE_MODERN_GL)
                if (gfx == MODERN_GL || gfx == LEGACY_GL) {
                    if (runner->drawBackgroundColor) {
                        int rInt = BGR_R(runner->backgroundColor);
                        int gInt = BGR_G(runner->backgroundColor);
                        int bInt = BGR_B(runner->backgroundColor);
                        glClearColor(rInt / 255.0f, gInt / 255.0f, bInt / 255.0f, 1.0f);
                    } else
                        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

                    glClear(GL_COLOR_BUFFER_BIT);
                }
#endif

                Runner_drawViews(runner, gameW, gameH, displayScaleX, displayScaleY, debugShowCollisionMasks);
                renderer->vtable->endFrameInit(renderer);
                Runner_drawPost(runner, fbWidth, fbHeight);
                renderer->vtable->endFrameEnd(renderer);
                Runner_drawGUI(runner, fbWidth, fbHeight, gameW, gameH);

#if defined(ENABLE_LEGACY_GL) || defined(ENABLE_MODERN_GL)
                // Capture screenshot if this frame matches a requested frame
                bool shouldScreenshot = hmget(args.screenshotFrames, runner->frameCount);

                if (shouldScreenshot) {
                    int32_t appId = runner->applicationSurfaceId;
                    GLuint readFbo;
#ifdef ENABLE_LEGACY_GL
                    if (gfx == LEGACY_GL) {
                        readFbo = ((GLLegacyRenderer*) renderer)->surfaces[appId];
                    } else
#endif
                    {
                        readFbo = ((GLRenderer*) renderer)->surfaces[appId];
                    }
                    captureScreenshot(readFbo, args.screenshotPattern, runner->frameCount, gameW, gameH);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                }

                // Dump all surfaces if this frame matches a requested frame
                bool shouldDumpSurfaces = hmget(args.screenshotSurfacesFrames, runner->frameCount);

                if (shouldDumpSurfaces) {
                    GLRenderer* gl = (GLRenderer*) renderer;
                    dumpAllSurfaces(gl, args.screenshotSurfacesPattern, runner->frameCount);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                }
#endif

                if (args.exitAtFrame >= 0 && runner->frameCount >= args.exitAtFrame) {
                    printf("Exiting at frame %d (--exit-at-frame)\n", runner->frameCount);
                    shouldWindowClose = true;
                }

                if (shouldStep && args.traceFrames) {
                    double frameElapsedMs = (platformGetTime() - frameStartTime) * 1000.0;
                    fprintf(stderr, "Frame %d (End, %.2f ms)\n", runner->frameCount, frameElapsedMs);
                }

                // Only swap when there isn't a room change to match the original runner.
                if (runner->pendingRoom == -1)
                    platformSwapBuffers();
                Runner_handlePendingRoomChange(runner);
            }

            // Limit frame rate to room speed (skip in headless mode for max speed!!)
            if (!args.headless && runner->currentRoom->speed > 0) {
                static bool fastForwardActive = false;
                static bool fastForwardTabPrev = false;
                bool fastForwardTabNow = RunnerKeyboard_checkPressed(runner->keyboard, '\t');
                if (args.fastForwardSpeed > 0.0 && fastForwardTabNow && !fastForwardTabPrev) {
                    fastForwardActive = !fastForwardActive;
                    lastFrameTime = platformGetTime();
                }
                fastForwardTabPrev = fastForwardTabNow;
                double effectiveSpeed = (args.fastForwardSpeed > 0.0 && fastForwardActive) ? args.fastForwardSpeed : args.speedMultiplier;
                double targetFrameTime = 1.0 / (runner->currentRoom->speed * effectiveSpeed);
                double nextFrameTime = lastFrameTime + targetFrameTime;
                platformSleepUntil(nextFrameTime);
            }
            lastFrameTime = platformGetTime();
        }

        saveInputRecording();

        // Snapshot any pending game_change request before we tear the runner down
        char* nextWorkingDirectory = runner->pendingWorkingDirectory;
        char* nextLaunchParameters = runner->pendingLaunchParameters;
        runner->pendingWorkingDirectory = nullptr;
        runner->pendingLaunchParameters = nullptr;

        // Cleanup
        runner->audioSystem->vtable->destroy(runner->audioSystem);
        runner->audioSystem = nullptr;
        renderer->vtable->destroy(renderer);

        // Keep the window + GL context alive across game_change so we don't spawn a new window
        if (actuallyShuttingDown) {
            platformExit();
            platformInitialized = false;
        }

        Runner_free(runner);
        OverlayFileSystem_destroy(overlayFs);
#ifdef ENABLE_VM_OPCODE_PROFILER
        VM_printOpcodeProfilerReport(vm);
#endif
        VM_free(vm);
        DataWin_free(dataWin);

        if (actuallyShuttingDown) {
            freeCommandLineArgs(&args);
            free(currentDataWinPath);
            repeat(arrlen(currentGameArgs), i) {
                free(currentGameArgs[i]);
            }
            arrfree(currentGameArgs);
            printf("Bye! :3\n");
            return 0;
        }

        // game_change was called, so we need to restart the runner with the new data.win and launch parameters
        if (nextWorkingDirectory != nullptr && nextLaunchParameters != nullptr) {
            char** newArguments = nullptr;
            newArguments = extractRunnerArguments(nextLaunchParameters);

            // Extract the data.win filename from "-game <file>" inside the new launch parameters
            char* dataWinFilename = nullptr;
            {
                // After extraction, we now need to figure out where is the "-game" argument
                size_t length = arrlen(newArguments);
                repeat(length, i) {
                    if (strcmp(newArguments[i], "-game") == 0) {
                        // So we already know that the data.win file will be the NEXT one
                        if (length - 1 == i)
                            break; // Where's the value?? Bailing...

                        dataWinFilename = safeStrdup(newArguments[i + 1]);
                        break;
                    }
                }
            }

            if (dataWinFilename == nullptr) {
                fprintf(stderr, "Runner: Launch parameters '%s' did not contain a '-game <file>' entry! Shutting down...\n", nextLaunchParameters);
                free(nextWorkingDirectory);
                free(nextLaunchParameters);
                freeCommandLineArgs(&args);
                free(currentDataWinPath);
                repeat(arrlen(newArguments), i) {
                    free(newArguments[i]);
                }
                arrfree(newArguments);
                repeat(arrlen(currentGameArgs), i) {
                    free(currentGameArgs[i]);
                }
                arrfree(currentGameArgs);
                return 1;
            }

            // Get the parent directory of the main data.win file
            char* parentDir = safeStrdup(currentDataWinPath);
            {
                char* lastSlash = strrchr(parentDir, '/');
                char* lastBackslash = strrchr(parentDir, '\\');
                char* sep = (lastSlash > lastBackslash) ? lastSlash : lastBackslash;
                if (sep != nullptr) {
                    *sep = '\0';
                } else {
                    parentDir[0] = '.';
                    parentDir[1] = '\0';
                }
            }

            // The pendingWorkingDirectory contains a slash at the beginning of it (example: /chapter3)
            // The parentDir does NOT have a trailing slash, so we don't need to bother with it
            size_t newPathLen = strlen(parentDir) + strlen(nextWorkingDirectory) + 1 + strlen(dataWinFilename) + 1;
            char* newPath = safeMalloc(newPathLen);
            snprintf(newPath, newPathLen, "%s%s/%s", parentDir, nextWorkingDirectory, dataWinFilename);

            free(parentDir);
            free(currentDataWinPath);
            currentDataWinPath = newPath;
            args.dataWinPath = currentDataWinPath;

            // Rebuild the gameArgs
            // First, we'll remove ALL args except the first one (which is the argv[0])
            while (arrlen(currentGameArgs) > 1) {
                free(currentGameArgs[1]);
                arrdel(currentGameArgs, 1);
            }

            repeat(arrlen(newArguments), i) {
                arrput(currentGameArgs, newArguments[i]);
            }

            free(dataWinFilename);
            free(nextWorkingDirectory);
            free(nextLaunchParameters);
            arrfree(newArguments);
        }

#ifdef _WIN32
        timeEndPeriod(1);
#endif
        printf("Bye! :3\n");
    }
}
