#include <loop.h>
#include <stdbool.h>
#include <nds.h>
#include <filesystem.h>
#include "stb_image.h"
#include "nds_image.h"
#include <fat.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

static FILE* logFile = NULL;

//nitro:/friend.png
#define LOG_BUFFER_SIZE 256
void platformLog(const logType type, const char *format, va_list va) {
    const char* colourPrefix = ANSI_COLOUR_CODE_RESET;
    const char* textPrefix = "";
    char buffer[LOG_BUFFER_SIZE];
    
    switch (type) {
        case LOG_TYPE_NORMAL:
            break;
        case LOG_TYPE_WARNING:
            colourPrefix = ANSI_COLOUR_CODE_BOLD_YELLOW;
            textPrefix = "Warning: ";
            break;
        case LOG_TYPE_ERROR:
            colourPrefix = ANSI_COLOUR_CODE_BOLD_RED;
            textPrefix = "Error: ";
            break;
        case LOG_TYPE_DEBUG:
            colourPrefix = ANSI_COLOUR_CODE_BOLD_PURPLE;
            textPrefix = "Debug: ";
            break;
    }
    int written = snprintf(buffer, sizeof(buffer), "%s", textPrefix);
    
    if (written >= 0 && written < (int)sizeof(buffer)) {
        vsnprintf(buffer + written, (int)sizeof(buffer) - written, format, va);
    }
    buffer[sizeof(buffer) - 1] = '\0';

    iprintf("%s%s%s", colourPrefix, buffer, ANSI_COLOUR_CODE_RESET);

    if (logFile != NULL){
        fputs(buffer, logFile);
        fflush(logFile);
        fsync(fileno(logFile));
    }
}

int main(int argc, char* argv[]){
    //Init DS stuff
    defaultExceptionHandler();
    consoleDemoInit(); //Bottom screen log
    if (!fatInitDefault()){
        printf("fatInitDefault failed!!\n");
    }
    else
        logFile = fopen("sd:/butterscotch_log.txt", "w");

    logInfo("Hello butterscotchDS!\n\n");
    if (!nitroFSInit(NULL)){
        logInfo("nitroFSInit failed!!\n");
    }

    CommandLineArgs args = {0};

#ifdef ENABLE_VM_TRACING
    args.traceBytecodeAfterFrame = 0;
#endif
    args.speedMultiplier = 1.0;
    args.fastForwardSpeed = 0.0;
    args.osType = OS_WINDOWS;
    args.profilerFramesBetween = 0;
    args.dataWinPath = "nitro:/data.win";
    args.saveFolder = "";
    args.lazyTextures = true;
    args.lazyRooms = true;
    args.lazyAudio = true;
    args.lazyCode = true; 
    args.eagerRooms = NULL;
    args.exitAtFrame = -1;
    args.renderer = LIBNDS;
    args.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;


    int ret = loop(args, argv[0]);
    freeCommandLineArgs(&args);
    return ret;
}
