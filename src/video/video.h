#ifndef _BS_VIDEO_H_
#define _BS_VIDEO_H_

#include "common.h"
#include "runner.h"

typedef struct VideoDecoder VideoDecoder;

typedef struct {
    bool (*init)(void);
    void (*quit)(VideoDecoder* decoder);
    bool (*open)(VideoDecoder* decoder, const char* url);
    void (*close)(VideoDecoder* decoder);
    bool (*extractAudio)(VideoDecoder* decoder, Runner* runner, const char* url);
    bool (*isRunning)(VideoDecoder* decoder);
    bool (*isPaused)(VideoDecoder* decoder);
    void (*pause)(VideoDecoder* decoder);
    void (*resume)(VideoDecoder* decoder);
    void (*setLoop)(VideoDecoder* decoder, bool loop);
    int32_t (*update)(VideoDecoder* decoder);
    void (*draw)(VideoDecoder* decoder, Runner* runner, int32_t surfaceId);
    double (*duration)(VideoDecoder* decoder);
    double (*position)(VideoDecoder* decoder);
    int32_t (*width)(VideoDecoder* decoder);
    int32_t (*height)(VideoDecoder* decoder);
} VideoDecoderVtable;

struct VideoDecoder {
    VideoDecoderVtable* vtable;
    void* impl;
};

VideoDecoder* VideoDecoder_createBackend(void);

void Video_executePendingAsyncEvents(Runner* runner);

#endif /* _BS_VIDEO_H_ */