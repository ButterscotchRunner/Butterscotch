#pragma once

#include "common.h"
#include "audio_system.h"
#include "miniaudio.h"

#define WEB_MAX_SOUND_INSTANCES 128
#define WEB_SOUND_INSTANCE_ID_BASE 100000
#define WEB_MAX_AUDIO_STREAMS 32
#define WEB_AUDIO_STREAM_INDEX_BASE 300000

typedef struct {
    bool active;
    int32_t soundIndex;
    int32_t instanceId;
    ma_sound maSound;
    ma_decoder decoder;
    bool ownsDecoder;
    float targetGain;
    float currentGain;
    float fadeTimeRemaining;
    float fadeTotalTime;
    float startGain;
    int32_t priority;
} WebSoundInstance;

typedef struct {
    bool active;
    char* filePath;
} WebAudioStreamEntry;

typedef struct {
    AudioSystem base;
    ma_engine engine;
    bool engineReady;
    int32_t sampleRate;
    WebSoundInstance instances[WEB_MAX_SOUND_INSTANCES];
    int32_t nextInstanceCounter;
    FileSystem* fileSystem;
    WebAudioStreamEntry streams[WEB_MAX_AUDIO_STREAMS];
} WebAudioSystem;

// Creates a no-device miniaudio engine that mixes into a buffer when WebAudioSystem_pullFrames is called.
// sampleRate must match the AudioContext's sampleRate on the JS side.
WebAudioSystem* WebAudioSystem_create(int32_t sampleRate);

// Pulls frameCount interleaved-stereo float32 frames into out.
// out must have at least frameCount * 2 floats of space. Underruns are zero-filled by miniaudio.
void WebAudioSystem_pullFrames(WebAudioSystem* audio, float* out, int32_t frameCount);
