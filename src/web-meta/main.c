#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "data_win.h"

DataWin* parseDataWin(const char* path) {
    DataWinParserOptions opts = {0};
    opts.parseGen8 = true;
    opts.parseStrg = true; // GEN8 stores string offsets that point into STRG
    return DataWin_parse(path, opts);
}

void freeDataWin(DataWin* dw) {
    if (dw != nullptr) DataWin_free(dw);
}

const char* getGameName(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.name : nullptr;
}

const char* getGameDisplayName(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.displayName : nullptr;
}

uint32_t getMajorVersion(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.major : 0;
}

uint32_t getMinorVersion(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.minor : 0;
}

uint32_t getRelease(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.release : 0;
}

uint32_t getBuild(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.build : 0;
}

uint32_t getDefaultWindowWidth(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.defaultWindowWidth : 0;
}

uint32_t getDefaultWindowHeight(DataWin* dw) {
    return (dw != nullptr) ? dw->gen8.defaultWindowHeight : 0;
}

int main(void) {
    return 0;
}
