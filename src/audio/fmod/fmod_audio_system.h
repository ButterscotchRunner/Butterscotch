#ifndef _BS_FMOD_AUDIO_SYSTEM_H_
#define _BS_FMOD_AUDIO_SYSTEM_H_

#include "common.h"
#include "audio_system.h"

#if defined(BUTTERSCOTCH_FMOD_STUB)
#define FMOD_OK 0
#define FMOD_VERSION 0u
#define FMOD_INIT_NORMAL 0u
#define FMOD_LOOP_OFF 0
#define FMOD_DEFAULT 0
#define FMOD_OPENMEMORY 0x00000004u
#define FMOD_CREATESAMPLE 0x00000008u
#define FMOD_LOOP_NORMAL 0x00000001u
#define FMOD_2D 0x00000002u
#define FMOD_CHANNEL_FREE 0x00000001u

typedef struct FMOD_SYSTEM { int unused; } FMOD_SYSTEM;
typedef struct FMOD_SOUND { int unused; } FMOD_SOUND;
typedef struct FMOD_CHANNEL { int unused; } FMOD_CHANNEL;
typedef struct FMOD_CHANNELGROUP { int unused; } FMOD_CHANNELGROUP;
typedef int FMOD_RESULT;

typedef struct FMOD_CREATESOUNDEXINFO {
    unsigned int cbsize;
    unsigned int length;
    unsigned int fileoffset;
    int numchannels;
    int defaultfrequency;
    int format;
    int bits;
    int lengthinbytes;
    int totalpcmlen;
} FMOD_CREATESOUNDEXINFO;

static inline FMOD_RESULT FMOD_System_Create(FMOD_SYSTEM** system, unsigned int version) {
    (void)system;
    (void)version;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_System_Init(FMOD_SYSTEM* system, int maxchannels, int flags, void* extradriverdata) {
    (void)system;
    (void)maxchannels;
    (void)flags;
    (void)extradriverdata;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_System_Update(FMOD_SYSTEM* system) {
    (void)system;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_System_CreateChannelGroup(FMOD_SYSTEM* system, const char* name, FMOD_CHANNELGROUP** channelgroup) {
    (void)system;
    (void)name;
    (void)channelgroup;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_ChannelGroup_SetVolume(FMOD_CHANNELGROUP* channelgroup, float volume) {
    (void)channelgroup;
    (void)volume;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_System_CreateSound(
    FMOD_SYSTEM* system,
    const char* name_or_data,
    unsigned int mode,
    FMOD_CREATESOUNDEXINFO* exinfo,
    FMOD_SOUND** sound
) {
    (void)system;
    (void)name_or_data;
    (void)mode;
    (void)exinfo;
    (void)sound;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_Sound_Release(FMOD_SOUND* sound) {
    (void)sound;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_System_PlaySound(FMOD_SYSTEM* system, FMOD_SOUND* sound, FMOD_CHANNELGROUP* channelgroup, int paused, FMOD_CHANNEL** channel) {
    (void)system;
    (void)sound;
    (void)channelgroup;
    (void)paused;
    (void)channel;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_Channel_SetVolume(FMOD_CHANNEL* channel, float volume) {
    (void)channel;
    (void)volume;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_Channel_SetPitch(FMOD_CHANNEL* channel, float pitch) {
    (void)channel;
    (void)pitch;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_Channel_SetPaused(FMOD_CHANNEL* channel, int paused) {
    (void)channel;
    (void)paused;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_Channel_Stop(FMOD_CHANNEL* channel) {
    (void)channel;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_Channel_IsPlaying(FMOD_CHANNEL* channel, int* isplaying) {
    (void)channel;
    *isplaying = 0;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_Channel_SetPosition(FMOD_CHANNEL* channel, unsigned int position, int postype) {
    (void)channel;
    (void)position;
    (void)postype;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_Channel_GetPosition(FMOD_CHANNEL* channel, unsigned int* position, int postype) {
    (void)channel;
    (void)position;
    (void)postype;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_Channel_GetLength(FMOD_CHANNEL* channel, unsigned int* length, int postype) {
    (void)channel;
    (void)length;
    (void)postype;
    return FMOD_OK;
}

static inline FMOD_RESULT FMOD_System_Release(FMOD_SYSTEM* system) {
    (void)system;
    return FMOD_OK;
}

#else
#include <fmod.h>
#endif

#define MAX_FMOD_SOUNDS 128
#define FMOD_SOUND_INSTANCE_BASE 100000

#define FMOD_SOUND_FLAG_STREAM 0x00000080u
#define FMOD_OPENUSER 0x00000010u

typedef struct {
    bool active;
    int32_t soundIndex;
    int32_t instanceId;
    FMOD_CHANNEL* channel;
    FMOD_SOUND* sound;
    float gain;
    float pitch;
    bool loop;
} FmodSoundInstance;

typedef struct {
    AudioSystem base;
    FMOD_SYSTEM* system;
    FMOD_CHANNELGROUP* masterGroup;
    FmodSoundInstance instances[MAX_FMOD_SOUNDS];
    FileSystem* fileSystem;
    int32_t nextInstanceId;
} FmodAudioSystem;

FmodAudioSystem* FmodAudioSystem_create(DataWin* dataWin);

#endif /* _BS_FMOD_AUDIO_SYSTEM_H_ */

