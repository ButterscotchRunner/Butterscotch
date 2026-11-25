#include "launch_options.h"
#include <stdio.h>
#include <string.h>

UwpLaunchOptions uwpLaunchOptionsDefault(void) {
    UwpLaunchOptions options = {0};
    return options;
}

UwpLaunchOptions uwpLaunchOptionsLoad(const char* path) {
    UwpLaunchOptions options = uwpLaunchOptionsDefault();
    FILE* file = fopen(path, "r");
    if (!file) return options;
    char line[128], key[32], value[64];
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "%31[^=]=%63s", key, value) != 2) continue;
        if (!strcmp(value, "0") || !strcmp(value, "1")) {
            if (!strcmp(key, "debug_stats")) options.debugStats = value[0] == '1';
            if (!strcmp(key, "lazy_textures")) options.lazyTextures = value[0] == '1';
            if (!strcmp(key, "textures_16bpp")) options.texture16bpp = value[0] == '1';
        }
    }
    fclose(file);
    return options;
}

bool uwpLaunchOptionsSave(const char* path, const UwpLaunchOptions* options) {
    FILE* file = fopen(path, "w");
    if (!file) return false;
    int written = fprintf(file, "debug_stats=%d\nlazy_textures=%d\ntextures_16bpp=%d\n",
        options->debugStats, options->lazyTextures, options->texture16bpp);
    int closed = fclose(file);
    return written > 0 && closed == 0;
}

void uwpLaunchOptionsChange(UwpLaunchOptions* options, int option) {
    switch (option) {
        case UWP_OPTION_DEBUG_STATS:
            options->debugStats = !options->debugStats;
            break;
        case UWP_OPTION_LAZY_TEXTURES:
            options->lazyTextures = !options->lazyTextures;
            break;
        case UWP_OPTION_16BPP_TEXTURES:
            options->texture16bpp = !options->texture16bpp;
            break;
    }
}
