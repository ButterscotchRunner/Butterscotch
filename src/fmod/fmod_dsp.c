#include "fmod_dsp.h"
#include "math_compat.h"
#include <stdlib.h>

enum { ECHO = 6 };

struct FmodDsp {
    unsigned type, rate, channels;
    bool bypass;
    struct {
        float* samples;
        unsigned capacity, position;
        double delay;
        float feedback, dry, wet;
    } echo;
};

static float decibelGain(float value) {
    return value <= -80 ? 0 : powf(10, value / 20);
}

bool FmodDsp_setParameter(FmodDsp* effect, unsigned index, const EffectParameter* parameter) {
    if (!effect || !parameter || parameter->kind != FM_EFFECT_FLOAT || !isfinite(parameter->value.real)) return false;
    float value = parameter->value.real;
    if (effect->type != ECHO) return false;
    switch (index) {
        case 0:
            effect->echo.delay = fmax(1, fmin(value, 5000)) * effect->rate / 1000;
            break;
        case 1:
            effect->echo.feedback = fmaxf(0, fminf(value, 100)) / 100;
            break;
        case 2:
            effect->echo.dry = decibelGain(fmaxf(-80, fminf(value, 10)));
            break;
        case 3:
            effect->echo.wet = decibelGain(fmaxf(-80, fminf(value, 10)));
            break;
        default: return false;
    }
    return true;
}

FmodDsp* FmodDsp_create(const EffectDefinition* definition, unsigned rate, unsigned channels) {
    if (!definition || definition->type != ECHO || rate == 0 || rate > 192000 || channels == 0 || channels > 8) return nullptr;
    if (arrlen(definition->parameters) > 4) return nullptr;
    FmodDsp* effect = (FmodDsp*) calloc(1, sizeof(*effect));
    if (!effect) return nullptr;
    effect->type = definition->type;
    effect->rate = rate;
    effect->channels = channels;
    effect->bypass = definition->bypass;
    effect->echo.capacity = rate * 5 + 1;
    effect->echo.samples = (float*) calloc((size_t) effect->echo.capacity * channels, sizeof(float));
    if (!effect->echo.samples) {
        free(effect);
        return nullptr;
    }
    effect->echo.delay = rate / 2.0;
    effect->echo.feedback = 0.5f;
    effect->echo.dry = effect->echo.wet = 1;
    for (ptrdiff_t i = 0; i < arrlen(definition->parameters); i++) {
        if (!FmodDsp_setParameter(effect, (unsigned) i, &definition->parameters[i])) {
            FmodDsp_destroy(effect);
            return nullptr;
        }
    }
    return effect;
}

void FmodDsp_destroy(FmodDsp* effect) {
    if (!effect) return;
    free(effect->echo.samples);
    free(effect);
}

void FmodDsp_process(FmodDsp* effect, float* pcm, unsigned frames) {
    if (!effect || !pcm || effect->bypass) return;
    float* buffer = effect->echo.samples;
    for (unsigned frame = 0; frame < frames; frame++) {
        double position = effect->echo.position - effect->echo.delay;
        if (position < 0) position += effect->echo.capacity;
        unsigned first = (unsigned) position;
        unsigned second = (first + 1) % effect->echo.capacity;
        float weight = (float) (position - first);
        for (unsigned channel = 0; channel < effect->channels; channel++) {
            float left = buffer[(size_t) first * effect->channels + channel];
            float right = buffer[(size_t) second * effect->channels + channel];
            float delayed = left + (right - left) * weight;
            float input = pcm[(size_t) frame * effect->channels + channel];
            buffer[(size_t) effect->echo.position * effect->channels + channel] = input + delayed * effect->echo.feedback;
            pcm[(size_t) frame * effect->channels + channel] = input * effect->echo.dry + delayed * effect->echo.wet;
        }
        effect->echo.position = (effect->echo.position + 1) % effect->echo.capacity;
    }
}
