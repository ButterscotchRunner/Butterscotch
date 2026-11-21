#include "audio.h"
#include <SDL2/SDL.h>

static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len) {
    ma_device* device = userdata;
    ma_uint32 frames = len / (sizeof(float) * device->playback.internalChannels);
    ma_device_handle_backend_data_callback(device, stream, NULL, frames);
}

static int SDLCALL audioLifecycle(void* userdata, SDL_Event* event) {
    ma_device* device = userdata;
    switch (event->type) {
        case SDL_APP_WILLENTERBACKGROUND:
            ma_device_stop(device);
            break;
        case SDL_APP_DIDENTERFOREGROUND:
            ma_device_start(device);
            break;
    }
    return 1;
}

static ma_result deviceInit(ma_device* device, const ma_device_config* config, ma_device_descriptor* playback, ma_device_descriptor* capture) {
    (void)capture;
    if (config->deviceType != ma_device_type_playback || playback->pDeviceID) return MA_INVALID_DEVICE_CONFIG;

    SDL_AudioSpec wanted = {0}, obtained;
    wanted.freq = playback->sampleRate ? playback->sampleRate : 48000;
    wanted.format = AUDIO_F32SYS;
    wanted.channels = 2;
    wanted.samples = 512;
    wanted.callback = audioCallback;
    wanted.userdata = device;
    SDL_AudioDeviceID id = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!id) {
        ma_log_postf(ma_context_get_log(device->pContext), MA_LOG_LEVEL_ERROR, "SDL audio: %s\n", SDL_GetError());
        return MA_FAILED_TO_OPEN_BACKEND_DEVICE;
    }
    device->playback.id.custom.i = id;
    playback->format = ma_format_f32;
    playback->channels = obtained.channels;
    playback->sampleRate = obtained.freq;
    playback->periodSizeInFrames = obtained.samples;
    playback->periodCount = 1;
    ma_channel_map_init_standard(ma_standard_channel_map_default, playback->channelMap, MA_MAX_CHANNELS, obtained.channels);
    // WinRT suspension can prevent queued events from reaching SDL_PollEvent.
    SDL_AddEventWatch(audioLifecycle, device);
    return MA_SUCCESS;
}

static ma_result deviceUninit(ma_device* device) {
    SDL_DelEventWatch(audioLifecycle, device);
    SDL_CloseAudioDevice(device->playback.id.custom.i);
    return MA_SUCCESS;
}

static ma_result deviceStart(ma_device* device) {
    SDL_PauseAudioDevice(device->playback.id.custom.i, 0);
    return MA_SUCCESS;
}

static ma_result deviceStop(ma_device* device) {
    SDL_PauseAudioDevice(device->playback.id.custom.i, 1);
    return MA_SUCCESS;
}

static ma_result deviceInfo(ma_device* device, ma_device_type type, ma_device_info* info) {
    if (type != ma_device_type_playback) return MA_INVALID_DEVICE_CONFIG;
    info->id = device->playback.id;
    info->isDefault = MA_TRUE;
    SDL_snprintf(info->name, sizeof(info->name), "SDL/%s", SDL_GetCurrentAudioDriver());
    return MA_SUCCESS;
}

static ma_result contextInit(ma_context* context, const ma_context_config* config, ma_backend_callbacks* callbacks) {
    (void)config;
    (void)callbacks;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        ma_log_postf(ma_context_get_log(context), MA_LOG_LEVEL_ERROR, "SDL audio: %s\n", SDL_GetError());
        return MA_FAILED_TO_INIT_BACKEND;
    }
    return MA_SUCCESS;
}

static ma_result contextUninit(ma_context* context) {
    (void)context;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return MA_SUCCESS;
}

ma_result uwpAudioDeviceInit(ma_device* device, const ma_device_config* config) {
    ma_context_config context = ma_context_config_init();
    context.custom.onContextInit = contextInit;
    context.custom.onContextUninit = contextUninit;
    context.custom.onDeviceInit = deviceInit;
    context.custom.onDeviceUninit = deviceUninit;
    context.custom.onDeviceStart = deviceStart;
    context.custom.onDeviceStop = deviceStop;
    context.custom.onDeviceGetInfo = deviceInfo;
    ma_backend backend = ma_backend_custom;
    return ma_device_init_ex(&backend, 1, &context, config, device);
}
