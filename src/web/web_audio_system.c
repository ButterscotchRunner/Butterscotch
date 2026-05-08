// Include stb_vorbis BEFORE miniaudio so that STB_VORBIS_INCLUDE_STB_VORBIS_H is defined,
// which enables miniaudio's built-in OGG Vorbis decoding support.
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "web_audio_system.h"
#include "data_win.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb_ds.h"

// ===[ Helpers ]===

static WebSoundInstance* findFreeSlot(WebAudioSystem* ma) {
    repeat(WEB_MAX_SOUND_INSTANCES, i) {
        if (!ma->instances[i].active) {
            return &ma->instances[i];
        }
    }

    WebSoundInstance* best = nullptr;
    repeat(WEB_MAX_SOUND_INSTANCES, i) {
        WebSoundInstance* inst = &ma->instances[i];
        if (!ma_sound_is_playing(&inst->maSound)) {
            if (best == nullptr || best->priority > inst->priority) {
                best = inst;
            }
        }
    }

    if (best != nullptr) {
        ma_sound_uninit(&best->maSound);
        if (best->ownsDecoder) {
            ma_decoder_uninit(&best->decoder);
        }
        best->active = false;
    }

    return best;
}

static WebSoundInstance* findInstanceById(WebAudioSystem* ma, int32_t instanceId) {
    int32_t slotIndex = instanceId - WEB_SOUND_INSTANCE_ID_BASE;
    if (0 > slotIndex || slotIndex >= WEB_MAX_SOUND_INSTANCES) return nullptr;
    WebSoundInstance* inst = &ma->instances[slotIndex];
    if (!inst->active || inst->instanceId != instanceId) return nullptr;
    return inst;
}

static char* resolveExternalPath(WebAudioSystem* ma, Sound* sound) {
    const char* file = sound->file;
    if (file == nullptr || file[0] == '\0') return nullptr;

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

static void webInit(AudioSystem* audio, DataWin* dataWin, FileSystem* fileSystem) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    arrput(ma->base.audioGroups, dataWin);
    ma->fileSystem = fileSystem;

    ma_engine_config config = ma_engine_config_init();
    config.noDevice = MA_TRUE;
    config.channels = 2;
    config.sampleRate = (ma_uint32) ma->sampleRate;

    ma_result result = ma_engine_init(&config, &ma->engine);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "Audio: Failed to initialize miniaudio engine in noDevice mode (error %d)\n", result);
        ma->engineReady = false;
        return;
    }
    ma->engineReady = true;

    memset(ma->instances, 0, sizeof(ma->instances));
    ma->nextInstanceCounter = 0;

    fprintf(stderr, "Audio: web miniaudio engine initialized (noDevice, %d Hz, 2 ch)\n", ma->sampleRate);
}

static void webDestroy(AudioSystem* audio) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;

    if (ma->engineReady) {
        repeat(WEB_MAX_SOUND_INSTANCES, i) {
            if (ma->instances[i].active) {
                ma_sound_uninit(&ma->instances[i].maSound);
                if (ma->instances[i].ownsDecoder) {
                    ma_decoder_uninit(&ma->instances[i].decoder);
                }
                ma->instances[i].active = false;
            }
        }

        repeat(WEB_MAX_AUDIO_STREAMS, i) {
            if (ma->streams[i].active) {
                free(ma->streams[i].filePath);
            }
        }

        if (arrlen(ma->base.audioGroups) > 1) {
            for (int32_t i = 1; i < (int32_t) arrlen(ma->base.audioGroups); i++) {
                DataWin_free(ma->base.audioGroups[i]);
            }
        }
        arrfree(ma->base.audioGroups);

        ma_engine_uninit(&ma->engine);
        ma->engineReady = false;
    }

    free(ma);
}

static void webUpdate(AudioSystem* audio, float deltaTime) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return;

    repeat(WEB_MAX_SOUND_INSTANCES, i) {
        WebSoundInstance* inst = &ma->instances[i];
        if (!inst->active) continue;

        if (inst->fadeTimeRemaining > 0.0f) {
            inst->fadeTimeRemaining -= deltaTime;
            if (0.0f >= inst->fadeTimeRemaining) {
                inst->fadeTimeRemaining = 0.0f;
                inst->currentGain = inst->targetGain;
            } else {
                float t = 1.0f - (inst->fadeTimeRemaining / inst->fadeTotalTime);
                inst->currentGain = inst->startGain + (inst->targetGain - inst->startGain) * t;
            }
            ma_sound_set_volume(&inst->maSound, inst->currentGain);
        }

        if (ma_sound_at_end(&inst->maSound) && !ma_sound_is_looping(&inst->maSound)) {
            ma_sound_uninit(&inst->maSound);
            if (inst->ownsDecoder) {
                ma_decoder_uninit(&inst->decoder);
            }
            inst->active = false;
        }
    }
}

static int32_t webPlaySound(AudioSystem* audio, int32_t soundIndex, int32_t priority, bool loop) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return -1;

    bool isStream = (soundIndex >= WEB_AUDIO_STREAM_INDEX_BASE);
    Sound* sound = nullptr;
    char* streamPath = nullptr;

    if (isStream) {
        int32_t streamSlot = soundIndex - WEB_AUDIO_STREAM_INDEX_BASE;
        if (0 > streamSlot || streamSlot >= WEB_MAX_AUDIO_STREAMS || !ma->streams[streamSlot].active) {
            fprintf(stderr, "Audio: Invalid stream index %d\n", soundIndex);
            return -1;
        }
        streamPath = ma->streams[streamSlot].filePath;
    } else {
        DataWin* dw = ma->base.audioGroups[0];
        if (0 > soundIndex || (uint32_t) soundIndex >= dw->sond.count) {
            fprintf(stderr, "Audio: Invalid sound index %d\n", soundIndex);
            return -1;
        }
        sound = &dw->sond.sounds[soundIndex];
    }

    WebSoundInstance* slot = findFreeSlot(ma);
    if (slot == nullptr) {
        fprintf(stderr, "Audio: No free sound slots for sound %d\n", soundIndex);
        return -1;
    }

    int32_t slotIndex = (int32_t) (slot - ma->instances);
    ma_result result;

    if (isStream) {
        result = ma_sound_init_from_file(&ma->engine, streamPath, MA_SOUND_FLAG_ASYNC, nullptr, nullptr, &slot->maSound);
        if (result != MA_SUCCESS) {
            fprintf(stderr, "Audio: Failed to load stream file '%s' (error %d)\n", streamPath, result);
            return -1;
        }
        slot->ownsDecoder = false;
    } else {
        bool isEmbedded = (sound->flags & 0x01) != 0;
        bool isCompressed = (sound->flags & 0x02) != 0;

        if (isEmbedded || isCompressed) {
            if (0 > sound->audioFile || (uint32_t) sound->audioFile >= ma->base.audioGroups[sound->audioGroup]->audo.count) {
                fprintf(stderr, "Audio: Invalid audio file index %d for sound '%s'\n", sound->audioFile, sound->name);
                return -1;
            }

            AudioEntry* entry = &ma->base.audioGroups[sound->audioGroup]->audo.entries[sound->audioFile];

            ma_decoder_config decoderConfig = ma_decoder_config_init_default();
            result = ma_decoder_init_memory(entry->data, entry->dataSize, &decoderConfig, &slot->decoder);
            if (result != MA_SUCCESS) {
                fprintf(stderr, "Audio: Failed to init decoder for '%s' (error %d)\n", sound->name, result);
                return -1;
            }
            slot->ownsDecoder = true;

            result = ma_sound_init_from_data_source(&ma->engine, &slot->decoder, 0, nullptr, &slot->maSound);
            if (result != MA_SUCCESS) {
                fprintf(stderr, "Audio: Failed to init sound from decoder for '%s' (error %d)\n", sound->name, result);
                ma_decoder_uninit(&slot->decoder);
                return -1;
            }
        } else {
            char* path = resolveExternalPath(ma, sound);
            if (path == nullptr) {
                fprintf(stderr, "Audio: Could not resolve path for sound '%s'\n", sound->name);
                return -1;
            }

            result = ma_sound_init_from_file(&ma->engine, path, MA_SOUND_FLAG_ASYNC, nullptr, nullptr, &slot->maSound);
            if (result != MA_SUCCESS) {
                fprintf(stderr, "Audio: Failed to load file for '%s' at '%s' (error %d)\n", sound->name, path, result);
                free(path);
                return -1;
            }
            free(path);
            slot->ownsDecoder = false;
        }
    }

    float volume = isStream ? 1.0f : sound->volume;
    float pitch = isStream ? 1.0f : sound->pitch;
    ma_sound_set_volume(&slot->maSound, volume);
    if (pitch != 1.0f) {
        ma_sound_set_pitch(&slot->maSound, pitch);
    }
    ma_sound_set_looping(&slot->maSound, loop);

    slot->active = true;
    slot->soundIndex = soundIndex;
    slot->instanceId = WEB_SOUND_INSTANCE_ID_BASE + slotIndex;
    slot->currentGain = volume;
    slot->targetGain = volume;
    slot->fadeTimeRemaining = 0.0f;
    slot->fadeTotalTime = 0.0f;
    slot->startGain = volume;
    slot->priority = priority;

    ma->nextInstanceCounter++;

    ma_sound_start(&slot->maSound);

    return slot->instanceId;
}

static void webStopSound(AudioSystem* audio, int32_t soundOrInstance) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return;

    if (soundOrInstance >= WEB_SOUND_INSTANCE_ID_BASE) {
        WebSoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            ma_sound_stop(&inst->maSound);
            ma_sound_uninit(&inst->maSound);
            if (inst->ownsDecoder) ma_decoder_uninit(&inst->decoder);
            inst->active = false;
        }
    } else {
        repeat(WEB_MAX_SOUND_INSTANCES, i) {
            WebSoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                ma_sound_stop(&inst->maSound);
                ma_sound_uninit(&inst->maSound);
                if (inst->ownsDecoder) ma_decoder_uninit(&inst->decoder);
                inst->active = false;
            }
        }
    }
}

static void webStopAll(AudioSystem* audio) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return;

    repeat(WEB_MAX_SOUND_INSTANCES, i) {
        WebSoundInstance* inst = &ma->instances[i];
        if (inst->active) {
            ma_sound_stop(&inst->maSound);
            ma_sound_uninit(&inst->maSound);
            if (inst->ownsDecoder) ma_decoder_uninit(&inst->decoder);
            inst->active = false;
        }
    }
}

static bool webIsPlaying(AudioSystem* audio, int32_t soundOrInstance) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return false;

    if (soundOrInstance >= WEB_SOUND_INSTANCE_ID_BASE) {
        WebSoundInstance* inst = findInstanceById(ma, soundOrInstance);
        return inst != nullptr && ma_sound_is_playing(&inst->maSound);
    }
    repeat(WEB_MAX_SOUND_INSTANCES, i) {
        WebSoundInstance* inst = &ma->instances[i];
        if (inst->active && inst->soundIndex == soundOrInstance && ma_sound_is_playing(&inst->maSound)) {
            return true;
        }
    }
    return false;
}

static void webPauseSound(AudioSystem* audio, int32_t soundOrInstance) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return;

    if (soundOrInstance >= WEB_SOUND_INSTANCE_ID_BASE) {
        WebSoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) ma_sound_stop(&inst->maSound);
    } else {
        repeat(WEB_MAX_SOUND_INSTANCES, i) {
            WebSoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) ma_sound_stop(&inst->maSound);
        }
    }
}

static void webResumeSound(AudioSystem* audio, int32_t soundOrInstance) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return;

    if (soundOrInstance >= WEB_SOUND_INSTANCE_ID_BASE) {
        WebSoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) ma_sound_start(&inst->maSound);
    } else {
        repeat(WEB_MAX_SOUND_INSTANCES, i) {
            WebSoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) ma_sound_start(&inst->maSound);
        }
    }
}

static void webPauseAll(AudioSystem* audio) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return;
    repeat(WEB_MAX_SOUND_INSTANCES, i) {
        WebSoundInstance* inst = &ma->instances[i];
        if (inst->active && ma_sound_is_playing(&inst->maSound)) ma_sound_stop(&inst->maSound);
    }
}

static void webResumeAll(AudioSystem* audio) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return;
    repeat(WEB_MAX_SOUND_INSTANCES, i) {
        WebSoundInstance* inst = &ma->instances[i];
        if (inst->active) ma_sound_start(&inst->maSound);
    }
}

static void webSetSoundGain(AudioSystem* audio, int32_t soundOrInstance, float gain, uint32_t timeMs) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return;

    if (soundOrInstance >= WEB_SOUND_INSTANCE_ID_BASE) {
        WebSoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            if (timeMs == 0) {
                inst->currentGain = gain;
                inst->targetGain = gain;
                inst->fadeTimeRemaining = 0.0f;
                ma_sound_set_volume(&inst->maSound, gain);
            } else {
                inst->startGain = inst->currentGain;
                inst->targetGain = gain;
                inst->fadeTotalTime = (float) timeMs / 1000.0f;
                inst->fadeTimeRemaining = inst->fadeTotalTime;
            }
        }
    } else {
        repeat(WEB_MAX_SOUND_INSTANCES, i) {
            WebSoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                if (timeMs == 0) {
                    inst->currentGain = gain;
                    inst->targetGain = gain;
                    inst->fadeTimeRemaining = 0.0f;
                    ma_sound_set_volume(&inst->maSound, gain);
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

static float webGetSoundGain(AudioSystem* audio, int32_t soundOrInstance) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return 0.0f;

    if (soundOrInstance >= WEB_SOUND_INSTANCE_ID_BASE) {
        WebSoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) return inst->currentGain;
    } else {
        repeat(WEB_MAX_SOUND_INSTANCES, i) {
            WebSoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) return inst->currentGain;
        }
    }
    return 0.0f;
}

static void webSetSoundPitch(AudioSystem* audio, int32_t soundOrInstance, float pitch) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return;

    if (soundOrInstance >= WEB_SOUND_INSTANCE_ID_BASE) {
        WebSoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) ma_sound_set_pitch(&inst->maSound, pitch);
    } else {
        repeat(WEB_MAX_SOUND_INSTANCES, i) {
            WebSoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) ma_sound_set_pitch(&inst->maSound, pitch);
        }
    }
}

static float webGetSoundPitch(AudioSystem* audio, int32_t soundOrInstance) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return 1.0f;

    if (soundOrInstance >= WEB_SOUND_INSTANCE_ID_BASE) {
        WebSoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) return ma_sound_get_pitch(&inst->maSound);
    } else {
        repeat(WEB_MAX_SOUND_INSTANCES, i) {
            WebSoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) return ma_sound_get_pitch(&inst->maSound);
        }
    }
    return 1.0f;
}

static float webGetTrackPosition(AudioSystem* audio, int32_t soundOrInstance) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return 0.0f;

    if (soundOrInstance >= WEB_SOUND_INSTANCE_ID_BASE) {
        WebSoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) {
            float cursor;
            if (ma_sound_get_cursor_in_seconds(&inst->maSound, &cursor) == MA_SUCCESS) return cursor;
        }
    } else {
        repeat(WEB_MAX_SOUND_INSTANCES, i) {
            WebSoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                float cursor;
                if (ma_sound_get_cursor_in_seconds(&inst->maSound, &cursor) == MA_SUCCESS) return cursor;
            }
        }
    }
    return 0.0f;
}

static void webSetTrackPosition(AudioSystem* audio, int32_t soundOrInstance, float positionSeconds) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return;

    ma_uint64 framePos = (ma_uint64) (positionSeconds * (float) ma->sampleRate);
    if (soundOrInstance >= WEB_SOUND_INSTANCE_ID_BASE) {
        WebSoundInstance* inst = findInstanceById(ma, soundOrInstance);
        if (inst != nullptr) ma_sound_seek_to_pcm_frame(&inst->maSound, framePos);
    } else {
        repeat(WEB_MAX_SOUND_INSTANCES, i) {
            WebSoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) ma_sound_seek_to_pcm_frame(&inst->maSound, framePos);
        }
    }
}

static float webGetSoundLength(AudioSystem* audio, int32_t soundOrInstance) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return 0.0f;

    WebSoundInstance* match = nullptr;
    if (soundOrInstance >= WEB_SOUND_INSTANCE_ID_BASE) {
        match = findInstanceById(ma, soundOrInstance);
    } else {
        repeat(WEB_MAX_SOUND_INSTANCES, i) {
            WebSoundInstance* inst = &ma->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                match = inst;
                break;
            }
        }
    }
    if (match == nullptr) return 0.0f;

    float seconds = 0.0f;
    if (ma_sound_get_length_in_seconds(&match->maSound, &seconds) != MA_SUCCESS) return 0.0f;
    return seconds;
}

static void webSetMasterGain(AudioSystem* audio, float gain) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return;
    ma_engine_set_volume(&ma->engine, gain);
}

static void webSetChannelCount(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t count) {}

static void webGroupLoad(AudioSystem* audio, int32_t groupIndex) {
    if (groupIndex > 0) {
        WebAudioSystem* ma = (WebAudioSystem*) audio;
        int sz = snprintf(nullptr, 0, "audiogroup%d.dat", groupIndex);
        char buf[sz + 1];
        snprintf(buf, sizeof(buf), "audiogroup%d.dat", groupIndex);
        DataWin* audioGroup = DataWin_parse(
            ma->fileSystem->vtable->resolvePath(ma->fileSystem, buf),
            (DataWinParserOptions) { .parseAudo = true }
        );
        arrput(audio->audioGroups, audioGroup);
    }
}

static bool webGroupIsLoaded(AudioSystem* audio, int32_t groupIndex) {
    return (arrlen(audio->audioGroups) > groupIndex);
}

static int32_t webCreateStream(AudioSystem* audio, const char* filename) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return -1;

    int32_t freeSlot = -1;
    repeat(WEB_MAX_AUDIO_STREAMS, i) {
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

    int32_t streamIndex = WEB_AUDIO_STREAM_INDEX_BASE + freeSlot;
    fprintf(stderr, "Audio: Created stream %d for '%s' -> '%s'\n", streamIndex, filename, resolved);
    return streamIndex;
}

static bool webDestroyStream(AudioSystem* audio, int32_t streamIndex) {
    WebAudioSystem* ma = (WebAudioSystem*) audio;
    if (!ma->engineReady) return false;

    int32_t slotIndex = streamIndex - WEB_AUDIO_STREAM_INDEX_BASE;
    if (0 > slotIndex || slotIndex >= WEB_MAX_AUDIO_STREAMS) {
        fprintf(stderr, "Audio: Invalid stream index %d for destroy\n", streamIndex);
        return false;
    }

    WebAudioStreamEntry* entry = &ma->streams[slotIndex];
    if (!entry->active) return false;

    repeat(WEB_MAX_SOUND_INSTANCES, i) {
        WebSoundInstance* inst = &ma->instances[i];
        if (inst->active && inst->soundIndex == streamIndex) {
            ma_sound_stop(&inst->maSound);
            ma_sound_uninit(&inst->maSound);
            if (inst->ownsDecoder) ma_decoder_uninit(&inst->decoder);
            inst->active = false;
        }
    }

    free(entry->filePath);
    entry->filePath = nullptr;
    entry->active = false;
    return true;
}

// ===[ Vtable ]===

static AudioSystemVtable webAudioSystemVtable = {
    .init = webInit,
    .destroy = webDestroy,
    .update = webUpdate,
    .playSound = webPlaySound,
    .stopSound = webStopSound,
    .stopAll = webStopAll,
    .isPlaying = webIsPlaying,
    .pauseSound = webPauseSound,
    .resumeSound = webResumeSound,
    .pauseAll = webPauseAll,
    .resumeAll = webResumeAll,
    .setSoundGain = webSetSoundGain,
    .getSoundGain = webGetSoundGain,
    .setSoundPitch = webSetSoundPitch,
    .getSoundPitch = webGetSoundPitch,
    .getTrackPosition = webGetTrackPosition,
    .setTrackPosition = webSetTrackPosition,
    .getSoundLength = webGetSoundLength,
    .setMasterGain = webSetMasterGain,
    .setChannelCount = webSetChannelCount,
    .groupLoad = webGroupLoad,
    .groupIsLoaded = webGroupIsLoaded,
    .createStream = webCreateStream,
    .destroyStream = webDestroyStream,
};

// ===[ Lifecycle ]===

WebAudioSystem* WebAudioSystem_create(int32_t sampleRate) {
    WebAudioSystem* ma = safeCalloc(1, sizeof(WebAudioSystem));
    ma->base.vtable = &webAudioSystemVtable;
    ma->sampleRate = sampleRate > 0 ? sampleRate : 48000;
    return ma;
}

void WebAudioSystem_pullFrames(WebAudioSystem* audio, float* out, int32_t frameCount) {
    if (audio == nullptr || !audio->engineReady || frameCount <= 0) {
        if (out != nullptr && frameCount > 0) memset(out, 0, (size_t) frameCount * 2 * sizeof(float));
        return;
    }
    ma_uint64 framesRead = 0;
    ma_engine_read_pcm_frames(&audio->engine, out, (ma_uint64) frameCount, &framesRead);
    // miniaudio zero-fills any unread tail when the source runs dry, so we don't need to do it ourselves.
}
