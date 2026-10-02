    AudioSystem* audioSystem = nullptr;
#ifdef USE_FMOD
    audioSystem = (AudioSystem*) FmodAudioSystem_create(dataWin);
#elif defined(USE_MINIAUDIO)
    audioSystem = (AudioSystem*) MaAudioSystem_create(dataWin);
#else
    audioSystem = (AudioSystem*) NoopAudioSystem_create();
#endif
    if (audioSystem == nullptr) {
#ifdef USE_FMOD
        logWarn("FmodAudioSystem_create returned NULL; falling back to silent audio");
#elif defined(USE_MINIAUDIO)
        logWarn("MaAudioSystem_create returned NULL; falling back to silent audio");
#else
        logWarn("Audio system creation returned NULL; falling back to silent audio");
#endif
        audioSystem = (AudioSystem*) NoopAudioSystem_create();
    }
