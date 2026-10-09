#ifndef _BS_AUDIO_SYSTEM_H_
#define _BS_AUDIO_SYSTEM_H_

#include "common.h"
#include <stdint.h>
#include <stdbool.h>

#include "data_win.h"
#include "file_system.h"
#include "utils.h"

typedef struct {
    float current;
    float start;
    float target;
    float timeRemaining;
    float totalTime;
} AudioGroupGain;

// ===[ AudioSystem Vtable ]===

typedef struct AudioSystem AudioSystem;
struct FmodSystem;

typedef struct {
    void (*init)(AudioSystem* audio, DataWin* dataWin, FileSystem* fileSystem);
    void (*destroy)(AudioSystem* audio);
    void (*update)(AudioSystem* audio, float deltaTime);
    int32_t (*playSound)(AudioSystem* audio, int32_t soundIndex, int32_t priority, bool loop);
    void (*setSoundSpatial)(AudioSystem* audio, int32_t instanceId, float x, float y, float z, float ref, float max, float factor);
    void (*setListenerPosition)(AudioSystem* audio, float x, float y, float z);
    void (*stopSound)(AudioSystem* audio, int32_t soundOrInstance);
    void (*stopAll)(AudioSystem* audio);
    bool (*isPlaying)(AudioSystem* audio, int32_t soundOrInstance);
    void (*pauseSound)(AudioSystem* audio, int32_t soundOrInstance);
    void (*resumeSound)(AudioSystem* audio, int32_t soundOrInstance);
    void (*pauseAll)(AudioSystem* audio);
    void (*resumeAll)(AudioSystem* audio);
    void (*suspend)(AudioSystem* audio);
    void (*resume)(AudioSystem* audio);
    void (*setSoundGain)(AudioSystem* audio, int32_t soundOrInstance, float gain, uint32_t timeMs);
    float (*getSoundGain)(AudioSystem* audio, int32_t soundOrInstance);
    void (*setSoundPitch)(AudioSystem* audio, int32_t soundOrInstance, float pitch);
    float (*getSoundPitch)(AudioSystem* audio, int32_t soundOrInstance);
    float (*getTrackPosition)(AudioSystem* audio, int32_t soundOrInstance);
    void (*setTrackPosition)(AudioSystem* audio, int32_t soundOrInstance, float positionSeconds);
    // Total length of a sound in seconds. Accepts either a SOND index or an active sound instance id.
    // Returns 0.0 if unknown (e.g. stream not yet loaded or invalid index).
    float (*getSoundLength)(AudioSystem* audio, int32_t soundOrInstance);
    void (*setMasterGain)(AudioSystem* audio, float gain);
    void (*setMasterGainForListener)(AudioSystem* audio, float gain, int32_t listenerId);
    void (*setChannelCount)(AudioSystem* audio, int32_t count);
    void (*setGroupGain)(AudioSystem* audio, int32_t groupIndex, float gain, uint32_t timeMs);
    void (*groupLoad)(AudioSystem* audio, int32_t groupIndex);
    bool (*groupIsLoaded)(AudioSystem* audio, int32_t groupIndex);
    int32_t (*createStream)(AudioSystem* audio, const char* filename);
    bool (*destroyStream)(AudioSystem* audio, int32_t streamIndex);
    // encoded data stays alive until the voice ends or is stopped.
    // returns a voice id, -1 on failure, or -2 for the headless sink.
    int32_t (*playEncoded)(AudioSystem* audio, const uint8_t* data, size_t bytes, bool loop);
} AudioSystemVtable;

// ===[ AudioSystem Base Struct ]===

struct AudioSystem {
    AudioSystemVtable* vtable;
    DataWin* dw;
    DataWin** audioGroups;
    float listenerX, listenerY, listenerZ;
    AudioGroupGain* groupGains;
    uint32_t groupGainCount;
    struct FmodSystem* fmodSystem;
    int32_t fmodNextHandle;
};

static inline int32_t AudioSystem_soundGroup(const AudioSystem* audio, int32_t soundIndex) {
    if (audio == nullptr || audio->dw == nullptr || soundIndex < 0 ||
        (uint32_t)soundIndex >= audio->dw->sond.count) return 0;
    int32_t group = audio->dw->sond.sounds[soundIndex].audioGroup;
    return group >= 0 ? group : 0;
}

static inline float AudioSystem_getGroupGain(const AudioSystem* audio, int32_t groupIndex) {
    if (audio == nullptr || groupIndex < 0 || (uint32_t)groupIndex >= audio->groupGainCount) return 1.0f;
    return audio->groupGains[groupIndex].current;
}

static inline float AudioSystem_soundGroupGain(const AudioSystem* audio, int32_t soundIndex) {
    if (soundIndex < 0) return 1; // encoded voices don't belong to a gamemaker audio group.
    return AudioSystem_getGroupGain(audio, AudioSystem_soundGroup(audio, soundIndex));
}

static inline void AudioSystem_setGroupGain(AudioSystem* audio, int32_t groupIndex, float gain, uint32_t timeMs) {
    if (audio == nullptr || groupIndex < 0) return;
    uint32_t count = audio->dw != nullptr ? audio->dw->agrp.count : 0;
    if (count == 0) count = 1; // Legacy games have only the default audio group.
    if ((uint32_t)groupIndex >= count) return;

    if (audio->groupGains == nullptr) {
        audio->groupGains = (AudioGroupGain*)safeCalloc(count, sizeof(AudioGroupGain));
        audio->groupGainCount = count;
        for (uint32_t i = 0; i < count; i++) {
            audio->groupGains[i].current = 1.0f;
            audio->groupGains[i].start = 1.0f;
            audio->groupGains[i].target = 1.0f;
        }
    }

    AudioGroupGain* group = &audio->groupGains[groupIndex];
    group->start = group->current;
    group->target = gain;
    group->totalTime = (float)timeMs / 1000.0f;
    group->timeRemaining = group->totalTime;
    if (timeMs == 0) group->current = gain;
}

static inline bool AudioSystem_updateGroupGains(AudioSystem* audio, float deltaTime) {
    if (audio == nullptr || deltaTime <= 0) return false;
    bool changed = false;
    for (uint32_t i = 0; i < audio->groupGainCount; i++) {
        AudioGroupGain* group = &audio->groupGains[i];
        if (group->timeRemaining <= 0) continue;
        group->timeRemaining -= deltaTime;
        if (group->timeRemaining <= 0) {
            group->timeRemaining = 0;
            group->current = group->target;
        } else {
            group->current = group->start + (group->target - group->start) *
                (1.0f - group->timeRemaining / group->totalTime);
        }
        changed = true;
    }
    return changed;
}

#endif /* _BS_AUDIO_SYSTEM_H_ */
