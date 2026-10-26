#ifndef BS_UWP_UI_H
#define BS_UWP_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "launch_options.h"

struct DataWin;

#define UWP_UI_WIDTH 1280
#define UWP_UI_HEIGHT 720

bool uwpUiInit(void);
void uwpUiShutdown(void);
void uwpUiBegin(void);
void uwpUiSelector(const char* const* games, int count, int selected, const char* status);
void uwpUiLaunchOptions(const char* game, const UwpLaunchOptions* options, int selected);
void uwpUiLoading(const char* game, const char* stage, uint64_t current, uint64_t total);
void uwpUiRender(int width, int height);
void uwpLoadingProgress(const char* title, const char* stage, uint64_t current, uint64_t total);
void uwpDataProgress(const char* chunk, int index, int total, struct DataWin* dataWin, void* userData);

#endif
