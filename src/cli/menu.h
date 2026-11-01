#ifndef BUTTERSCOTCH_CLI_MENU_H
#define BUTTERSCOTCH_CLI_MENU_H

#include <stdbool.h>

bool runLauncherMenu(const char* executablePath, char*** arguments, int* argumentCount);
void freeLauncherMenuArguments(char** arguments, int argumentCount);

#endif