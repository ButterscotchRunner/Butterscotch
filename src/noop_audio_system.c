#include "noop_audio_system.h"
#include "data_win.h"
#include "stb_ds.h"

#include <stdlib.h>

static void noopInit(AudioSystem* audio, DataWin* dataWin, MAYBE_UNUSED FileSystem* fileSystem) {
    arrput(audio->audioGroups, dataWin);
}

static void noopDestroy(AudioSystem* audio) {
    free(audio);
}

static void noopUpdate(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED float deltaTime) {}

static int32_t noopPlaySound(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundIndex, MAYBE_UNUSED int32_t priority, MAYBE_UNUSED bool loop) {
    return -1;
}

static void noopStopSound(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {}

static void noopStopAll(MAYBE_UNUSED AudioSystem* audio) {}

static bool noopIsPlaying(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {
    return false;
}

static void noopPauseSound(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {}

static void noopResumeSound(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {}

static void noopPauseAll(MAYBE_UNUSED AudioSystem* audio) {}

static void noopResumeAll(MAYBE_UNUSED AudioSystem* audio) {}

static void noopSetSoundGain(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance, MAYBE_UNUSED float gain, MAYBE_UNUSED uint32_t timeMs) {}

static float noopGetSoundGain(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {
    return 1.0f;
}

static void noopSetSoundPitch(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance, MAYBE_UNUSED float pitch) {}

static float noopGetSoundPitch(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {
    return 1.0f;
}

static float noopGetTrackPosition(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {
    return 0.0f;
}

static void noopSetTrackPosition(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance, MAYBE_UNUSED float positionSeconds) {}

// Return 1.0s (not 0) so GML code that divides by audio length doesn't hit division-by-zero.
static float noopGetSoundLength(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t soundOrInstance) {
    return 1.0f;
}

static void noopSetMasterGain(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED float gain) {}

static void noopSetMasterGainForListener(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED float gain, MAYBE_UNUSED int32_t listenerId) {}

static void noopSetChannelCount(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t count) {}

static void noopGroupLoad(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t groupIndex) {}

static bool noopGroupIsLoaded(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t groupIndex) {
    return true;
}

static int32_t noopCreateStream(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED const char* filename) {
    return -1;
}

static bool noopDestroyStream(MAYBE_UNUSED AudioSystem* audio, MAYBE_UNUSED int32_t streamIndex) {
    return false;
}

static AudioSystemVtable noopVtable;

NoopAudioSystem* NoopAudioSystem_create(void) {
    NoopAudioSystem* audio = (NoopAudioSystem *)safeCalloc(1, sizeof(NoopAudioSystem));
    noopVtable.init = noopInit,
    noopVtable.destroy = noopDestroy,
    noopVtable.update = noopUpdate,
    noopVtable.playSound = noopPlaySound,
    noopVtable.stopSound = noopStopSound,
    noopVtable.stopAll = noopStopAll,
    noopVtable.isPlaying = noopIsPlaying,
    noopVtable.pauseSound = noopPauseSound,
    noopVtable.resumeSound = noopResumeSound,
    noopVtable.pauseAll = noopPauseAll,
    noopVtable.resumeAll = noopResumeAll,
    noopVtable.suspend = noopPauseAll,
    noopVtable.resume = noopResumeAll,
    noopVtable.setSoundGain = noopSetSoundGain,
    noopVtable.getSoundGain = noopGetSoundGain,
    noopVtable.setSoundPitch = noopSetSoundPitch,
    noopVtable.getSoundPitch = noopGetSoundPitch,
    noopVtable.getTrackPosition = noopGetTrackPosition,
    noopVtable.setTrackPosition = noopSetTrackPosition,
    noopVtable.getSoundLength = noopGetSoundLength,
    noopVtable.setMasterGain = noopSetMasterGain,
    noopVtable.setMasterGainForListener = noopSetMasterGainForListener,
    noopVtable.setChannelCount = noopSetChannelCount,
    noopVtable.groupLoad = noopGroupLoad,
    noopVtable.groupIsLoaded = noopGroupIsLoaded,
    noopVtable.createStream = noopCreateStream,
    noopVtable.destroyStream = noopDestroyStream,
    audio->base.vtable = &noopVtable;
    return audio;
}
