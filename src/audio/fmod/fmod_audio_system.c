#include "fmod_audio_system.h"

#include "data_win.h"
#include "utils.h"
#include "stdio_compat.h"
#include "string_compat.h"

static FmodSoundInstance* fmodFindFreeSlot(FmodAudioSystem* fmod) {
    repeat(MAX_FMOD_SOUNDS, i) {
        if (!fmod->instances[i].active) {
            return &fmod->instances[i];
        }
    }
    return nullptr;
}

static bool fmodIsValidInstanceId(int32_t instanceId) {
    return instanceId >= FMOD_SOUND_INSTANCE_BASE && instanceId < FMOD_SOUND_INSTANCE_BASE + MAX_FMOD_SOUNDS;
}

static FmodSoundInstance* fmodFindInstanceById(FmodAudioSystem* fmod, int32_t instanceId) {
    repeat(MAX_FMOD_SOUNDS, i) {
        if (fmod->instances[i].active && fmod->instances[i].instanceId == instanceId) {
            return &fmod->instances[i];
        }
    }
    return nullptr;
}

static char* fmodResolveSoundPath(FmodAudioSystem* fmod, const Sound* sound) {
    if (sound == nullptr || sound->file == nullptr || sound->file[0] == '\0') {
        return nullptr;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s", sound->file);
    if (strchr(sound->file, '.') == nullptr) {
        snprintf(path, sizeof(path), "%s.ogg", sound->file);
    }
    return fmod->fileSystem != nullptr ? fmod->fileSystem->vtable->resolvePath(fmod->fileSystem, path) : strdup(path);
}

static void fmodInit(AudioSystem* audio, DataWin* dataWin, FileSystem* fileSystem) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    fmod->base.dw = dataWin;
    fmod->fileSystem = fileSystem;
    memset(fmod->instances, 0, sizeof(fmod->instances));
    fmod->nextInstanceId = 0;

    FMOD_RESULT result = FMOD_System_Create(&fmod->system, FMOD_VERSION);
    if (result != FMOD_OK) {
        logError("Audio: FMOD initialization failed (System_Create: %d)\n", result);
        fmod->system = nullptr;
        return;
    }

    result = FMOD_System_Init(fmod->system, 256, FMOD_INIT_NORMAL, nullptr);
    if (result != FMOD_OK) {
        logError("Audio: FMOD initialization failed (System_Init: %d)\n", result);
        FMOD_System_Release(fmod->system);
        fmod->system = nullptr;
        return;
    }

    result = FMOD_System_CreateChannelGroup(fmod->system, "master", &fmod->masterGroup);
    if (result != FMOD_OK) {
        fmod->masterGroup = nullptr;
    }

    logInfo("Audio: FMOD backend initialized\n");
}

static void fmodDestroy(AudioSystem* audio) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;

    repeat(MAX_FMOD_SOUNDS, i) {
        if (fmod->instances[i].active) {
            if (fmod->instances[i].channel != nullptr) {
                FMOD_Channel_Stop(fmod->instances[i].channel);
            }
            if (fmod->instances[i].sound != nullptr) {
                FMOD_Sound_Release(fmod->instances[i].sound);
            }
            memset(&fmod->instances[i], 0, sizeof(fmod->instances[i]));
        }
    }

    if (fmod->masterGroup != nullptr) {
        FMOD_ChannelGroup_SetVolume(fmod->masterGroup, 1.0f);
    }
    if (fmod->system != nullptr) {
        FMOD_System_Release(fmod->system);
    }

    free(fmod);
}

static void fmodUpdate(AudioSystem* audio, float deltaTime) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmod->system != nullptr) {
        FMOD_System_Update(fmod->system);
    }
    (void)deltaTime;
}

static int32_t fmodPlaySound(AudioSystem* audio, int32_t soundIndex, int32_t priority, bool loop) {
    (void)priority;
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmod->system == nullptr || audio->dw == nullptr) {
        return -1;
    }
    if (soundIndex < 0 || (uint32_t)soundIndex >= audio->dw->sond.count) {
        return -1;
    }

    FmodSoundInstance* slot = fmodFindFreeSlot(fmod);
    if (slot == nullptr) {
        return -1;
    }

    const Sound* sound = &audio->dw->sond.sounds[soundIndex];
    char* resolved = fmodResolveSoundPath(fmod, sound);
    FMOD_SOUND* soundHandle = nullptr;
    FMOD_RESULT result = FMOD_OK;

    if (resolved != nullptr) {
        result = FMOD_System_CreateSound(
            fmod->system,
            resolved,
            loop ? FMOD_LOOP_NORMAL : FMOD_DEFAULT,
            nullptr,
            &soundHandle
        );
        free(resolved);
    }

    if (result != FMOD_OK || soundHandle == nullptr) {
        logWarn("Audio: FMOD failed to create sound %d (result=%d)\n", soundIndex, result);
        return -1;
    }

    FMOD_CHANNEL* channel = nullptr;
    result = FMOD_System_PlaySound(fmod->system, soundHandle, fmod->masterGroup, 0, &channel);
    if (result != FMOD_OK || channel == nullptr) {
        FMOD_Sound_Release(soundHandle);
        return -1;
    }

    slot->active = true;
    slot->soundIndex = soundIndex;
    slot->instanceId = FMOD_SOUND_INSTANCE_BASE + fmod->nextInstanceId++;
    slot->channel = channel;
    slot->sound = soundHandle;
    slot->gain = sound->volume;
    slot->pitch = sound->pitch;
    slot->loop = loop;

    FMOD_Channel_SetVolume(slot->channel, slot->gain * AudioSystem_soundGroupGain(audio, soundIndex));
    FMOD_Channel_SetPitch(slot->channel, slot->pitch);
    return slot->instanceId;
}

static void fmodSetSoundSpatial(AudioSystem* audio, int32_t instanceId, float x, float y, float z, float ref, float max, float factor) {
    (void)audio; (void)instanceId; (void)x; (void)y; (void)z; (void)ref; (void)max; (void)factor;
}

static void fmodSetListenerPosition(AudioSystem* audio, float x, float y, float z) {
    (void)audio; (void)x; (void)y; (void)z;
}

static void fmodStopSound(AudioSystem* audio, int32_t soundOrInstance) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmodIsValidInstanceId(soundOrInstance)) {
        FmodSoundInstance* inst = fmodFindInstanceById(fmod, soundOrInstance);
        if (inst != nullptr) {
            FMOD_Channel_Stop(inst->channel);
            if (inst->sound != nullptr) {
                FMOD_Sound_Release(inst->sound);
            }
            memset(inst, 0, sizeof(*inst));
        }
    } else {
        repeat(MAX_FMOD_SOUNDS, i) {
            FmodSoundInstance* inst = &fmod->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                FMOD_Channel_Stop(inst->channel);
                if (inst->sound != nullptr) {
                    FMOD_Sound_Release(inst->sound);
                }
                memset(inst, 0, sizeof(*inst));
            }
        }
    }
}

static void fmodStopAll(AudioSystem* audio) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    repeat(MAX_FMOD_SOUNDS, i) {
        if (fmod->instances[i].active) {
            FMOD_Channel_Stop(fmod->instances[i].channel);
            if (fmod->instances[i].sound != nullptr) {
                FMOD_Sound_Release(fmod->instances[i].sound);
            }
            memset(&fmod->instances[i], 0, sizeof(fmod->instances[i]));
        }
    }
}

static bool fmodIsPlaying(AudioSystem* audio, int32_t soundOrInstance) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmodIsValidInstanceId(soundOrInstance)) {
        FmodSoundInstance* inst = fmodFindInstanceById(fmod, soundOrInstance);
        if (inst == nullptr || inst->channel == nullptr) return false;
        int isPlaying = 0;
        FMOD_Channel_IsPlaying(inst->channel, &isPlaying);
        return isPlaying != 0;
    }
    repeat(MAX_FMOD_SOUNDS, i) {
        FmodSoundInstance* inst = &fmod->instances[i];
        if (inst->active && inst->soundIndex == soundOrInstance && inst->channel != nullptr) {
            int isPlaying = 0;
            FMOD_Channel_IsPlaying(inst->channel, &isPlaying);
            if (isPlaying) return true;
        }
    }
    return false;
}

static void fmodPauseSound(AudioSystem* audio, int32_t soundOrInstance) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmodIsValidInstanceId(soundOrInstance)) {
        FmodSoundInstance* inst = fmodFindInstanceById(fmod, soundOrInstance);
        if (inst != nullptr && inst->channel != nullptr) {
            FMOD_Channel_SetPaused(inst->channel, 1);
        }
    }
}

static void fmodResumeSound(AudioSystem* audio, int32_t soundOrInstance) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmodIsValidInstanceId(soundOrInstance)) {
        FmodSoundInstance* inst = fmodFindInstanceById(fmod, soundOrInstance);
        if (inst != nullptr && inst->channel != nullptr) {
            FMOD_Channel_SetPaused(inst->channel, 0);
        }
    }
}

static void fmodPauseAll(AudioSystem* audio) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    repeat(MAX_FMOD_SOUNDS, i) {
        if (fmod->instances[i].active && fmod->instances[i].channel != nullptr) {
            FMOD_Channel_SetPaused(fmod->instances[i].channel, 1);
        }
    }
}

static void fmodResumeAll(AudioSystem* audio) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    repeat(MAX_FMOD_SOUNDS, i) {
        if (fmod->instances[i].active && fmod->instances[i].channel != nullptr) {
            FMOD_Channel_SetPaused(fmod->instances[i].channel, 0);
        }
    }
}

static void fmodSuspend(AudioSystem* audio) {
    (void)audio;
}

static void fmodResumePlayback(AudioSystem* audio) {
    (void)audio;
}

static void fmodSetSoundGain(AudioSystem* audio, int32_t soundOrInstance, float gain, uint32_t timeMs) {
    (void)timeMs;
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmodIsValidInstanceId(soundOrInstance)) {
        FmodSoundInstance* inst = fmodFindInstanceById(fmod, soundOrInstance);
        if (inst != nullptr && inst->channel != nullptr) {
            inst->gain = gain;
            FMOD_Channel_SetVolume(inst->channel, gain * AudioSystem_soundGroupGain(audio, inst->soundIndex));
        }
    } else {
        repeat(MAX_FMOD_SOUNDS, i) {
            if (fmod->instances[i].active && fmod->instances[i].soundIndex == soundOrInstance && fmod->instances[i].channel != nullptr) {
                fmod->instances[i].gain = gain;
                FMOD_Channel_SetVolume(fmod->instances[i].channel, gain * AudioSystem_soundGroupGain(audio, fmod->instances[i].soundIndex));
            }
        }
    }
}

static float fmodGetSoundGain(AudioSystem* audio, int32_t soundOrInstance) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmodIsValidInstanceId(soundOrInstance)) {
        FmodSoundInstance* inst = fmodFindInstanceById(fmod, soundOrInstance);
        if (inst != nullptr) return inst->gain;
        return 0.0f;
    }
    repeat(MAX_FMOD_SOUNDS, i) {
        FmodSoundInstance* inst = &fmod->instances[i];
        if (inst->active && inst->soundIndex == soundOrInstance) return inst->gain;
    }
    return 0.0f;
}

static void fmodSetSoundPitch(AudioSystem* audio, int32_t soundOrInstance, float pitch) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmodIsValidInstanceId(soundOrInstance)) {
        FmodSoundInstance* inst = fmodFindInstanceById(fmod, soundOrInstance);
        if (inst != nullptr && inst->channel != nullptr) {
            inst->pitch = pitch;
            FMOD_Channel_SetPitch(inst->channel, pitch);
        }
    } else {
        repeat(MAX_FMOD_SOUNDS, i) {
            if (fmod->instances[i].active && fmod->instances[i].soundIndex == soundOrInstance && fmod->instances[i].channel != nullptr) {
                fmod->instances[i].pitch = pitch;
                FMOD_Channel_SetPitch(fmod->instances[i].channel, pitch);
            }
        }
    }
}

static float fmodGetSoundPitch(AudioSystem* audio, int32_t soundOrInstance) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmodIsValidInstanceId(soundOrInstance)) {
        FmodSoundInstance* inst = fmodFindInstanceById(fmod, soundOrInstance);
        if (inst != nullptr) return inst->pitch;
        return 1.0f;
    }
    repeat(MAX_FMOD_SOUNDS, i) {
        if (fmod->instances[i].active && fmod->instances[i].soundIndex == soundOrInstance) {
            return fmod->instances[i].pitch;
        }
    }
    return 1.0f;
}

static float fmodGetTrackPosition(AudioSystem* audio, int32_t soundOrInstance) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmodIsValidInstanceId(soundOrInstance)) {
        FmodSoundInstance* inst = fmodFindInstanceById(fmod, soundOrInstance);
        if (inst != nullptr && inst->channel != nullptr) {
            unsigned int position = 0;
            FMOD_Channel_GetPosition(inst->channel, &position, FMOD_DEFAULT);
            return (float)position / 44100.0f;
        }
    }
    return 0.0f;
}

static void fmodSetTrackPosition(AudioSystem* audio, int32_t soundOrInstance, float positionSeconds) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmodIsValidInstanceId(soundOrInstance)) {
        FmodSoundInstance* inst = fmodFindInstanceById(fmod, soundOrInstance);
        if (inst != nullptr && inst->channel != nullptr) {
            unsigned int position = (unsigned int)(positionSeconds * 44100.0f);
            FMOD_Channel_SetPosition(inst->channel, position, FMOD_DEFAULT);
        }
    }
}

static float fmodGetSoundLength(AudioSystem* audio, int32_t soundOrInstance) {
    (void)audio; (void)soundOrInstance;
    return 0.0f;
}

static void fmodSetMasterGain(AudioSystem* audio, float gain) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)audio;
    if (fmod->masterGroup != nullptr) {
        FMOD_ChannelGroup_SetVolume(fmod->masterGroup, gain);
    }
}

static void fmodSetMasterGainForListener(AudioSystem* audio, float gain, int32_t listenerId) {
    (void)audio; (void)gain; (void)listenerId;
}

static void fmodSetChannelCount(AudioSystem* audio, int32_t count) {
    (void)audio; (void)count;
}

static void fmodSetGroupGain(AudioSystem* audio, int32_t groupIndex, float gain, uint32_t timeMs) {
    AudioSystem_setGroupGain(audio, groupIndex, gain, timeMs);
}

static void fmodGroupLoad(AudioSystem* audio, int32_t groupIndex) {
    (void)audio; (void)groupIndex;
}

static bool fmodGroupIsLoaded(AudioSystem* audio, int32_t groupIndex) {
    (void)audio; (void)groupIndex;
    return true;
}

static int32_t fmodCreateStream(AudioSystem* audio, const char* filename) {
    (void)audio; (void)filename;
    return -1;
}

static bool fmodDestroyStream(AudioSystem* audio, int32_t streamIndex) {
    (void)audio; (void)streamIndex;
    return false;
}

static AudioSystemVtable fmodAudioSystemVtable;

FmodAudioSystem* FmodAudioSystem_create(DataWin* dataWin) {
    FmodAudioSystem* fmod = (FmodAudioSystem*)safeCalloc(1, sizeof(FmodAudioSystem));
    fmod->base.dw = dataWin;

    fmodAudioSystemVtable.init = fmodInit;
    fmodAudioSystemVtable.destroy = fmodDestroy;
    fmodAudioSystemVtable.update = fmodUpdate;
    fmodAudioSystemVtable.playSound = fmodPlaySound;
    fmodAudioSystemVtable.setSoundSpatial = fmodSetSoundSpatial;
    fmodAudioSystemVtable.setListenerPosition = fmodSetListenerPosition;
    fmodAudioSystemVtable.stopSound = fmodStopSound;
    fmodAudioSystemVtable.stopAll = fmodStopAll;
    fmodAudioSystemVtable.isPlaying = fmodIsPlaying;
    fmodAudioSystemVtable.pauseSound = fmodPauseSound;
    fmodAudioSystemVtable.resumeSound = fmodResumeSound;
    fmodAudioSystemVtable.pauseAll = fmodPauseAll;
    fmodAudioSystemVtable.resumeAll = fmodResumeAll;
    fmodAudioSystemVtable.suspend = fmodSuspend;
    fmodAudioSystemVtable.resume = fmodResumePlayback;
    fmodAudioSystemVtable.setSoundGain = fmodSetSoundGain;
    fmodAudioSystemVtable.getSoundGain = fmodGetSoundGain;
    fmodAudioSystemVtable.setSoundPitch = fmodSetSoundPitch;
    fmodAudioSystemVtable.getSoundPitch = fmodGetSoundPitch;
    fmodAudioSystemVtable.getTrackPosition = fmodGetTrackPosition;
    fmodAudioSystemVtable.setTrackPosition = fmodSetTrackPosition;
    fmodAudioSystemVtable.getSoundLength = fmodGetSoundLength;
    fmodAudioSystemVtable.setMasterGain = fmodSetMasterGain;
    fmodAudioSystemVtable.setMasterGainForListener = fmodSetMasterGainForListener;
    fmodAudioSystemVtable.setChannelCount = fmodSetChannelCount;
    fmodAudioSystemVtable.setGroupGain = fmodSetGroupGain;
    fmodAudioSystemVtable.groupLoad = fmodGroupLoad;
    fmodAudioSystemVtable.groupIsLoaded = fmodGroupIsLoaded;
    fmodAudioSystemVtable.createStream = fmodCreateStream;
    fmodAudioSystemVtable.destroyStream = fmodDestroyStream;

    fmod->base.vtable = &fmodAudioSystemVtable;
    return fmod;
}

