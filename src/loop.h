#ifndef _BS_LOOP_H_
#define _BS_LOOP_H_

#include "platformdefs.h"

char** extractRunnerArguments(char* rawArguments);
int loop(CommandLineArgs args, const char *argv0);
void freeCommandLineArgs(CommandLineArgs* args);

#define LOOP_CONTINUE -1

void loop_init(CommandLineArgs args, const char *argv0);
int loop_begin(void);
int loop_frame(void);
int loop_shutdown(void);

#endif
