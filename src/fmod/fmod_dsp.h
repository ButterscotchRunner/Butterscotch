#ifndef BS_FMOD_DSP_H
#define BS_FMOD_DSP_H

#include "fmod_internal.h"

typedef struct FmodDsp FmodDsp;

// effect state belongs to one bus, not to each voice routed through it.
// unsupported effect types return null until their processors are implemented.
FmodDsp* FmodDsp_create(const EffectDefinition* definition, unsigned rate, unsigned channels);
void FmodDsp_destroy(FmodDsp* effect);
bool FmodDsp_setParameter(FmodDsp* effect, unsigned index, const EffectParameter* parameter);
void FmodDsp_process(FmodDsp* effect, float* pcm, unsigned frames);

#endif
