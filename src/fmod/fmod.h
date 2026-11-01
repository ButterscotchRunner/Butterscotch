#ifndef BS_FMOD_H
#define BS_FMOD_H

#include "audio_system.h"

typedef struct FmodSystem FmodSystem;

#ifdef ENABLE_FMOD
// the system owns banks and samples; the audio backend owns the voices.
// stop the voices before freeing any sample memory they're using.
FmodSystem* Fmod_create(AudioSystem* audio, FileSystem* fs, int channels);
void Fmod_destroy(FmodSystem* system);
void Fmod_audioDestroy(AudioSystem* audio);
void Fmod_step(FmodSystem* system, double seconds);
bool Fmod_loadBank(FmodSystem* system, const char* path, bool nonblocking);
bool Fmod_loadSamples(FmodSystem* system, const char* path);
int Fmod_createInstance(FmodSystem* system, const char* path);
bool Fmod_play(FmodSystem* system, int handle);
bool Fmod_stop(FmodSystem* system, int handle, bool immediate);
bool Fmod_release(FmodSystem* system, int handle);
bool Fmod_isPlaying(FmodSystem* system, int handle);
bool Fmod_pause(FmodSystem* system, int handle, bool pause);
bool Fmod_getPaused(FmodSystem* system, int handle);
void Fmod_pauseAll(FmodSystem* system, bool pause);
bool Fmod_setPosition(FmodSystem* system, int handle, double milliseconds);
double Fmod_getPosition(FmodSystem* system, int handle);
double Fmod_getLength(FmodSystem* system, const char* path);
bool Fmod_eventExists(FmodSystem* system, const char* path);
bool Fmod_setParameter(FmodSystem* system, int handle, const char* name, float value, bool immediate);
float Fmod_getParameter(FmodSystem* system, int handle, const char* name);
bool Fmod_setSpatial(FmodSystem* system, int handle, float x, float y);
bool Fmod_setListener(FmodSystem* system, int listener, float x, float y);
bool Fmod_setListeners(FmodSystem* system, int count);
bool Fmod_oneShot(FmodSystem* system, const char* path, bool spatial, float x, float y);

#else

static inline void Fmod_audioDestroy(MAYBE_UNUSED AudioSystem* audio) {}
static inline void Fmod_step(MAYBE_UNUSED FmodSystem* system, MAYBE_UNUSED double seconds) {}

#endif

#endif
