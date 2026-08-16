#ifndef _BS_LOOP_H_
#define _BS_LOOP_H_

#include "platformdefs.h"

char** extractRunnerArguments(char* rawArguments);
int loop(CommandLineArgs args, const char *argv0);

#endif
