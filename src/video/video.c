#include "video.h"
#include "gml_array.h"
#include "stb_ds.h"

#include "stdio_compat.h"
#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#ifndef OTHER_ASYNC_SOCIAL
#define OTHER_ASYNC_SOCIAL 70
#endif
#ifndef SOUND_INSTANCE_ID_BASE
#define SOUND_INSTANCE_ID_BASE 100000
#endif

static const char* pendingVideoEventNames[4];
static int32_t pendingVideoEventCount = 0;

// From Cinnamon
// https://github.com/Project-Sunshine-Native/cinnamon/blob/DELTARUNE-3DS/src/vm_builtins.c#L4428
static void cleanupAsyncMap(Runner* runner, int32_t mapId) {
    if (mapId < 0 || (int32_t)arrlen(runner->dsMapPool) <= mapId) return;
    DsMapEntry** mapPtr = &runner->dsMapPool[mapId];
    if (*mapPtr != nullptr) {
        repeat(shlen(*mapPtr), i) {
            free((*mapPtr)[i].key);
            RValue_free(&(*mapPtr)[i].value);
        }
        shfree(*mapPtr);
        *mapPtr = nullptr;
    }
}

// From Cinnamon
// https://github.com/Project-Sunshine-Native/cinnamon/blob/DELTARUNE-3DS/src/vm_builtins.c#L4441
static void dispatchVideoAsync(Runner* runner, const char* type) {
    DsMapEntry* map = nullptr;
    arrput(runner->dsMapPool, map);
    int32_t mapId = (int32_t)arrlen(runner->dsMapPool) - 1;
    DsMapEntry** mapPtr = &runner->dsMapPool[mapId];
    if (mapPtr == nullptr) return;

    shput(*mapPtr, safeStrdup("type"), RValue_makeOwnedString(safeStrdup(type)));
    shput(*mapPtr, safeStrdup("event_type"), RValue_makeOwnedString(safeStrdup(type)));
    shput(*mapPtr, safeStrdup("status"), RValue_makeReal(0));

    int32_t previousAsyncLoad = runner->asyncLoadMapId;
    runner->asyncLoadMapId = mapId;
    Runner_executeEventForAll(runner, EVENT_OTHER, OTHER_ASYNC_SOCIAL);
    runner->asyncLoadMapId = previousAsyncLoad;

    cleanupAsyncMap(runner, mapId);
}

static void videoEnqueueAsyncEvent(const char* type) {
    if (pendingVideoEventCount >= (int32_t)(sizeof(pendingVideoEventNames) / sizeof(pendingVideoEventNames[0]))) return;
    pendingVideoEventNames[pendingVideoEventCount++] = type;
}

void Video_executePendingAsyncEvents(Runner* runner) {
    if (pendingVideoEventCount <= 0) return;
    const char* type = pendingVideoEventNames[0];
    repeat((pendingVideoEventCount - 1), i) {
        pendingVideoEventNames[i] = pendingVideoEventNames[i + 1];
    }
    pendingVideoEventCount--;
    dispatchVideoAsync(runner, type);
}

static VideoDecoder* videoDecoder = nullptr;
int video_w = 0, video_h = 0;
int videoSurfId = 0;
bool videoRunnin = false;
static GMLReal videoVolume = 1.0;

static int32_t videoAudioStreamIndex = -1;
static int32_t videoAudioInstanceId = -1;
static char* videoAudioWavPath = nullptr;

static void videoAudioDiscard(Runner* runner);

static bool videoAudioStart(Runner* runner, const char* url) {
    videoAudioDiscard(runner);
    if (runner == nullptr || runner->fileSystem == nullptr || runner->audioSystem == nullptr) return false;
    if (videoDecoder == nullptr || videoDecoder->vtable->extractAudio == nullptr) return false;
    if (!videoDecoder->vtable->extractAudio(videoDecoder, runner, url)) return false;
    AudioSystem* audio = runner->audioSystem;
    const char* wavPath = "butterscotch_video_extract.wav";
    int32_t streamIndex = audio->vtable->createStream(audio, wavPath);
    if (streamIndex < 0) return false;
    videoAudioStreamIndex = streamIndex;
    videoAudioWavPath = safeStrdup(wavPath);
    videoAudioInstanceId = audio->vtable->playSound(audio, streamIndex, 0, false);
    if (videoAudioInstanceId >= SOUND_INSTANCE_ID_BASE) {
        audio->vtable->setSoundGain(audio, videoAudioInstanceId, videoVolume, 0);
    }
    return true;
}

static void videoAudioDiscard(Runner* runner) {
    if (videoAudioWavPath != nullptr) {
        if (runner != nullptr && runner->fileSystem != nullptr) {
            if (videoAudioStreamIndex >= 0 && runner->audioSystem != nullptr) {
                if (videoAudioInstanceId >= SOUND_INSTANCE_ID_BASE) {
                    runner->audioSystem->vtable->stopSound(runner->audioSystem, videoAudioInstanceId);
                }
                runner->audioSystem->vtable->destroyStream(runner->audioSystem, videoAudioStreamIndex);
            }
            runner->fileSystem->vtable->deleteFile(runner->fileSystem, videoAudioWavPath);
        }
        free(videoAudioWavPath);
        videoAudioWavPath = nullptr;
    }
    videoAudioStreamIndex = -1;
    videoAudioInstanceId = -1;
}

static void video_cleanup(Runner* runner) {
    if (videoDecoder != nullptr) {
        videoDecoder->vtable->close(videoDecoder);
        videoDecoder->vtable->quit(videoDecoder);
        free(videoDecoder->impl);
        free(videoDecoder);
        videoDecoder = nullptr;
    }
    videoAudioDiscard(runner);
    videoSurfId = 0;
    video_w = 0;
    video_h = 0;
    videoRunnin = false;
    pendingVideoEventCount = 0;
}

static void video_process(Runner* runner) {
    //Renderer* rend = runner->renderer;
    if (!videoRunnin) return;
    if (videoDecoder == nullptr) return;
    if (!videoDecoder->vtable->isRunning(videoDecoder) && videoRunnin) {
        videoRunnin = false;
        if (videoAudioInstanceId >= SOUND_INSTANCE_ID_BASE && runner->audioSystem != nullptr) {
            runner->audioSystem->vtable->stopSound(runner->audioSystem, videoAudioInstanceId);
        }
        dispatchVideoAsync(runner, "video_end");
        return;
    }
    if (videoSurfId != 0 && videoDecoder->vtable->update(videoDecoder) == 0) {
        videoDecoder->vtable->draw(videoDecoder, runner, videoSurfId);
        //stbi_write_png("pinge.png", video_w, video_h, 4, data[0], line_size[0]);
    }
}

RValue builtin_video_open(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    FileSystem* fs = runner->fileSystem;

    char* filePath = RValue_toString(args[0], ctx->dataWin);
    char* url = fs->vtable->resolvePath(fs, filePath);
    if (videoDecoder == nullptr) {
        videoDecoder = VideoDecoder_createBackend();
        if (videoDecoder != nullptr) videoDecoder->vtable->init();
    } else {
        videoDecoder->vtable->close(videoDecoder);
    }
    if (videoDecoder == nullptr) {
        fprintf(stderr, "Video playback is disabled or unsupported, skipping the video: %s\n", url);
        free(filePath);
        free(url);
        videoEnqueueAsyncEvent("video_start");
        videoEnqueueAsyncEvent("video_end");
        return RValue_makeUndefined();
    }
    videoSurfId = 0;
    video_w = 0;
    video_h = 0;
    printf("%s\n", url);
    if (!videoDecoder->vtable->open(videoDecoder, url)) {
        fprintf(stderr, "Unable to open video: %s\n", url);
        free(filePath);
        free(url);
        videoEnqueueAsyncEvent("video_start");
        videoEnqueueAsyncEvent("video_end");
        return RValue_makeUndefined();
    }
    video_w = videoDecoder->vtable->width(videoDecoder);
    video_h = videoDecoder->vtable->height(videoDecoder);
    videoAudioStart(runner, url);
    free(filePath);
    free(url);

    videoRunnin = true;
    videoDecoder->vtable->resume(videoDecoder);
    dispatchVideoAsync(runner, "video_start");
    return RValue_makeUndefined();
}

RValue builtin_video_start(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    return builtin_video_open(ctx, args, argCount);
}

RValue builtin_video_enable_loop(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeUndefined();
}

RValue builtin_video_set_volume(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    videoVolume = RValue_toReal(args[0]);
    if (videoAudioInstanceId >= SOUND_INSTANCE_ID_BASE && ctx->runner->audioSystem != nullptr) {
        ctx->runner->audioSystem->vtable->setSoundGain(ctx->runner->audioSystem, videoAudioInstanceId, (float)videoVolume, 0);
    }
    return RValue_makeUndefined();
}

RValue builtin_video_close(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    video_cleanup(ctx->runner);
    return RValue_makeUndefined();
}

RValue builtin_video_draw(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (videoSurfId == 0 && videoRunnin) videoSurfId = Renderer_createSurface(ctx->runner->renderer, video_w, video_h);
    if (videoSurfId != 0 && videoRunnin) video_process(ctx->runner);
    GMLArray* out = GMLArray_create(ctx->dataWin, 2);
    *GMLArray_slot(out, 1) = RValue_makeReal(videoSurfId);
    return RValue_makeArray(out);
}

RValue builtin_video_pause(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (videoDecoder != nullptr && videoRunnin) {
        videoDecoder->vtable->pause(videoDecoder);
        if (videoAudioInstanceId >= SOUND_INSTANCE_ID_BASE && ctx->runner->audioSystem != nullptr) {
            ctx->runner->audioSystem->vtable->pauseSound(ctx->runner->audioSystem, videoAudioInstanceId);
        }
    }
    return RValue_makeUndefined();
}

RValue builtin_video_resume(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (videoDecoder != nullptr && videoRunnin) {
        videoDecoder->vtable->resume(videoDecoder);
        if (videoAudioInstanceId >= SOUND_INSTANCE_ID_BASE && ctx->runner->audioSystem != nullptr) {
            ctx->runner->audioSystem->vtable->resumeSound(ctx->runner->audioSystem, videoAudioInstanceId);
        }
    }
    return RValue_makeUndefined();
}

RValue builtin_video_get_format(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    return RValue_makeReal(0);
}

RValue builtin_video_get_status(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (videoDecoder == nullptr) return RValue_makeReal(1);
    bool playing = videoRunnin && videoDecoder->vtable->isRunning(videoDecoder) && !videoDecoder->vtable->isPaused(videoDecoder);
    return RValue_makeReal(!playing);
}

RValue builtin_video_get_duration(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (videoDecoder != nullptr && videoRunnin) {
        return RValue_makeReal((GMLReal)(videoDecoder->vtable->duration(videoDecoder) * 1000));
    }
    return RValue_makeReal(0);
}

RValue builtin_video_get_position(VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (videoDecoder != nullptr && videoRunnin) {
        return RValue_makeReal((GMLReal)(videoDecoder->vtable->position(videoDecoder) * 1000));
    }
    return RValue_makeReal(0);
}

