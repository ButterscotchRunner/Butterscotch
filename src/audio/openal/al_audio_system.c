// On Windows, include windows.h first so its headers are processed before stb_vorbis
// defines single-letter macros (L, C, R) that conflict with winnt.h struct field names.
#ifdef _WIN32
#include <windows.h>
#endif

#include "stb_vorbis.c"
#include "al_audio_system.h"
#include "data_win.h"
#include "utils.h"
#include "wave.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb_ds.h"

// ===[ Helpers ]===

static bool alSourceIsPlaying(ALuint source) {
    ALint state;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

static bool alSourceHasStopped(ALuint source) {
    ALint state;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return state == AL_STOPPED;
}

static bool alSourceIsLooping(ALuint source) {
    ALint state;
    alGetSourcei(source, AL_LOOPING, &state);
    return state != AL_FALSE;
}

// Source - https://stackoverflow.com/a/7995655
// Posted by Karl
// Retrieved 2026-05-05, License - CC BY-SA 3.0
static void alGetSourceLengthSec(ALuint buffer, float* out) {
    ALint sizeInBytes;
    ALint channels;
    ALint bits;

    alGetBufferi(buffer, AL_SIZE, &sizeInBytes);
    alGetBufferi(buffer, AL_CHANNELS, &channels);
    alGetBufferi(buffer, AL_BITS, &bits);

    int lengthInSamples = sizeInBytes * 8 / (channels * bits);
    ALint frequency;

    alGetBufferi(buffer, AL_FREQUENCY, &frequency);

    *out = (float)lengthInSamples / (float)frequency;
}

static SoundInstance* findFreeSlot(AlAudioSystem* ma) {
    // First pass: find an inactive slot
    repeat(MAX_SOUND_INSTANCES, i) {
        if (!ma->instances[i].active) {
            return &ma->instances[i];
        }
    }

    // Second pass: evict the lowest-priority ended sound
    SoundInstance* best = nullptr;
    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (!alSourceIsPlaying(inst->alSource)) {
            if (best == nullptr || best->priority > inst->priority) {
                best = inst;
            }
        }
    }

    if (best != nullptr) {
        alDeleteSources(1, &best->alSource);
        alDeleteBuffers(1, &best->alBuffer);
        best->active = false;
    }

    return best;
}

static SoundInstance* findInstanceById(AlAudioSystem* ma, int32_t instanceId) {
    int32_t slotIndex = instanceId - SOUND_INSTANCE_ID_BASE;
    if (0 > slotIndex || slotIndex >= MAX_SOUND_INSTANCES) return nullptr;
    SoundInstance* inst = &ma->instances[slotIndex];
    if (!inst->active || inst->instanceId != instanceId) return nullptr;
    return inst;
}

// Helper: resolve external audio file path from Sound entry
static char* resolveExternalPath(AlAudioSystem* ma, Sound* sound) {
    const char* file = sound->file;
    if (file == nullptr || file[0] == '\0') return nullptr;

    // If the filename has no extension, append ".ogg"
    bool hasExtension = (strchr(file, '.') != nullptr);

    char filename[512];
    if (hasExtension) {
        snprintf(filename, sizeof(filename), "%s", file);
    } else {
        snprintf(filename, sizeof(filename), "%s.ogg", file);
    }

    return ma->fileSystem->vtable->resolvePath(ma->fileSystem, filename);
}

// ===[ Vtable Implementations ]===

static void maInit(AudioSystem* audio, DataWin* dataWin, FileSystem* fileSystem) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    arrput(ma->base.audioGroups, dataWin);
    ma->fileSystem = fileSystem;

    ma->alDevice = alcOpenDevice(nullptr);
    ma->alContext = alcCreateContext(ma->alDevice, nullptr);
    alcMakeContextCurrent(ma->alContext);
    if (ma->alDevice == nullptr || ma->alContext == nullptr) {
        fprintf(stderr, "Audio: Failed to initialize OpenAL engine (error %d)\n", alGetError());
        return;
    }

    memset(ma->instances, 0, sizeof(ma->instances));
    ma->nextInstanceCounter = 0;

    fprintf(stderr, "Audio: OpenAL engine initialized\n");
}

static void maDestroy(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    // Uninit all active sound instances
    repeat(MAX_SOUND_INSTANCES, i) {
        if (ma->instances[i].active) {
            alDeleteSources(1, &ma->instances[i].alSource);
            alDeleteBuffers(1, &ma->instances[i].alBuffer);
            ma->instances[i].active = false;
        }
    }

    // Free stream entries
    repeat(MAX_AUDIO_STREAMS, i) {
        if (ma->streams[i].active) {
            free(ma->streams[i].filePath);
        }
    }

    // Free loaded audio groups. The main data.win is owned by the caller, so skip index 0.
    if (arrlen(ma->base.audioGroups) > 1) {
        for (int32_t i = 1; i < (int32_t) arrlen(ma->base.audioGroups); i++) {
            DataWin_free(ma->base.audioGroups[i]);
        }
    }
    arrfree(ma->base.audioGroups);

    alcMakeContextCurrent(nullptr);
    alcDestroyContext(ma->alContext);
    alcCloseDevice(ma->alDevice);
    free(ma);
}

static void maUpdate(AudioSystem* audio, float deltaTime) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (!inst->active) continue;

        // Handle gain fading (for cases where we do manual fading)
        if (inst->fadeTimeRemaining > 0.0f) {
            inst->fadeTimeRemaining -= deltaTime;
            if (0.0f >= inst->fadeTimeRemaining) {
                inst->fadeTimeRemaining = 0.0f;
                inst->currentGain = inst->targetGain;
            } else {
                float t = 1.0f - (inst->fadeTimeRemaining / inst->fadeTotalTime);
                inst->currentGain = inst->startGain + (inst->targetGain - inst->startGain) * t;
            }
            alSourcef(inst->alSource, AL_GAIN, inst->currentGain);
        }

        // Clean up ended non-looping sounds (ma_sound_at_end avoids reaping still-loading async sounds)
        if (alSourceHasStopped(inst->alSource) && !alSourceIsLooping(inst->alSource)) {
            alDeleteSources(1, &inst->alSource);
            alDeleteBuffers(1, &inst->alBuffer);
            inst->active = false;
        }
    }
}

static int32_t maPlaySound(AudioSystem* audio, int32_t soundIndex, int32_t priority, bool loop) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    // Check if this is a stream index (created by audio_create_stream)
    bool isStream = (soundIndex >= AUDIO_STREAM_INDEX_BASE);
    Sound* sound = nullptr;
    char* streamPath = nullptr;

    if (isStream) {
        int32_t streamSlot = soundIndex - AUDIO_STREAM_INDEX_BASE;
        if (0 > streamSlot || streamSlot >= MAX_AUDIO_STREAMS || !ma->streams[streamSlot].active) {
            fprintf(stderr, "Audio: Invalid stream index %d\n", soundIndex);
            return -1;
        }
        streamPath = ma->streams[streamSlot].filePath;
    } else {
        DataWin* dw = ma->base.audioGroups[0]; // Audio Group 0 should always be data.win
        if (0 > soundIndex || (uint32_t) soundIndex >= dw->sond.count) {
            fprintf(stderr, "Audio: Invalid sound index %d\n", soundIndex);
            return -1;
        }
        sound = &dw->sond.sounds[soundIndex];
    }

    SoundInstance* slot = findFreeSlot(ma);
    if (slot == nullptr) {
        fprintf(stderr, "Audio: No free sound slots for sound %d\n", soundIndex);
        return -1;
    }

    int32_t slotIndex = (int32_t) (slot - ma->instances);
    
    alGenSources(1, &slot->alSource);
    alGenBuffers(1, &slot->alBuffer);
    if (isStream) {
        int channels;
        int sample_rate;
        short* data = NULL;
        int len = stb_vorbis_decode_filename(streamPath, &channels, &sample_rate, &data);
        alBufferData(
            slot->alBuffer, 
            (channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, 
            (void*)data, 
            len*channels*sizeof(uint16_t), 
            sample_rate
        );
        alSourcei(slot->alSource, AL_BUFFER, slot->alBuffer);
        if(data != NULL) free(data);
    } else {
        bool isEmbedded = (sound->flags & 0x01) != 0;
        bool isCompressed = (sound->flags & 0x02) != 0;

        if (isEmbedded || isCompressed) {
           // Embedded audio: decode from AUDO chunk memory
            if (0 > sound->audioFile || (uint32_t) sound->audioFile >= ma->base.audioGroups[sound->audioGroup]->audo.count) {
                fprintf(stderr, "Audio: Invalid audio file index %d for sound '%s'\n", sound->audioFile, sound->name);
                return -1;
            }

            AudioEntry* entry = &ma->base.audioGroups[sound->audioGroup]->audo.entries[sound->audioFile];
            WAVFile wav = WAV_ParseFileData(entry->data);

            uint32_t format;
            if (wav.header.number_of_channels == 1)
            {
                if (wav.header.bits_per_sample == 8)
                    format = AL_FORMAT_MONO8;
                else 
                    format = AL_FORMAT_MONO16;
            }
            else {
                if (wav.header.bits_per_sample == 8)
                    format = AL_FORMAT_STEREO8;
                else
                    format = AL_FORMAT_STEREO16;
            }
            alBufferData(
                slot->alBuffer, 
                format, 
                wav.data, 
                wav.data_length, 
                wav.header.sample_rate
            );
            alSourcei(slot->alSource, AL_BUFFER, slot->alBuffer);
            if(wav.data != NULL) free(wav.data);
        } else {
            // External audio: load from file
            char* path = resolveExternalPath(ma, sound);
            if (path == nullptr) {
                fprintf(stderr, "Audio: Could not resolve path for sound '%s'\n", sound->name);
                return -1;
            }

            int channels;
            int sample_rate;
            short* data = NULL;
            int len = stb_vorbis_decode_filename(path, &channels, &sample_rate, &data);
            alBufferData(
                slot->alBuffer, 
                (channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, 
                (void*)data, 
                len*channels*sizeof(uint16_t), 
                sample_rate
            );
            alSourcei(slot->alSource, AL_BUFFER, slot->alBuffer);
            if(data != NULL) free(data);
            free(path);
        }
    }

    // Apply properties
    float volume = isStream ? 1.0f : sound->volume;
    float pitch = isStream ? 1.0f : sound->pitch;
    alSourcef(slot->alSource, AL_GAIN, volume);

    if (pitch != 1.0f) {
        alSourcef(slot->alSource, AL_PITCH, pitch != 0.0f ? pitch : 1.0f);
    }
    alSourcei(slot->alSource, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);

    // Set up instance tracking
    slot->active = true;
    slot->soundIndex = soundIndex;
    slot->instanceId = SOUND_INSTANCE_ID_BASE + slotIndex;
    slot->currentGain = volume;
    slot->targetGain = volume;
    slot->fadeTimeRemaining = 0.0f;
    slot->fadeTotalTime = 0.0f;
    slot->startGain = volume;
    slot->priority = priority;

    // Track unique IDs for disambiguation
    ma->nextInstanceCounter++;

    alSourcePlay(slot->alSource);

    return slot->instanceId;
}

static void maStopSound(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        // Stop specific instance
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            alSourceStop(inst->alSource);
            alDeleteSources(1, &inst->alSource);
            alDeleteBuffers(1, &inst->alBuffer);
            inst->active = false;
        }
    } else {
        // Stop all instances of this sound resource
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                alSourceStop(inst->alSource);
                alDeleteSources(1, &inst->alSource);
                alDeleteBuffers(1, &inst->alBuffer);
                inst->active = false;
            }
        }
    }
}

static void maStopAll(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (inst->active) {
            alSourceStop(inst->alSource);
            alDeleteSources(1, &inst->alSource);
            alDeleteBuffers(1, &inst->alBuffer);
            inst->active = false;
        }
    }
}

static bool maIsPlaying(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        return inst != nullptr && alSourceIsPlaying(inst->alSource);
    } else {
        // Check if any instance of this sound resource is playing
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance && alSourceIsPlaying(inst->alSource)) {
                return true;
            }
        }
        return false;
    }
}

static void maPauseSound(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            alSourcePause(inst->alSource);
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                alSourcePause(inst->alSource);
            }
        }
    }
}

static void maResumeSound(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            alSourcePlay(inst->alSource);
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                alSourcePlay(inst->alSource);
            }
        }
    }
}

static void maPauseAll(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (inst->active && alSourceIsPlaying(inst->alSource)) {
            alSourcePause(inst->alSource);
        }
    }
}

static void maResumeAll(AudioSystem* audio) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (inst->active) {
            alSourcePlay(inst->alSource);
        }
    }
}

static void maSetSoundGain(AudioSystem* audio, int32_t soundOrInstance, float gain, uint32_t timeMs) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            if (timeMs == 0) {
                inst->currentGain = gain;
                inst->targetGain = gain;
                inst->fadeTimeRemaining = 0.0f;
                alSourcef(inst->alSource, AL_GAIN, gain);
            } else {
                inst->startGain = inst->currentGain;
                inst->targetGain = gain;
                inst->fadeTotalTime = (float) timeMs / 1000.0f;
                inst->fadeTimeRemaining = inst->fadeTotalTime;
            }
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                if (timeMs == 0) {
                    inst->currentGain = gain;
                    inst->targetGain = gain;
                    inst->fadeTimeRemaining = 0.0f;
                    alSourcef(inst->alSource, AL_GAIN, gain);
                } else {
                    inst->startGain = inst->currentGain;
                    inst->targetGain = gain;
                    inst->fadeTotalTime = (float) timeMs / 1000.0f;
                    inst->fadeTimeRemaining = inst->fadeTotalTime;
                }
            }
        }
    }
}

static float maGetSoundGain(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) return inst->currentGain;
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                return inst->currentGain;
            }
        }
    }
    return 0.0f;
}

static void maSetSoundPitch(AudioSystem* audio, int32_t soundOrInstance, float pitch) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            alSourcef(inst->alSource, AL_PITCH, pitch);
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                alSourcef(inst->alSource, AL_PITCH, pitch);
            }
        }
    }
}

static float maGetSoundPitch(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    float pitch = 1.0f;
    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) alGetSourcef(inst->alSource, AL_PITCH, &pitch);
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                alGetSourcef(inst->alSource, AL_PITCH, &pitch);
            }
        }
    }
    return pitch;
}

static float maGetTrackPosition(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            float cursor;
            alGetSourcef(inst->alSource, AL_SEC_OFFSET, &cursor);
            return cursor;
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                float cursor;
                alGetSourcef(inst->alSource, AL_SEC_OFFSET, &cursor);
                return cursor;
            }
        }
    }
    return 0.0f;
}

static void maSetTrackPosition(AudioSystem* audio, int32_t soundOrInstance, float positionSeconds) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        SoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            alSourcef(inst->alSource, AL_SEC_OFFSET, positionSeconds);
        }
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
            alSourcef(inst->alSource, AL_SEC_OFFSET, positionSeconds);
            }
        }
    }
}

// Total length of a loaded sound. Works on both SOND index and active instance ids.
static float maGetSoundLength(AudioSystem* audio, int32_t soundOrInstance) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    SoundInstance* match = nullptr;
    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE) {
        match = findInstanceById(ma, soundOrInstance);
    } else {
        repeat(MAX_SOUND_INSTANCES, i) {
            SoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                match = inst;
                break;
            }
        }
    }
    if (match != nullptr) {
        float seconds = 0.0f;
        alGetSourceLengthSec(match->alBuffer, &seconds);
        return seconds;
    }

    // No active instance: GMS audio_sound_length(soundIndex) must still return the asset's duration.
    if (soundOrInstance >= SOUND_INSTANCE_ID_BASE || soundOrInstance >= AUDIO_STREAM_INDEX_BASE)
        return 0.0f;

    DataWin* dw = ma->base.audioGroups[0];
    if (dw == nullptr || 0 > soundOrInstance || (uint32_t) soundOrInstance >= dw->sond.count)
        return 0.0f;

    Sound* sound = &dw->sond.sounds[soundOrInstance];

    bool isEmbedded = (sound->flags & 0x01) != 0;
    bool isCompressed = (sound->flags & 0x02) != 0;
    if (isEmbedded || isCompressed) {
        if (0 > sound->audioFile || (uint32_t) sound->audioFile >= ma->base.audioGroups[sound->audioGroup]->audo.count) return 0.0f;
        AudioEntry* entry = &ma->base.audioGroups[sound->audioGroup]->audo.entries[sound->audioFile];
        WAVFile wav = WAV_ParseFileData(entry->data);
        float seconds = 0.0f;
        if (wav.header.byte_rate > 0) seconds = (float) wav.header.data_size / (float) wav.header.byte_rate;
        if (wav.data != nullptr) free(wav.data);
        return seconds;
    }

    char* path = resolveExternalPath(ma, sound);
    if (path == nullptr) return 0.0f;
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path, &err, nullptr);
    free(path);
    if (v == nullptr) return 0.0f;
    float seconds = stb_vorbis_stream_length_in_seconds(v);
    stb_vorbis_close(v);
    return seconds;
}

static void maSetMasterGain(AudioSystem* audio, float gain) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;
    alListenerf(AL_GAIN, gain);
}

static void maSetChannelCount(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t count) {
    // miniaudio handles channel management internally, this is a no-op
}

static void maGroupLoad(AudioSystem* audio, int32_t groupIndex) {
    if (groupIndex > 0) {
        int sz = snprintf(nullptr, 0, "audiogroup%d.dat", groupIndex);
        char buf[sz + 1];
        snprintf(buf, sizeof(buf), "audiogroup%d.dat", groupIndex);
        DataWin *audioGroup = DataWin_parse(((AlAudioSystem*)audio)->fileSystem->vtable->resolvePath(((AlAudioSystem*)audio)->fileSystem, buf),
        (DataWinParserOptions) {
            .parseAudo = true,
        });
        arrput(audio->audioGroups, audioGroup);
    }
}

static bool maGroupIsLoaded(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t groupIndex) {
    return (arrlen(audio->audioGroups) > groupIndex);
}

// ===[ Audio Streams ]===

static int32_t maCreateStream(AudioSystem* audio, const char* filename) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    // Find a free stream slot
    int32_t freeSlot = -1;
    repeat(MAX_AUDIO_STREAMS, i) {
        if (!ma->streams[i].active) {
            freeSlot = (int32_t) i;
            break;
        }
    }

    if (0 > freeSlot) {
        fprintf(stderr, "Audio: No free stream slots for '%s'\n", filename);
        return -1;
    }

    char* resolved = ma->fileSystem->vtable->resolvePath(ma->fileSystem, filename);
    if (resolved == nullptr) {
        fprintf(stderr, "Audio: Could not resolve path for stream '%s'\n", filename);
        return -1;
    }

    ma->streams[freeSlot].active = true;
    ma->streams[freeSlot].filePath = resolved;

    int32_t streamIndex = AUDIO_STREAM_INDEX_BASE + freeSlot;
    fprintf(stderr, "Audio: Created stream %d for '%s' -> '%s'\n", streamIndex, filename, resolved);
    return streamIndex;
}

static bool maDestroyStream(AudioSystem* audio, int32_t streamIndex) {
    AlAudioSystem* ma = (AlAudioSystem*) audio;

    int32_t slotIndex = streamIndex - AUDIO_STREAM_INDEX_BASE;
    if (0 > slotIndex || slotIndex >= MAX_AUDIO_STREAMS) {
        fprintf(stderr, "Audio: Invalid stream index %d for destroy\n", streamIndex);
        return false;
    }

    AudioStreamEntry* entry = &ma->streams[slotIndex];
    if (!entry->active) return false;

    // Stop all sound instances that were playing this stream
    repeat(MAX_SOUND_INSTANCES, i) {
        SoundInstance* inst = &ma->instances[i];
        if (inst->active && inst->soundIndex == streamIndex) {
            alSourceStop(inst->alSource);
            alDeleteSources(1, &inst->alSource);
            alDeleteBuffers(1, &inst->alBuffer);
            inst->active = false;
        }
    }

    free(entry->filePath);
    entry->filePath = nullptr;
    entry->active = false;
    fprintf(stderr, "Audio: Destroyed stream %d\n", streamIndex);
    return true;
}

// ===[ Vtable ]===

static AudioSystemVtable AlAudioSystemVtable = {
    .init = maInit,
    .destroy = maDestroy,
    .update = maUpdate,
    .playSound = maPlaySound,
    .stopSound = maStopSound,
    .stopAll = maStopAll,
    .isPlaying = maIsPlaying,
    .pauseSound = maPauseSound,
    .resumeSound = maResumeSound,
    .pauseAll = maPauseAll,
    .resumeAll = maResumeAll,
    .setSoundGain = maSetSoundGain,
    .getSoundGain = maGetSoundGain,
    .setSoundPitch = maSetSoundPitch,
    .getSoundPitch = maGetSoundPitch,
    .getTrackPosition = maGetTrackPosition,
    .setTrackPosition = maSetTrackPosition,
    .getSoundLength = maGetSoundLength,
    .setMasterGain = maSetMasterGain,
    .setChannelCount = maSetChannelCount,
    .groupLoad = maGroupLoad,
    .groupIsLoaded = maGroupIsLoaded,
    .createStream = maCreateStream,
    .destroyStream = maDestroyStream,
};

// ===[ Lifecycle ]===

AlAudioSystem* AlAudioSystem_create(void) {
    AlAudioSystem* ma = safeCalloc(1, sizeof(AlAudioSystem));
    ma->base.vtable = &AlAudioSystemVtable;
    return ma;
}
