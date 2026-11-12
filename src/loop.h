#ifndef _BS_LOOP_H_
#define _BS_LOOP_H_

#include "platformdefs.h"

char** extractRunnerArguments(char* rawArguments);
int loop(CommandLineArgs args, const char *argv0);
void freeCommandLineArgs(CommandLineArgs* args);

#define LOOP_CONTINUE -1

void App_init(CommandLineArgs args, const char *argv0);
int App_begin(void);
int App_frame(void);
int App_shutdown(void);

#endif
