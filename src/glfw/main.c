#include "data_win.h"
#include "vm.h"

#include <glad/glad.h>
#ifdef USE_GLFW2
#include <GL/glfw.h>
#else
#include <GLFW/glfw3.h>
#endif
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
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
#ifndef USE_GLFW2
#include "glfw_gamepad.h"
#endif
#include "runner.h"
#include "input_recording.h"
#include "debug_overlay.h"
#include "gl_renderer.h"
#include "gl_legacy_renderer.h"
#include "overlay_file_system.h"
#include "ma_audio_system.h"
#include "noop_audio_system.h"
#include "stb_ds.h"
#include "stb_image_write.h"

#include "utils.h"
#include "profiler.h"

#ifndef USE_GLFW2
static void glfwErrorCallback(int code, const char* description) {
    fprintf(stderr, "GLFW error 0x%x: %s\n", code, description);
}
#endif

#ifndef ENABLE_GLES
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
    bool printDeclaredFunctions;
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
    bool lazyRooms;
    StringBooleanEntry* eagerRooms; // stb_ds string-keyed set of room names
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

static void parseCommandLineArgs(CommandLineArgs* args, int argc, char* argv[]) {
    memset(args, 0, sizeof(CommandLineArgs));

    static struct option longOptions[] = {
        {"screenshot",          required_argument, nullptr, 's'},
        {"screenshot-at-frame", required_argument, nullptr, 'f'},
        {"screenshot-surfaces", required_argument, nullptr, 'U'},
        {"screenshot-surfaces-at-frame", required_argument, nullptr, 'V'},
        {"headless",            no_argument,       nullptr, 'h'},
        {"print-rooms", no_argument,               nullptr, 'r'},
        {"print-declared-functions", no_argument,  nullptr, 'p'},
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
        {"profile-gml-scripts", required_argument, nullptr, 'q'},
        {"save-folder", required_argument, nullptr, 'B'},
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
#ifdef USE_GLFW2
    args->renderer = "legacy-gl";
#else
    args->renderer = "gl";
#endif
    args->osType = OS_WINDOWS;
    args->profilerFramesBetween = 0;

    int opt;
    while ((opt = getopt_long(argc, argv, "", longOptions, nullptr)) != -1) {
        switch (opt) {
            case 's':
                args->screenshotPattern = optarg;
                break;
            case 'f': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s'\n", optarg);
                    exit(1);
                }

                hmput(args->screenshotFrames, (int) frame, true);
                break;
            }
            case 'U':
                args->screenshotSurfacesPattern = optarg;
                break;
            case 'V': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --screenshot-surfaces-at-frame\n", optarg);
                    exit(1);
                }
                hmput(args->screenshotSurfacesFrames, (int) frame, true);
                break;
            }
            case 'h':
                args->headless = true;
                break;
            case 'r':
                args->printRooms = true;
                break;
            case 'p':
                args->printDeclaredFunctions = true;
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
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --exit-at-frame\n", optarg);
                    exit(1);
                }
                args->exitAtFrame = (int) frame;
                break;
            }
            case 'F': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --trace-bytecode-after-frame\n", optarg);
                    exit(1);
                }
                args->traceBytecodeAfterFrame = (int) frame;
                break;
            }
            case 'd': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --dump-frame\n", optarg);
                    exit(1);
                }
                hmput(args->dumpFrames, (int) frame, true);
                break;
            }
            case 'j': {
                char* endPtr;
                long frame = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || 0 > frame) {
                    fprintf(stderr, "Error: Invalid frame number '%s' for --dump-frame-json\n", optarg);
                    exit(1);
                }
                hmput(args->dumpJsonFrames, (int) frame, true);
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
                long seedVal = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0') {
                    fprintf(stderr, "Error: Invalid seed value '%s' for --seed\n", optarg);
                    exit(1);
                }
                args->seed = (int) seedVal;
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
                long framesBetween = strtol(optarg, &endPtr, 10);
                if (*endPtr != '\0' || framesBetween <= 0) {
                    fprintf(stderr, "Error: Invalid frame count '%s' for --profile-gml-scripts (must be > 0)\n", optarg);
                    exit(1);
                }
                args->profilerFramesBetween = (int) framesBetween;
                break;
            }
            case 'B':
                args->saveFolder = optarg;
                break;
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
            default:
                fprintf(stderr, "Usage: %s "
#ifndef USE_GLFW2
                        "[--headless] "
#endif
                        "[--screenshot=PATTERN] [--screenshot-at-frame=N ...] <path to data.win or game.unx>\n", argv[0]);
                exit(1);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Usage: %s "
#ifndef USE_GLFW2
                "[--headless] "
#endif
                "[--screenshot=PATTERN] [--screenshot-at-frame=N ...] <path to data.win or game.unx>\n", argv[0]);
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

#ifdef USE_GLFW2
    if (args->headless) {
        fprintf(stderr, "Headless mode is not supported with GLFW2\n");
        exit(1);
    }
#else
    if (args->headless && args->speedMultiplier != 1.0) {
        fprintf(stderr, "You can't set the speed multiplier while running in headless mode! Headless mode always run in real time\n");
        exit(1);
    }
#endif

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
}

// ===[ SCREENSHOT ]===
// Reads the contents of an FBO (use 0 for the default framebuffer) into a PNG file.
// If forceOpaque is true, the alpha channel is overwritten with 255, fixing any clobbering done by blending modes.
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
    repeat(gl->ssurfaceCount, surfaceId) {
        if (gl->surfaces[surfaceId] == 0)
            continue;

        int width = gl->surfaceWidth[surfaceId];
        int height = gl->surfaceHeight[surfaceId];
        if (0 >= width || 0 >= height) continue;

        char filename[512];
        snprintf(filename, sizeof(filename), filenamePattern, frameNumber, (int) surfaceId);
        writeFramebufferAsPng(gl->surfaces[surfaceId], width, height, filename, "Surface dump", false);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, gl->fbo);
}

// ===[ KEYBOARD INPUT ]===

#ifdef USE_GLFW2
#define GLFW_KEY_ESCAPE GLFW_KEY_ESC
#define GLFW_KEY_LEFT_SHIFT GLFW_KEY_LSHIFT
#define GLFW_KEY_RIGHT_SHIFT GLFW_KEY_RSHIFT
#define GLFW_KEY_LEFT_CONTROL GLFW_KEY_LCTRL
#define GLFW_KEY_RIGHT_CONTROL GLFW_KEY_RCTRL
#define GLFW_KEY_LEFT_ALT GLFW_KEY_LALT
#define GLFW_KEY_RIGHT_ALT GLFW_KEY_RALT
#define GLFW_KEY_DELETE GLFW_KEY_DEL
#define GLFW_KEY_PAGE_UP GLFW_KEY_PAGEUP
#define GLFW_KEY_PAGE_DOWN GLFW_KEY_PAGEDOWN
#endif

static int32_t glfwKeyToGml(int glfwKey) {
    // Letters and numbers are the same as GML
    if (glfwKey >= 'A' && glfwKey <= 'Z') return glfwKey;
    if (glfwKey >= '0' && glfwKey <= '9') return glfwKey;
    // Special keys need mapping
    switch (glfwKey) {
        case GLFW_KEY_ESCAPE:        return VK_ESCAPE;
        case GLFW_KEY_ENTER:         return VK_ENTER;
        case GLFW_KEY_TAB:           return VK_TAB;
        case GLFW_KEY_BACKSPACE:     return VK_BACKSPACE;
        case GLFW_KEY_SPACE:         return VK_SPACE;
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_RIGHT_SHIFT:   return VK_SHIFT;
        case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL: return VK_CONTROL;
        case GLFW_KEY_LEFT_ALT:
        case GLFW_KEY_RIGHT_ALT:     return VK_ALT;
        case GLFW_KEY_UP:            return VK_UP;
        case GLFW_KEY_DOWN:          return VK_DOWN;
        case GLFW_KEY_LEFT:          return VK_LEFT;
        case GLFW_KEY_RIGHT:         return VK_RIGHT;
        case GLFW_KEY_F1:            return VK_F1;
        case GLFW_KEY_F2:            return VK_F2;
        case GLFW_KEY_F3:            return VK_F3;
        case GLFW_KEY_F4:            return VK_F4;
        case GLFW_KEY_F5:            return VK_F5;
        case GLFW_KEY_F6:            return VK_F6;
        case GLFW_KEY_F7:            return VK_F7;
        case GLFW_KEY_F8:            return VK_F8;
        case GLFW_KEY_F9:            return VK_F9;
        case GLFW_KEY_F10:           return VK_F10;
        case GLFW_KEY_F11:           return VK_F11;
        case GLFW_KEY_F12:           return VK_F12;
        case GLFW_KEY_INSERT:        return VK_INSERT;
        case GLFW_KEY_DELETE:        return VK_DELETE;
        case GLFW_KEY_HOME:          return VK_HOME;
        case GLFW_KEY_END:           return VK_END;
        case GLFW_KEY_PAGE_UP:       return VK_PAGEUP;
        case GLFW_KEY_PAGE_DOWN:     return VK_PAGEDOWN;
        default:                     return -1; // Unknown
    }
}

static InputRecording* globalInputRecording = nullptr;

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

#ifdef USE_GLFW2
static Runner *g_runner = nullptr;
#endif

#ifdef USE_GLFW2
static void keyCallback(int key, int action) {
    Runner* runner = g_runner;
#else
static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void) scancode; (void) mods;
    Runner* runner = (Runner*) glfwGetWindowUserPointer(window);
#endif
    // During playback, suppress real keyboard input (window events like close still work)
    if (InputRecording_isPlaybackActive(globalInputRecording)) return;
    int32_t gmlKey = glfwKeyToGml(key);
    if (0 > gmlKey) return;
    if (action == GLFW_PRESS) RunnerKeyboard_onKeyDown(runner->keyboard, gmlKey);
    else if (action == GLFW_RELEASE) RunnerKeyboard_onKeyUp(runner->keyboard, gmlKey);
    // GLFW_REPEAT is ignored (GML doesn't use key repeat)
}

#ifdef USE_GLFW2
static void characterCallback(int codepoint, int action) {
    if (action != GLFW_PRESS) return;
    Runner* runner = g_runner;
#else
static void characterCallback(GLFWwindow* window, unsigned int codepoint) {
    Runner* runner = (Runner*) glfwGetWindowUserPointer(window);
#endif
    if (InputRecording_isPlaybackActive(globalInputRecording)) return;
    RunnerKeyboard_onCharacter(runner->keyboard, codepoint);
}

static void setGlfwWindowTitle(void* window, const char* title) {
    char windowTitle[256];
    snprintf(windowTitle, sizeof(windowTitle), "Butterscotch - %s", title);
#ifdef USE_GLFW2
    (void)window;
    glfwSetWindowTitle(windowTitle);
#else
    glfwSetWindowTitle((GLFWwindow*) window, windowTitle);
#endif
}

static bool getGlfwWindowFocus(void *window) {
#ifdef USE_GLFW2
    (void)window;
    return glfwGetWindowParam(GLFW_ACTIVE);
#else
    return glfwGetWindowAttrib((GLFWwindow*) window, GLFW_FOCUSED) != 0;
#endif
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
    sigaction(sig, &previousSignalActions[hmgeti(previousSignalActions, sig)].value, nullptr);
    raise(sig);
}
#endif

#if defined(_WIN32) && defined(USE_GLFW2)
// glfw2's glfwGetProcAddress is broken on Windows.
// This just implements it in a way that's fixed
// so it can be passed to GLAD.
#define glfwGetProcAddress fixed_glfwGetProcAddress
static void *fixed_glfwGetProcAddress(const char *name) {
    void *ret = (void *)wglGetProcAddress(name);
    if (ret == 0 || ret == (void *)1 || ret == (void *)2 || ret == (void *)3 || ret == (void *)-1) { // ChatGPT says this is needed because some OpenGL drivers do this
        HMODULE handle = GetModuleHandle("opengl32.dll");
        if (handle)
            ret = (void *)GetProcAddress(handle, name);
    }
    return ret;
}
#endif

// ===[ MAIN ]===
int main(int argc, char* argv[]) {
    CommandLineArgs args;
    parseCommandLineArgs(&args, argc, argv);

    printf("Loading %s...\n", args.dataWinPath);

    DataWin* dataWin = DataWin_parse(
        args.dataWinPath,
        (DataWinParserOptions) {
            .parseGen8 = true,
            .parseOptn = true,
            .parseLang = true,
            .parseExtn = false,
            .parseSond = true,
            .parseAgrp = true,
            .parseSprt = true,
            .parseBgnd = true,
            .parsePath = true,
            .parseScpt = true,
            .parseGlob = true,
            .parseShdr = true,
            .parseFont = true,
            .parseTmln = true,
            .parseObjt = true,
            .parseRoom = true,
            .parseTpag = true,
            .parseCode = true,
            .parseVari = true,
            .parseFunc = true,
            .parseStrg = true,
            .parseTxtr = true,
            .parseAudo = true,
            .skipLoadingPreciseMasksForNonPreciseSprites = true,
            .lazyLoadRooms = args.lazyRooms,
            .eagerlyLoadedRooms = args.eagerRooms
        }
    );

    Gen8* gen8 = &dataWin->gen8;
    printf("Loaded \"%s\" (%d) successfully! [Bytecode Version %u / GameMaker version %u.%u.%u.%u]\n", gen8->name, gen8->gameID, gen8->bytecodeVersion, dataWin->detectedFormat.major, dataWin->detectedFormat.minor, dataWin->detectedFormat.release, dataWin->detectedFormat.build);

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
            bool loadedHere = false;
            if (!room->payloadLoaded) {
                DataWin_loadRoomPayload(dataWin, (int32_t) idx);
                loadedHere = true;
            }

            printf("[%d] %s ()\n", idx, room->name);

            forEachIndexed(RoomGameObject, roomGameObject, idx2, room->gameObjects, room->gameObjectCount) {
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

    if (args.printDeclaredFunctions) {
        repeat(hmlen(vm->codeIndexByName), i) {
            printf("[%d] %s\n", vm->codeIndexByName[i].value, vm->codeIndexByName[i].key);
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

    // Init GLFW
#ifndef USE_GLFW2
    glfwSetErrorCallback(glfwErrorCallback);
#endif
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        DataWin_free(dataWin);
        freeCommandLineArgs(&args);
        return 1;
    }

    bool modernGL = strcmp(args.renderer, "legacy-gl") != 0;

    if (!modernGL && hmlen(args.screenshotSurfacesFrames)) {
        fprintf(stderr, "You can't use --screenshot-surfaces with --renderer legacy-gl!\n");
        return 0;
    }

#ifndef USE_GLFW2
    if (!modernGL) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 1);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    } else {
#ifdef ENABLE_GLES
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif
    }
#endif

#ifndef USE_GLFW2
    // Load SDL gamecontroller mappings
    {
        const char* dbPath = "gamecontrollerdb.txt";
        FILE* f = fopen(dbPath, "r");
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* buffer = (char*) malloc(len + 1);
            if (buffer != NULL) {
                fread(buffer, 1, len, f);
                buffer[len] = '\0';
                GlfwGamepad_loadMappings(buffer);
                free(buffer);
            }
            fclose(f);
        } else {
            fprintf(stderr, "Gamepad: SDL gamecontrollerdb.txt not found at %s, using defaults\n", dbPath);
        }
    }

    if (args.headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
#endif

#ifdef USE_GLFW2
    int window = glfwOpenWindow((int) gen8->defaultWindowWidth, (int) gen8->defaultWindowHeight, 8, 8, 8, 8, 24, 8, GLFW_WINDOW);
#else
    GLFWwindow* window = glfwCreateWindow((int) gen8->defaultWindowWidth, (int) gen8->defaultWindowHeight, windowTitle, nullptr, nullptr);
#endif
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        DataWin_free(dataWin);
        freeCommandLineArgs(&args);
        return 1;
    }

#ifndef USE_GLFW2
    glfwMakeContextCurrent(window);
#endif
    glfwSwapInterval(0); // Disable v-sync, we control timing ourselves

    // Load OpenGL function pointers via GLAD
#ifdef ENABLE_GLES
    if (!gladLoadGLES2Loader((GLADloadproc) glfwGetProcAddress)) {
#else
    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
#endif
        fprintf(stderr, "Failed to initialize GLAD\n");
#ifdef USE_GLFW2
        glfwCloseWindow();
#else
        glfwDestroyWindow(window);
#endif
        glfwTerminate();
        DataWin_free(dataWin);
        freeCommandLineArgs(&args);
        return 1;
    }

    // Install the OpenGL debug message callback
#ifndef ENABLE_GLES
    if (modernGL)
        installGLDebugCallback();
#endif

    // Initialize the renderer
    Renderer* renderer = nullptr;
#ifdef ENABLE_GLES
    if (strcmp(args.renderer, "legacy-gl") == 0) {
        fprintf(stderr, "--renderer legacy-gl is not available in GLES builds; falling back to gl\n");
    }
    renderer = GLRenderer_create();
#else
    if(strcmp(args.renderer, "legacy-gl") == 0)
        renderer = GLLegacyRenderer_create();
    else
        renderer = GLRenderer_create();
#endif

    // Initialize the audio system
    AudioSystem* audioSystem = nullptr;
    if (!args.headless) {
        audioSystem = (AudioSystem*) MaAudioSystem_create();
    } else {
        audioSystem = (AudioSystem*) NoopAudioSystem_create();
    }

    // Initialize the runner
    Runner* runner = Runner_create(dataWin, vm, renderer, (FileSystem*) overlayFs, audioSystem);
    runner->debugMode = args.debug;
    runner->osType = args.osType;
    runner->setWindowTitle = setGlfwWindowTitle;
    runner->windowHasFocus = getGlfwWindowFocus;
#ifdef USE_GLFW2
    runner->nativeWindow = (void*)0xDEADBEEF;
    g_runner = runner;
#else
    runner->nativeWindow = window;
#endif

    // Set up input recording/playback (both can be active: playback then continue recording)
    if (args.playbackInputsPath != nullptr) {
        globalInputRecording = InputRecording_createPlayer(args.playbackInputsPath, args.recordInputsPath);
    } else if (args.recordInputsPath != nullptr) {
        globalInputRecording = InputRecording_createRecorder(args.recordInputsPath);
    }
    if (globalInputRecording != nullptr) {
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

    // Set up keyboard input
#ifdef USE_GLFW2
    glfwSetKeyCallback(keyCallback);
    glfwSetCharCallback(characterCallback);
#else
    glfwSetWindowUserPointer(window, runner);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCharCallback(window, characterCallback);
#endif

#ifndef _WIN32
    struct sigaction sa = { .sa_handler = onCrashSignal };
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
    double lastFrameTime = glfwGetTime();
    while (
#ifdef USE_GLFW2
            glfwGetWindowParam(GLFW_OPENED)
#else
            !glfwWindowShouldClose(window)
#endif
            && !runner->shouldExit) {
        // Clear last frame's pressed/released state, then poll new input events
        RunnerKeyboard_beginFrame(runner->keyboard);
        RunnerGamepad_beginFrame(runner->gamepads);
        glfwPollEvents();
#ifndef USE_GLFW2
        GlfwGamepad_poll(runner->gamepads);
#endif

        // Process input recording/playback (must happen after glfwPollEvents, before Runner_step)
        InputRecording_processFrame(globalInputRecording, runner->keyboard, runner->frameCount);

        // Debug key bindings
        if (runner->debugMode) {
            // Pause
            if (RunnerKeyboard_checkPressed(runner->keyboard, 'P')) {
                debugPaused = !debugPaused;
                fprintf(stderr, "Debug: %s\n", debugPaused ? "Paused" : "Resumed");
            }

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

            // Reset global interact state because I HATE when I get stuck while moving through rooms
            if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F10)) {
                int32_t interactVarId = shget(runner->vmContext->globalVarNameMap, "interact");

                runner->vmContext->globalVars[interactVarId] = RValue_makeInt32(0);
                printf("Changed global.interact [%d] value!\n", interactVarId);
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
                frameStartTime = glfwGetTime();
                fprintf(stderr, "Frame %d (Start)\n", runner->frameCount);
            }

            // Run one game step (Begin Step, Keyboard, Alarms, Step, End Step, room transitions)
            Runner_step(runner);

            if (args.profilerFramesBetween > 0 && runner->frameCount > 0 && runner->frameCount % args.profilerFramesBetween == 0) {
                char* profilerReport = Profiler_createReport(vm->profiler, 20, args.profilerFramesBetween);
                if (profilerReport != nullptr) {
                    fprintf(stderr, "%s\n", profilerReport);
                    free(profilerReport);
                }
                Profiler_reset(vm->profiler);
            }

            // Update audio system (gain fading, cleanup ended sounds)
            float dt = (float) (glfwGetTime() - lastFrameTime);
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
        }

        // Query actual framebuffer size (differs from window size on Wayland with fractional scaling)
        int fbWidth, fbHeight;
#ifdef USE_GLFW2
        glfwGetWindowSize(&fbWidth, &fbHeight);
#else
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
#endif

        // Clear the default framebuffer (window background) to black
        if (!(strcmp(args.renderer, "legacy-gl") == 0)) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        glClear(GL_COLOR_BUFFER_BIT);

        int32_t gameW = (int32_t) gen8->defaultWindowWidth;
        int32_t gameH = (int32_t) gen8->defaultWindowHeight;

        // The application surface (FBO) is sized to defaultWindowWidth x defaultWindowHeight.
        // It is a bit hard to understand, but here's how it works:
        // The Port X/Port Y controls the position of the game viewport within the application surface.
        // The Port W/Port H controls the size of the game viewport within the application surface.
        // Think of it like if you had an image (or... well, a framebuffer) and you are "pasting" it over the application surface.
        // And the Port W/Port H are scaled by the window size too (set by the GEN8 chunk)
        float displayScaleX;
        float displayScaleY;

        Runner_computeViewDisplayScale(runner, gameW, gameH, &displayScaleX, &displayScaleY);

        renderer->vtable->beginFrame(renderer, gameW, gameH, fbWidth, fbHeight);

        // Clear FBO with room background color
        if (runner->drawBackgroundColor) {
            int rInt = BGR_R(runner->backgroundColor);
            int gInt = BGR_G(runner->backgroundColor);
            int bInt = BGR_B(runner->backgroundColor);
            glClearColor(rInt / 255.0f, gInt / 255.0f, bInt / 255.0f, 1.0f);
        } else {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        }
        glClear(GL_COLOR_BUFFER_BIT);

        Runner_drawViews(runner, gameW, gameH, displayScaleX, displayScaleY, debugShowCollisionMasks);

        renderer->vtable->endFrame(renderer);

        // Capture screenshot if this frame matches a requested frame
        bool shouldScreenshot = hmget(args.screenshotFrames, runner->frameCount);

        if (shouldScreenshot) {
            GLuint readFbo = (strcmp(args.renderer, "legacy-gl") == 0) ? 0 : ((GLRenderer*) renderer)->fbo;
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

        if (args.exitAtFrame >= 0 && runner->frameCount >= args.exitAtFrame) {
            printf("Exiting at frame %d (--exit-at-frame)\n", runner->frameCount);
#ifdef USE_GLFW2
            glfwCloseWindow();
#else
            glfwSetWindowShouldClose(window, GLFW_TRUE);
#endif
        }

        if (shouldStep && args.traceFrames) {
            double frameElapsedMs = (glfwGetTime() - frameStartTime) * 1000.0;
            fprintf(stderr, "Frame %d (End, %.2f ms)\n", runner->frameCount, frameElapsedMs);
        }

        // Only swap when there isn't a room change to match the original runner.
        if (runner->pendingRoom == -1) {
#ifdef USE_GLFW2
            glfwSwapBuffers();
#else
            glfwSwapBuffers(window);
#endif
        }
        Runner_handlePendingRoomChange(runner);

        // Limit frame rate to room speed (skip in headless mode for max speed!!)
        if (!args.headless && runner->currentRoom->speed > 0) {
            static bool fastForwardActive = false;
            static bool fastForwardTabPrev = false;
            bool fastForwardTabNow = glfwGetKey(
#ifndef USE_GLFW2
                    window,
#endif
                    GLFW_KEY_TAB) == GLFW_PRESS;
            if (args.fastForwardSpeed > 0.0 && fastForwardTabNow && !fastForwardTabPrev) {
                fastForwardActive = !fastForwardActive;
                lastFrameTime = glfwGetTime();
            }
            fastForwardTabPrev = fastForwardTabNow;
            double effectiveSpeed = (args.fastForwardSpeed > 0.0 && fastForwardActive) ? args.fastForwardSpeed : args.speedMultiplier;
            double targetFrameTime = 1.0 / (runner->currentRoom->speed * effectiveSpeed);
            double nextFrameTime = lastFrameTime + targetFrameTime;
            // Sleep for most of the remaining time, then spin-wait for precision
            double remaining = nextFrameTime - glfwGetTime();
            if (remaining > 0.002) {
                #ifdef _WIN32
                Sleep((DWORD) ((remaining - 0.001) * 1000));
                #else
                struct timespec ts = {
                    .tv_sec = 0,
                    .tv_nsec = (long) ((remaining - 0.001) * 1e9)
                };
                nanosleep(&ts, nullptr);
                #endif
            }
            while (glfwGetTime() < nextFrameTime) {
                // Spin-wait for the remaining sub-millisecond
            }
            lastFrameTime = nextFrameTime;
        } else {
            lastFrameTime = glfwGetTime();
        }
    }

    saveInputRecording();

    // Cleanup
    runner->audioSystem->vtable->destroy(runner->audioSystem);
    runner->audioSystem = nullptr;
    renderer->vtable->destroy(renderer);

#ifdef USE_GLFW2
    glfwCloseWindow();
#else
    glfwDestroyWindow(window);
#endif
    glfwTerminate();

    Runner_free(runner);
    OverlayFileSystem_destroy(overlayFs);
#ifdef ENABLE_VM_OPCODE_PROFILER
    VM_printOpcodeProfilerReport(vm);
#endif
    VM_free(vm);
    DataWin_free(dataWin);

    freeCommandLineArgs(&args);

    printf("Bye! :3\n");
    return 0;
}
