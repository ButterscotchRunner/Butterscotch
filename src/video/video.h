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

RValue builtin_video_open(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);
RValue builtin_video_start(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);
RValue builtin_video_close(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);
RValue builtin_video_draw(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);
RValue builtin_video_pause(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);
RValue builtin_video_resume(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);
RValue builtin_video_enable_loop(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);
RValue builtin_video_set_volume(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);
RValue builtin_video_get_format(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);
RValue builtin_video_get_status(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);
RValue builtin_video_get_duration(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);
RValue builtin_video_get_position(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount);

void Video_executePendingAsyncEvents(Runner* runner);

#endif /* _BS_VIDEO_H_ */