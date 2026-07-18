#ifndef _BS_LIBRETRO_AUDIO_SYSTEM_H_
#define _BS_LIBRETRO_AUDIO_SYSTEM_H_

#include "common.h"
#include "audio_system.h"
#include "miniaudio.h"

#define LIBRETRO_MAX_SOUND_INSTANCES 128
#define LIBRETRO_SOUND_INSTANCE_ID_BASE 100000
#define LIBRETRO_MAX_AUDIO_STREAMS 32
#define LIBRETRO_AUDIO_STREAM_INDEX_BASE 300000

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
} LibretroSoundInstance;

typedef struct {
    bool active;
    char* filePath;
} LibretroAudioStreamEntry;

typedef struct {
    AudioSystem base;
    ma_engine engine;
    bool engineReady;
    int32_t sampleRate;
    LibretroSoundInstance instances[LIBRETRO_MAX_SOUND_INSTANCES];
    int32_t nextInstanceCounter;
    FileSystem* fileSystem;
    LibretroAudioStreamEntry streams[LIBRETRO_MAX_AUDIO_STREAMS];
    ma_sound_group listeners[LIBRETRO_MAX_LISTENERS];
    float listenerGains[LIBRETRO_MAX_LISTENERS];
} LibretroAudioSystem;

LibretroAudioSystem* LibretroAudioSystem_create(DataWin* dataWin, int32_t sampleRate);

void LibretroAudioSystem_pullFrames(LibretroAudioSystem* audio, float* out, int32_t frameCount);

#endif /* _BS_LIBRETRO_AUDIO_SYSTEM_H_ */
