#ifndef BS_UWP_LAUNCH_OPTIONS_H
#define BS_UWP_LAUNCH_OPTIONS_H

#include <stdbool.h>

typedef struct {
    bool debugStats;
    bool lazyTextures;
    bool texture16bpp;
} UwpLaunchOptions;

enum {
    UWP_OPTION_DEBUG_STATS,
    UWP_OPTION_LAZY_TEXTURES,
    UWP_OPTION_16BPP_TEXTURES,
    UWP_OPTION_COUNT
};

UwpLaunchOptions uwpLaunchOptionsDefault(void);
UwpLaunchOptions uwpLaunchOptionsLoad(const char* path);
bool uwpLaunchOptionsSave(const char* path, const UwpLaunchOptions* options);
void uwpLaunchOptionsChange(UwpLaunchOptions* options, int option);

#endif
