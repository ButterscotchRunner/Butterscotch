#include "fmod_internal.h"
#include "log.h"
#include "math_compat.h"
#include <stdlib.h>

static unsigned nextRandom(FmodSystem* system);
static float exponentialWeight(double weight, float curvature) {
    float x = (float) fmax(0, fmin(weight, 1));
    if (fabsf(curvature) < 0.00001f) return x;
    float exponent = fmaxf(-1, fminf(curvature, 1)) * 6.95219755f;
    return expm1f(exponent * x) / expm1f(exponent);
}

static float interpolateCurve(float left, float right, double position, float curvature, uint32_t mode) {
    float weight = (float) fmax(0, fmin(position, 1));
    if (mode == FM_CURVE_STEPPED) return left;
    if (mode == FM_CURVE_SQUARED) {
        // interpolate squared levels in log space without overflowing when
        // a controller uses large values, such as filter frequencies.
        if (weight <= 0) return left;
        if (weight >= 1) return right;
        double a = 2.0 * left, b = 2.0 * right, maximum = fmax(a, b);
        return (float) ((maximum + log2((1 - weight) * exp2(a - maximum) + weight * exp2(b - maximum))) / 2);
    }
    if (mode == FM_CURVE_EXPONENTIAL) {
        weight = exponentialWeight(weight, curvature);
    } else if (mode == FM_CURVE_SYMMETRIC && curvature > 0) {
        float power = 1 + 2 * curvature;
        weight = weight <= 0.5f ? powf(2 * weight, power) / 2 : 1 - powf(2 * (1 - weight), power) / 2;
    } else if (mode == FM_CURVE_SYMMETRIC && curvature < 0) {
        float centered = 2 * weight - 1;
        float cubic = (centered * centered * centered + 1) / 2;
        weight += (weight - cubic) * curvature;
    } else if (mode > FM_CURVE_SYMMETRIC && curvature != 0) {
        weight = 0;
    }
    return left + (right - left) * weight;
}

// ===[ events and parameters ]===

static EventInstance* findInstance(FmodSystem* system, int handle) {
    if (!system || handle <= 0) return nullptr;
    for (ptrdiff_t i = 0; i < arrlen(system->instances); i++) {
        EventInstance* instance = system->instances[i];
        if (instance->handle == handle && !instance->released) return instance;
    }
    return nullptr;
}

static Record* findEvent(FmodSystem* system, const char* path) {
    if (!system || !path) return nullptr;
    ptrdiff_t index = shgeti(system->index.names, path);
    return index >= 0 ? lookup(system, "EVTB", system->index.names[index].value.b) : nullptr;
}

bool Fmod_eventExists(FmodSystem* system, const char* path) {
    return findEvent(system, path) != nullptr;
}

static float readParameter(FmodSystem* system, EventInstance* instance, int index, bool target) {
    if (index < 0 || index >= arrlen(system->index.parameters)) return 0;
    if (!system->index.parameters[index].global && instance) {
        for (ptrdiff_t i = 0; i < arrlen(instance->values); i++) {
            if (instance->values[i].parameter == index) return target ? instance->values[i].value : instance->values[i].current;
        }
    }
    Parameter* parameter = &system->index.parameters[index];
    return parameter->global ? (target ? parameter->value : parameter->current) : parameter->initial;
}

static float parameterValue(FmodSystem* system, EventInstance* instance, int index) {
    return readParameter(system, instance, index, false);
}

static float advanceParameter(float current, float target, const Parameter* parameter, double seconds) {
    float speed = target >= current ? parameter->seekUp : parameter->seekDown;
    if (speed == 0) return target;
    double distance = seconds * speed;
    return target >= current ? (float) fmin(target, current + distance) : (float) fmax(target, current - distance);
}

static int findParameter(FmodSystem* system, EventInstance* instance, const char* name) {
    for (ptrdiff_t i = 0; i < arrlen(system->index.parameters); i++) {
        Parameter* parameter = &system->index.parameters[i];
        if (strcmp(parameter->name, name)) continue;
        if (parameter->global) return (int) i;
        if (!instance || instance->event->size <= FM_EVENT_PARAMETERS) continue;
        FmReader reader = {instance->event->data, instance->event->size, FM_EVENT_PARAMETERS, false};
        uint32_t count, stride;
        const uint8_t* references = fmlist(&reader, &count, &stride);
        if (reader.failed || stride != FM_GUID_SIZE) continue;
        for (uint32_t j = 0; j < count; j++) {
            Record* mapping = lookup(system, "PMLB", references + j * FM_GUID_SIZE);
            if (mapping && equalGuid(mapping->data + FM_GUID_SIZE, parameter->id.b)) return (int) i;
        }
    }
    return -1;
}

bool Fmod_setParameter(FmodSystem* system, int handle, const char* name, float value, bool immediate) {
    EventInstance* instance = handle ? findInstance(system, handle) : nullptr;
    if (!system || !name || !isfinite(value) || (handle && !instance)) return false;
    int index = findParameter(system, instance, name);
    if (index < 0) return false;
    Parameter* parameter = &system->index.parameters[index];
    if (value < parameter->min) value = parameter->min;
    if (value > parameter->max) value = parameter->max;
    if (parameter->global) {
        parameter->value = value;
        if (immediate) parameter->current = value;
        else parameter->current = advanceParameter(parameter->current, value, parameter, 0);
    } else {
        ptrdiff_t i;
        for (i = 0; i < arrlen(instance->values); i++) {
            if (instance->values[i].parameter == index) break;
        }
        if (i == arrlen(instance->values)) {
            LocalValue local = {index, value, immediate ? value : advanceParameter(parameter->initial, value, parameter, 0)};
            arrput(instance->values, local);
        } else {
            instance->values[i].value = value;
            if (immediate) instance->values[i].current = value;
            else instance->values[i].current = advanceParameter(instance->values[i].current, value, parameter, 0);
        }
    }
    Fmod_step(system, 0);
    return true;
}

float Fmod_getParameter(FmodSystem* system, int handle, const char* name) {
    EventInstance* instance = handle ? findInstance(system, handle) : nullptr;
    if (!system || !name || (handle && !instance)) return 0;
    return readParameter(system, instance, findParameter(system, instance, name), true);
}

// ===[ bus volume ]===

static float evaluateCurve(FmodSystem* system, EventInstance* instance, Record* curve) {
    if (!curve || curve->size < FM_CURVE_POINTS + 2) return 0;
    double input = instance->position;
    bool parameterInput = false;
    for (ptrdiff_t i = 0; i < arrlen(system->index.inputs); i++) {
        if (equalGuid(system->index.inputs[i].curve.b, curve->data)) {
            input = parameterValue(system, instance, findParameterIndex(&system->index, system->index.inputs[i].parameter.b));
            parameterInput = true;
            break;
        }
    }
    FmReader reader = {curve->data, curve->size, FM_CURVE_POINTS, false};
    uint32_t count, stride;
    const uint8_t* points = fmlist(&reader, &count, &stride);
    if (reader.failed || !count || stride < 8) return 0;
    double previousX = parameterInput ? fmfloat(points) : fm32(points) / FM_TICKS;
    float previousY = fmfloat(points + 4);
    if (input < previousX) return previousY;
    for (uint32_t i = 1; i < count; i++) {
        double x = parameterInput ? fmfloat(points + i * stride) : fm32(points + i * stride) / FM_TICKS;
        float y = fmfloat(points + i * stride + 4);
        if (input < x) {
            const uint8_t* previous = points + (i - 1) * stride;
            float curvature = stride >= 12 ? fmfloat(previous + 8) : 0;
            uint32_t mode = stride >= 16 ? fm32(previous + 12) : 0;
            return interpolateCurve(previousY, y, x > previousX ? (input - previousX) / (x - previousX) : 1, curvature, mode);
        }
        previousX = x;
        previousY = y;
    }
    return previousY;
}

// the volume property id stored in the bank's property map.
static const uint8_t volumeProperty[FM_GUID_SIZE] = {0x28, 0xa1, 0x5b, 0xd6, 0xf5, 0xa6, 0xd9, 0x41, 0xb6, 0xa1, 0xa7, 0xc4, 0x02, 0xf3, 0x29, 0xac};
static const uint8_t pitchProperty[FM_GUID_SIZE] = {0x07, 0x21, 0x8c, 0xc1, 0xb9, 0x7b, 0xdc, 0x4c, 0x84, 0x53, 0x19, 0xb8, 0x81, 0xdb, 0x2e, 0x14};

static float mapProperty(FmodSystem* system, const uint8_t* id, float value, bool inverse) {
    Record* map = lookup(system, "MAP ", id);
    if (!map) return value;
    FmReader reader = {map->data, map->size, FM_GUID_SIZE, false};
    uint32_t count, stride;
    const uint8_t* points = fmlist(&reader, &count, &stride);
    if (reader.failed || !count || stride != 8) return value;
    unsigned input = inverse ? 4 : 0, output = inverse ? 0 : 4;
    float previousX = fmfloat(points + input), previousY = fmfloat(points + output);
    if (value <= previousX) return previousY;
    for (uint32_t i = 1; i < count; i++) {
        float x = fmfloat(points + i * stride + input);
        float y = fmfloat(points + i * stride + output);
        if (value <= x) {
            float weight = x > previousX ? (value - previousX) / (x - previousX) : 1;
            return previousY + (y - previousY) * weight;
        }
        previousX = x;
        previousY = y;
    }
    return previousY;
}

static float randomModulation(FmodSystem* system, Clip* clip, Record* modulator) {
    if (!modulator || modulator->size < FM_RANDOM_MODULATOR_SIZE || fm32(modulator->data + FM_MODULATOR_TYPE) != 1 || fm32(modulator->data + FM_MODULATOR_MODE) != 0) return 0;
    float amount = fmfloat(modulator->data + FM_MODULATOR_AMOUNT);
    if (!isfinite(amount) || amount < 0) return 0;
    for (ptrdiff_t i = 0; i < arrlen(clip->modulation); i++) {
        if (clip->modulation[i].modulator == modulator) return clip->modulation[i].value;
    }
    ModulationValue value = {modulator, (float) ((nextRandom(system) / (double) UINT32_MAX * 2 - 1) * amount / 100)};
    arrput(clip->modulation, value);
    return value.value;
}

static float envelopeValue(FmodSystem* system, Record* envelope, double age) {
    const uint8_t* data = envelope->data;
    double attack = fmfloat(data + FM_ENVELOPE_ATTACK) / 1000.0;
    double hold = fmfloat(data + FM_ENVELOPE_HOLD) / 1000.0;
    double decay = fmfloat(data + FM_ENVELOPE_DECAY) / 1000.0;
    float initial = mapProperty(system, volumeProperty, fmfloat(data + FM_ENVELOPE_INITIAL), true);
    float peak = mapProperty(system, volumeProperty, fmfloat(data + FM_ENVELOPE_PEAK), true);
    float sustain = mapProperty(system, volumeProperty, fmfloat(data + FM_ENVELOPE_SUSTAIN), true);
    if (attack > 0 && age < attack)
        return initial + (peak - initial) * exponentialWeight(age / attack, fmfloat(data + FM_ENVELOPE_ATTACK_CURVE));
    age -= fmax(attack, 0);
    if (age < hold) return peak;
    age -= fmax(hold, 0);
    if (decay > 0 && age < decay)
        return peak + (sustain - peak) * exponentialWeight(age / decay, fmfloat(data + FM_ENVELOPE_DECAY_CURVE));
    return sustain;
}

static float evaluateEnvelope(FmodSystem* system, EventInstance* instance, Clip* clip, Record* owner, Record* envelope, double* releaseDuration) {
    if (!envelope || envelope->size < FM_ENVELOPE_SIZE || fm32(envelope->data + FM_MODULATOR_TYPE) != 0) return 0;
    for (size_t offset = FM_ENVELOPE_INITIAL; offset < FM_ENVELOPE_SIZE; offset += 4) {
        if (!isfinite(fmfloat(envelope->data + offset))) return 0;
    }
    double release = fmax(0, fmfloat(envelope->data + FM_ENVELOPE_RELEASE) / 1000.0);
    if (releaseDuration && release > *releaseDuration) *releaseDuration = release;
    double activated = clip->activated;
    if (!tagIs(owner, "INST")) {
        ptrdiff_t i;
        for (i = 0; i < arrlen(instance->clocks); i++) {
            if (instance->clocks[i].owner == owner) break;
        }
        if (i == arrlen(instance->clocks)) {
            BusClock clock = {owner, tagIs(owner, "MBSB") && equalGuid(owner->data, instance->event->data + FM_EVENT_MASTER_BUS) ? 0 : instance->elapsed};
            arrput(instance->clocks, clock);
        }
        activated = instance->clocks[i].activated;
    }
    double age = fmax(0, (instance->stopping ? instance->stopElapsed : instance->elapsed) - activated);
    float value = envelopeValue(system, envelope, age);
    if (instance->stopping) {
        double time = instance->elapsed - instance->stopElapsed;
        float final = mapProperty(system, volumeProperty, fmfloat(envelope->data + FM_ENVELOPE_FINAL), true);
        value += (final - value) * exponentialWeight(release > 0 ? time / release : 1, fmfloat(envelope->data + FM_ENVELOPE_RELEASE_CURVE));
    }
    return mapProperty(system, volumeProperty, value, false);
}

static float ownerBase(Record* owner, bool pitch) {
    if (tagIs(owner, "INST") && owner->size >= FM_INSTRUMENT_PITCH + 4)
        return fmfloat(owner->data + (pitch ? FM_INSTRUMENT_PITCH : FM_INSTRUMENT_VOLUME));
    Record* bus = findSiblingRecord(owner, "BUS ");
    if (!bus || bus->size < 8) return 0;
    FmReader reader = {bus->data, bus->size, 8, false};
    for (unsigned i = 0; i < 2; i++) {
        FmList list;
        if (!FmList_begin(&list, &reader)) return 0;
        while (list.remaining && !reader.failed) {
            uint32_t bytes;
            FmList_next(&list, &bytes);
        }
    }
    uint32_t bytes = fmcount(&reader);
    const uint8_t* values = fmread(&reader, bytes);
    return values && bytes >= (pitch ? 8u : 4u) ? fmfloat(values + (pitch ? 4 : 0)) : 0;
}

static float ownerValue(FmodSystem* system, EventInstance* instance, Clip* clip, Record* owner, bool pitch, double* releaseDuration) {
    if (!owner) return 0;
    const uint8_t* propertyId = pitch ? pitchProperty : volumeProperty;
    float value = ownerBase(owner, pitch);
    for (ptrdiff_t i = owner->parent + 1; i < arrlen(owner->bank->records); i++) {
        Record* property = &owner->bank->records[i];
        if (property->parent < owner->parent) break;
        if (property->parent < 0 || !tagIs(property, "PROP")) continue;
        if (owner->bank->records[property->parent].parent != owner->parent) continue;
        if (property->size < FM_PROPERTY_CONTROLLERS + 2) continue;
        if (!equalGuid(property->data + FM_PROPERTY_ID, propertyId)) continue;
        FmReader reader = {property->data, property->size, FM_PROPERTY_CONTROLLERS, false};
        for (unsigned group = 0; group < 2; group++) {
            uint32_t count, stride;
            const uint8_t* references = fmlist(&reader, &count, &stride);
            if (reader.failed || (count && stride != FM_GUID_SIZE)) break;
            for (uint32_t j = 0; j < count; j++) {
                const uint8_t* id = references + j * stride;
                if (group == 0) {
                    float contribution = evaluateCurve(system, instance, lookup(system, "CURV", id));
                    if (!pitch && fm16(property->data + FM_PROPERTY_MAPPING) == 1)
                        contribution = mapProperty(system, volumeProperty, contribution, false);
                    value += contribution;
                } else if (pitch) {
                    value += randomModulation(system, clip, lookup(system, "MODB", id)) * 24;
                } else {
                    value += evaluateEnvelope(system, instance, clip, owner, lookup(system, "MODB", id), releaseDuration);
                }
            }
        }
    }
    return value;
}

static float ownerGain(FmodSystem* system, EventInstance* instance, Clip* clip, Record* owner, double* releaseDuration) {
    float decibels = ownerValue(system, instance, clip, owner, false, releaseDuration);
    if (!isfinite(decibels)) return 0;
    return decibels <= -80 ? 0 : powf(10, fminf(decibels, 24) / 20);
}

static float ownerPitch(FmodSystem* system, EventInstance* instance, Clip* clip, Record* owner) {
    return ownerValue(system, instance, clip, owner, true, nullptr);
}

static Record* findBus(FmodSystem* system, const uint8_t* id) {
    Record* bus = lookup(system, "GBSB", id);
    if (!bus) bus = lookup(system, "IBSB", id);
    if (!bus) bus = lookup(system, "MBSB", id);
    return bus;
}

static Record* parentBus(FmodSystem* system, EventInstance** context, Record* bus, Record** instrument, double* activated) {
    EventInstance* instance = *context;
    *instrument = nullptr;
    if (tagIs(bus, "MBSB") && equalGuid(bus->data, instance->event->data + FM_EVENT_MASTER_BUS))
        return lookup(system, "IBSB", instance->event->data + FM_EVENT_INPUT_BUS);
    if (tagIs(bus, "IBSB") && instance->parent && equalGuid(bus->data, instance->event->data + FM_EVENT_INPUT_BUS)) {
        *instrument = instance->parentInstrument;
        *activated = instance->parentActivated;
        *context = instance->parent;
        return *instrument && (*instrument)->size >= FM_INSTRUMENT_BUS + FM_GUID_SIZE ? findBus(system, (*instrument)->data + FM_INSTRUMENT_BUS) : nullptr;
    }
    return findBus(system, bus->data + FM_BUS_PARENT);
}

static float clipGain(FmodSystem* system, EventInstance* instance, Clip* clip, double* releaseDuration) {
    float gain = ownerGain(system, instance, clip, clip->instrument, releaseDuration);
    for (unsigned i = 0; i < clip->controllerCount; i++) gain *= ownerGain(system, instance, clip, clip->controllers[i], releaseDuration);
    if (!clip->instrument || clip->instrument->size < FM_INSTRUMENT_BUS + FM_GUID_SIZE) return gain;
    Record* bus = findBus(system, clip->instrument->data + FM_INSTRUMENT_BUS);
    unsigned depth = 0;
    while (bus && depth++ < 32) {
        gain *= ownerGain(system, instance, clip, bus, releaseDuration);
        Record* instrument;
        double activated = 0;
        bus = parentBus(system, &instance, bus, &instrument, &activated);
        if (instrument) {
            Clip routed = *clip;
            routed.activated = activated;
            gain *= ownerGain(system, instance, &routed, instrument, releaseDuration);
        }
    }
    return isfinite(gain) ? gain : 0;
}

static float clipPitch(FmodSystem* system, EventInstance* instance, Clip* clip) {
    float semitones = ownerPitch(system, instance, clip, clip->instrument);
    for (unsigned i = 0; i < clip->controllerCount; i++) semitones += ownerPitch(system, instance, clip, clip->controllers[i]);
    Record* bus = clip->instrument && clip->instrument->size >= FM_INSTRUMENT_BUS + FM_GUID_SIZE ? findBus(system, clip->instrument->data + FM_INSTRUMENT_BUS) : nullptr;
    unsigned depth = 0;
    while (bus && depth++ < 32) {
        semitones += ownerPitch(system, instance, clip, bus);
        Record* instrument;
        double activated = 0;
        bus = parentBus(system, &instance, bus, &instrument, &activated);
        if (instrument) semitones += ownerPitch(system, instance, clip, instrument);
    }
    return isfinite(semitones) ? powf(2, fmaxf(-96, fminf(semitones, 96)) / 12) : 1;
}

// ===[ instrument scheduling ]===

static unsigned nextRandom(FmodSystem* system) {
    system->random ^= system->random << 13;
    system->random ^= system->random >> 17;
    system->random ^= system->random << 5;
    return system->random;
}

static bool scheduleInstrument(FmodSystem* system, EventInstance* instance, const uint8_t* id, double start, double end, unsigned depth);
static bool timelineLength(FmodSystem* system, Record* event, double base, unsigned depth, double* length);

static bool scheduleTimeline(FmodSystem* system, EventInstance* instance, Record* event, double base, unsigned depth) {
    if (!event || depth > 16) return false;
    Record* timeline = lookup(system, "TLNB", event->data + FM_EVENT_TIMELINE);
    if (!timeline) return false;
    FmReader reader = {timeline->data, timeline->size, FM_TIMELINE_LISTS, false};
    // nested events come first, then the regular instruments.
    for (unsigned group = 0; group < 2; group++) {
        uint32_t count, stride;
        const uint8_t* list = fmlist(&reader, &count, &stride);
        if (reader.failed || (count && stride != FM_TIMED_ITEM_SIZE)) return false;
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t* item = list + i * stride;
            ptrdiff_t first = arrlen(instance->clips);
            double start = base + fm32(item + FM_TIMED_ITEM_START) / FM_TICKS;
            double end = start + fm32(item + FM_TIMED_ITEM_DURATION) / FM_TICKS;
            if (end > instance->length) instance->length = end;
            if (!scheduleInstrument(system, instance, item, start, end, depth + 1)) return false;
            for (ptrdiff_t j = first; j < arrlen(instance->clips); j++) {
                Clip* clip = &instance->clips[j];
                if (clip->end > instance->length) instance->length = clip->end;
            }
        }
    }
    return true;
}

static bool scheduleInstrument(FmodSystem* system, EventInstance* instance, const uint8_t* id, double start, double end, unsigned depth) {
    if (depth > 16 || arrlen(instance->clips) >= FM_MAX_CLIPS) return false;
    Record* instrument = lookup(system, "WAIB", id);
    if (instrument) {
        Record* wave = lookup(system, "WAV ", instrument->data + FM_GUID_SIZE);
        if (!wave) return false;
        uint32_t sample = fm32(wave->data + FM_WAVE_SAMPLE);
        if (sample >= (uint32_t) arrlen(wave->bank->samples)) return false;
        Clip clip = {0};
        clip.bank = wave->bank;
        clip.sample = (int) sample;
        clip.start = start;
        clip.end = end;
        clip.voice = -1;
        clip.lastGain = -1;
        clip.instrument = findSiblingRecord(instrument, "INST");
        if (end <= start) clip.end = start + (double) wave->bank->samples[sample].frames / wave->bank->samples[sample].rate;
        arrput(instance->clips, clip);
        return true;
    }
    instrument = lookup(system, "MUIB", id);
    if (instrument) {
        Record* playlist = findSiblingRecord(instrument, "PLST");
        if (!playlist || playlist->size < 10) return false;
        FmReader reader = {playlist->data, playlist->size, 8, false};
        uint32_t count, stride;
        const uint8_t* items = fmlist(&reader, &count, &stride);
        if (reader.failed || !count || stride != FM_GUID_SIZE + 4) return false;
        float total = 0;
        for (uint32_t i = 0; i < count; i++) {
            float weight = fmfloat(items + i * stride + FM_GUID_SIZE);
            if (!isfinite(weight) || weight < 0) return false;
            total += weight;
        }
        if (!isfinite(total) || total <= 0) return false;
        float selection = (float) (nextRandom(system) / (double) UINT32_MAX) * total;
        uint32_t index;
        for (index = 0; index + 1 < count; index++) {
            selection -= fmfloat(items + index * stride + FM_GUID_SIZE);
            if (selection < 0) break;
        }
        ptrdiff_t first = arrlen(instance->clips);
        if (!scheduleInstrument(system, instance, items + index * stride, start, end, depth + 1)) return false;
        Record* controller = findSiblingRecord(instrument, "INST");
        if (!controller) controller = instrument;
        for (ptrdiff_t i = first; i < arrlen(instance->clips); i++) {
            Clip* clip = &instance->clips[i];
            if (clip->controllerCount >= 16) return false;
            clip->controllers[clip->controllerCount++] = controller;
        }
        return true;
    }
    instrument = lookup(system, "EVIB", id);
    if (instrument) {
        Record* event = lookup(system, "EVTB", instrument->data + FM_GUID_SIZE);
        if (!event || instance->depth >= 16 || arrlen(instance->children) >= FM_MAX_CLIPS) return false;
        if (end <= start) {
            double length = 0;
            if (!timelineLength(system, event, 0, instance->depth + 1, &length)) return false;
            end = start + length;
        }
        NestedEvent child = {event, findSiblingRecord(instrument, "INST"), start, end, nullptr, false};
        arrput(instance->children, child);
        if (end > instance->length) instance->length = end;
        return true;
    }
    if (lookup(system, "SLNB", id)) return true;
    logWarn("FMOD: Event has an unresolved instrument\n");
    return false;
}

// just the declared length. picking a playlist entry here would change what
// plays next, so keep this separate from scheduling.
static bool timelineLength(FmodSystem* system, Record* event, double base, unsigned depth, double* length) {
    if (!event || depth > 16) return false;
    Record* timeline = lookup(system, "TLNB", event->data + FM_EVENT_TIMELINE);
    if (!timeline) return false;
    FmReader reader = {timeline->data, timeline->size, FM_TIMELINE_LISTS, false};
    for (unsigned group = 0; group < 2; group++) {
        uint32_t count, stride;
        const uint8_t* items = fmlist(&reader, &count, &stride);
        if (reader.failed || (count && stride != FM_TIMED_ITEM_SIZE)) return false;
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t* item = items + i * stride;
            double start = base + fm32(item + FM_TIMED_ITEM_START) / FM_TICKS;
            double end = start + fm32(item + FM_TIMED_ITEM_DURATION) / FM_TICKS;
            if (end > *length) *length = end;
            Record* nested = lookup(system, "EVIB", item);
            if (nested && end <= start && !timelineLength(system, lookup(system, "EVTB", nested->data + FM_GUID_SIZE), start, depth + 1, length)) return false;
        }
    }
    return true;
}

double Fmod_getLength(FmodSystem* system, const char* path) {
    double length = 0;
    return timelineLength(system, findEvent(system, path), 0, 0, &length) ? floor(length * 1000) : -4;
}

// ===[ voice lifetime ]===

static void stopClip(FmodSystem* system, Clip* clip) {
    if (clip->voice != -1) {
        if (clip->voice >= 0) system->audio->vtable->stopSound(system->audio, clip->voice);
        clip->bank->samples[clip->sample].references--;
        system->activeVoices--;
    }
    clip->voice = -1;
    clip->virtualized = false;
    arrfree(clip->modulation);
}

static void stopVoices(FmodSystem* system, EventInstance* instance) {
    for (ptrdiff_t i = 0; i < arrlen(instance->clips); i++) stopClip(system, &instance->clips[i]);
    for (ptrdiff_t i = 0; i < arrlen(instance->children); i++) {
        if (instance->children[i].playback) stopVoices(system, instance->children[i].playback);
    }
}

static void freeInstance(FmodSystem* system, EventInstance* instance);

static void clearPlayback(FmodSystem* system, EventInstance* instance) {
    for (ptrdiff_t i = 0; i < arrlen(instance->clips); i++) stopClip(system, &instance->clips[i]);
    for (ptrdiff_t i = 0; i < arrlen(instance->children); i++) {
        if (instance->children[i].playback) freeInstance(system, instance->children[i].playback);
    }
    arrfree(instance->children);
    arrfree(instance->clocks);
    arrfree(instance->clips);
}

static void freeInstance(FmodSystem* system, EventInstance* instance) {
    clearPlayback(system, instance);
    arrfree(instance->values);
    free(instance);
    system->instanceCount--;
}

// ===[ system and instances ]===

FmodSystem* Fmod_create(AudioSystem* audio, FileSystem* fs, int channels) {
    if (!audio || !audio->vtable || !fs || !fs->vtable || channels < 1) return nullptr;
    AudioSystemVtable* backend = audio->vtable;
    FileSystemVtable* files = fs->vtable;
    if (!backend->playEncoded || !backend->stopSound || !backend->isPlaying || !backend->pauseSound || !backend->resumeSound || !backend->setSoundGain || !backend->setTrackPosition) return nullptr;
    if (!files->binaryOpen || !files->binaryRead || !files->binarySeek || !files->binarySize || !files->binaryClose) return nullptr;
    FmodSystem* system = (FmodSystem*) calloc(1, sizeof(*system));
    if (!system) return nullptr;
    system->audio = audio;
    system->fs = fs;
    system->maxChannels = channels;
    system->nextHandle = audio->fmodNextHandle > 0 ? audio->fmodNextHandle : 1;
    system->random = 0x41c64e6d;
    sh_new_strdup(system->index.names);
    return system;
}

void Fmod_destroy(FmodSystem* system) {
    if (!system) return;
    if (system->audio->fmodSystem == system) system->audio->fmodSystem = nullptr;
    for (ptrdiff_t i = 0; i < arrlen(system->instances); i++) freeInstance(system, system->instances[i]);
    for (ptrdiff_t i = 0; i < arrlen(system->banks); i++) FmodBank_destroy(system->banks[i]);
    FmodBank_clearIndex(&system->index);
    arrfree(system->instances);
    arrfree(system->banks);
    free(system);
}

void Fmod_audioDestroy(AudioSystem* audio) {
    if (!audio) return;
    Fmod_destroy(audio->fmodSystem);
    audio->fmodSystem = nullptr;
}

int Fmod_createInstance(FmodSystem* system, const char* path) {
    Record* event = findEvent(system, path);
    if (!event || system->instanceCount >= FM_MAX_INSTANCES || system->nextHandle >= FM_HANDLE_LIMIT) return -4;
    EventInstance* instance = (EventInstance*) calloc(1, sizeof(*instance));
    if (!instance) return -4;
    system->instanceCount++;
    instance->handle = system->nextHandle++;
    system->audio->fmodNextHandle = system->nextHandle;
    instance->event = event;
    instance->timeline = lookup(system, "TLNB", event->data + FM_EVENT_TIMELINE);
    arrput(system->instances, instance);
    return instance->handle;
}

static bool initializePlayback(FmodSystem* system, EventInstance* instance) {
    clearPlayback(system, instance);
    instance->playing = false;
    instance->position = instance->length = 0;
    instance->stopping = instance->held = false;
    instance->elapsed = instance->stopElapsed = instance->releaseDuration = 0;
    if (!scheduleTimeline(system, instance, instance->event, 0, 0)) {
        clearPlayback(system, instance);
        return false;
    }
    for (ptrdiff_t i = 0; i < arrlen(instance->clips); i++) {
        Clip* clip = &instance->clips[i];
        if (clip->start <= 0 && !FmodBank_prepareSample(system, clip->bank, clip->sample)) return false;
    }
    instance->playing = true;
    return true;
}

bool Fmod_play(FmodSystem* system, int handle) {
    EventInstance* instance = findInstance(system, handle);
    if (!instance || !initializePlayback(system, instance)) return false;
    bool silence = arrlen(instance->clips) == 0;
    Fmod_step(system, 0);
    instance = findInstance(system, handle);
    if (silence) return true;
    if (!instance) return false;
    if (instance->held) return true;
    for (ptrdiff_t i = 0; i < arrlen(instance->clips); i++) {
        if (instance->clips[i].voice != -1 || instance->clips[i].virtualized || instance->clips[i].start > 0) return true;
    }
    instance->playing = false;
    return false;
}

static bool stopPlayback(FmodSystem* system, EventInstance* instance, bool immediate) {
    if (!immediate && instance->stopping) return true;
    instance->releaseDuration = 0;
    for (ptrdiff_t i = 0; i < arrlen(instance->clips); i++) {
        Clip* clip = &instance->clips[i];
        if (clip->started && !clip->finished) clipGain(system, instance, clip, &instance->releaseDuration);
    }
    for (ptrdiff_t i = 0; i < arrlen(instance->children); i++) {
        EventInstance* child = instance->children[i].playback;
        if (!child) continue;
        stopPlayback(system, child, immediate);
        if (child->playing && child->releaseDuration > instance->releaseDuration) instance->releaseDuration = child->releaseDuration;
    }
    if (immediate || !instance->playing || instance->releaseDuration <= 0) {
        stopVoices(system, instance);
        instance->playing = instance->stopping = instance->held = false;
    } else {
        instance->stopElapsed = instance->elapsed;
        instance->stopping = true;
        instance->held = false;
    }
    return true;
}

bool Fmod_stop(FmodSystem* system, int handle, bool immediate) {
    EventInstance* instance = findInstance(system, handle);
    return instance && stopPlayback(system, instance, immediate);
}

bool Fmod_release(FmodSystem* system, int handle) {
    EventInstance* instance = findInstance(system, handle);
    if (!instance) return false;
    instance->released = true;
    Fmod_step(system, 0);
    return true;
}

bool Fmod_isPlaying(FmodSystem* system, int handle) {
    EventInstance* instance = findInstance(system, handle);
    return instance && instance->playing && !instance->stopping && !instance->held;
}

bool Fmod_getPaused(FmodSystem* system, int handle) {
    EventInstance* instance = findInstance(system, handle);
    return instance && instance->paused;
}

static bool pausePlayback(FmodSystem* system, EventInstance* instance, bool paused) {
    if (instance->paused == paused) return true;
    instance->paused = paused;
    for (ptrdiff_t i = 0; i < arrlen(instance->clips); i++) {
        int voice = instance->clips[i].voice;
        if (voice < 0) continue;
        if (paused) system->audio->vtable->pauseSound(system->audio, voice);
        else system->audio->vtable->resumeSound(system->audio, voice);
    }
    for (ptrdiff_t i = 0; i < arrlen(instance->children); i++) {
        if (instance->children[i].playback) pausePlayback(system, instance->children[i].playback, paused);
    }
    return true;
}

bool Fmod_pause(FmodSystem* system, int handle, bool paused) {
    EventInstance* instance = findInstance(system, handle);
    return instance && pausePlayback(system, instance, paused);
}

void Fmod_pauseAll(FmodSystem* system, bool pause) {
    if (!system) return;
    for (ptrdiff_t i = 0; i < arrlen(system->instances); i++) {
        EventInstance* instance = system->instances[i];
        if (!instance->released && !instance->oneshot) Fmod_pause(system, instance->handle, pause);
    }
}

bool Fmod_setSpatial(FmodSystem* system, int handle, float x, float y) {
    EventInstance* instance = findInstance(system, handle);
    if (!instance || !system->audio->vtable->setSoundSpatial || !isfinite(x) || !isfinite(y)) return false;
    instance->spatial = true;
    instance->x = x;
    instance->y = y;
    Fmod_step(system, 0);
    return true;
}

bool Fmod_setListeners(FmodSystem* system, int count) {
    // the playback adapters only handle one spatial listener for now.
    return system && count == 1;
}

bool Fmod_setListener(FmodSystem* system, int listener, float x, float y) {
    if (!system || listener != 0 || !system->audio->vtable->setListenerPosition || !isfinite(x) || !isfinite(y)) return false;
    system->audio->vtable->setListenerPosition(system->audio, x, y, 0);
    return true;
}

// ===[ timeline lists and conditions ]===

static bool seekTimelineList(Record* timeline, unsigned index, FmReader* reader) {
    if (!timeline) return false;
    reader->data = timeline->data;
    reader->size = timeline->size;
    reader->position = FM_TIMELINE_LISTS;
    reader->failed = false;
    for (unsigned i = 0; i < index; i++) {
        FmList list;
        if (!FmList_begin(&list, reader)) return false;
        while (list.remaining && !reader->failed) {
            uint32_t bytes;
            FmList_next(&list, &bytes);
        }
        if (reader->failed) return false;
    }
    return true;
}

static bool markerPosition(Record* timeline, const uint8_t* id, double* position) {
    FmReader reader;
    if (!seekTimelineList(timeline, 3, &reader)) return false;
    FmList list;
    if (!FmList_begin(&list, &reader)) return false;
    while (list.remaining && !reader.failed) {
        uint32_t bytes;
        const uint8_t* marker = FmList_next(&list, &bytes);
        if (!marker || bytes < FM_GUID_SIZE + 4) return false;
        if (equalGuid(marker, id)) {
            *position = fm32(marker + FM_GUID_SIZE) / FM_TICKS;
            return true;
        }
    }
    return false;
}

static bool parameterConditions(FmodSystem* system, EventInstance* instance, const uint8_t* data, size_t bytes) {
    size_t position = 0;
    while (position < bytes) {
        if (bytes - position < FM_CONDITION_SIZE) return false;
        uint32_t flags = fm32(data + position);
        int index = findParameterIndex(&system->index, data + position + FM_CONDITION_PARAMETER);
        if (index < 0) return false;
        float value = parameterValue(system, instance, index);
        float low = fmfloat(data + position + FM_CONDITION_LOW);
        float high = fmfloat(data + position + FM_CONDITION_HIGH);
        if (!isfinite(low) || !isfinite(high) || low > high || (flags & 0xff) != 0x11) return false;
        if (value < low - 0.0001f || value > high + 0.0001f) return false;
        position += FM_CONDITION_SIZE;
        if (bytes - position >= 4 && fm32(data + position) <= 2) position += 4;
    }
    return true;
}

static void updateSustain(FmodSystem* system, EventInstance* instance, double previous) {
    FmReader reader;
    instance->held = false;
    if (!seekTimelineList(instance->timeline, 2, &reader)) return;
    FmList list;
    if (!FmList_begin(&list, &reader)) return;
    while (list.remaining && !reader.failed) {
        uint32_t entryBytes;
        const uint8_t* point = FmList_next(&list, &entryBytes);
        if (!point || entryBytes < 8) return;
        double time = fm32(point) / FM_TICKS;
        uint32_t bytes = fm32(point + 4);
        if (bytes > entryBytes - 8) return;
        if (previous <= time + 0.000001 && instance->position >= time && parameterConditions(system, instance, point + 8, bytes)) {
            instance->position = time;
            instance->held = true;
            return;
        }
    }
}

static void reposition(FmodSystem* system, EventInstance* instance, double position) {
    instance->position = position;
    instance->held = false;
    for (ptrdiff_t i = 0; i < arrlen(instance->clips); i++) {
        Clip* clip = &instance->clips[i];
        bool virtualized = clip->virtualized;
        clip->finished = clip->started = false;
        clip->virtualized = false;
        if (position >= clip->start && position < clip->end && (clip->voice >= 0 || virtualized)) {
            clip->sourcePosition = (position - clip->start) * clipPitch(system, instance, clip);
            if (clip->voice >= 0) {
                system->audio->vtable->setTrackPosition(system->audio, clip->voice, (float) clip->sourcePosition);
                if (instance->playing && !instance->paused) system->audio->vtable->resumeSound(system->audio, clip->voice);
            }
            clip->started = true;
            clip->virtualized = virtualized;
        } else {
            stopClip(system, clip);
        }
    }
    for (ptrdiff_t i = 0; i < arrlen(instance->children); i++) {
        NestedEvent* child = &instance->children[i];
        child->finished = false;
        if (!child->playback) continue;
        if (position >= child->start && position < child->end && child->playback->playing) {
            reposition(system, child->playback, position - child->start);
        } else {
            freeInstance(system, child->playback);
            child->playback = nullptr;
        }
    }
}

bool Fmod_setPosition(FmodSystem* system, int handle, double milliseconds) {
    EventInstance* instance = findInstance(system, handle);
    if (!instance || !isfinite(milliseconds) || milliseconds < 0 || milliseconds > INT32_MAX) return false;
    reposition(system, instance, milliseconds / 1000);
    Fmod_step(system, 0);
    return true;
}

double Fmod_getPosition(FmodSystem* system, int handle) {
    EventInstance* instance = findInstance(system, handle);
    return instance ? floor(instance->position * 1000) : 0;
}

// ===[ event update ]===

static void advanceTimeline(FmodSystem* system, EventInstance* instance, double seconds) {
    if (!instance->playing || instance->paused) return;
    bool starting = instance->elapsed == 0 && seconds > 0;
    for (ptrdiff_t i = 0; i < arrlen(instance->values); i++) {
        LocalValue* value = &instance->values[i];
        value->current = advanceParameter(value->current, value->value, &system->index.parameters[value->parameter], seconds);
    }
    double previous = instance->position;
    instance->elapsed += seconds;
    instance->position += seconds;
    if (instance->stopping) {
        if (instance->elapsed - instance->stopElapsed >= instance->releaseDuration) {
            stopVoices(system, instance);
            instance->playing = false;
        }
        return;
    }
    updateSustain(system, instance, previous);
    if (instance->held || !instance->timeline) return;
    FmBank* bank = instance->timeline->bank;
    for (ptrdiff_t i = instance->timeline->parent + 1; i < arrlen(bank->records); i++) {
        Record* transition = &bank->records[i];
        if (transition->parent < instance->timeline->parent) break;
        if (!tagIs(transition, "TRNB") || transition->size < FM_TRANSITION_CONDITIONS + 16) continue;
        double start = fm32(transition->data + FM_TRANSITION_START) / FM_TICKS;
        double end = fm32(transition->data + FM_TRANSITION_END) / FM_TICKS;
        if (instance->position < start) continue;
        // a destination at the end of a transition region is already outside
        // that region. repeated parameter/spatial polls must not seek it again.
        if (end > start && instance->position >= end) continue;
        if (start == end) {
            // landing on a cue doesn't cross a loop point at that same time.
            // only the initial origin can trigger without a prior position.
            if (previous >= start && !(starting && start == 0 && previous == 0)) continue;
        }
        uint32_t bytes = fm32(transition->data + FM_TRANSITION_CONDITION_SIZE);
        if (bytes > transition->size - FM_TRANSITION_CONDITIONS) continue;
        if (!parameterConditions(system, instance, transition->data + FM_TRANSITION_CONDITIONS, bytes)) continue;
        double target;
        if (!markerPosition(instance->timeline, transition->data + FM_GUID_SIZE, &target)) continue;
        if (start == end || bytes != 0) {
            reposition(system, instance, target + (start == end ? fmax(0, instance->position - start) : 0));
            break;
        }
    }
}

static bool startClip(FmodSystem* system, EventInstance* instance, Clip* clip) {
    if (!clip->started) {
        clip->activated = instance->elapsed;
        clip->lastPitch = clipPitch(system, instance, clip);
        clip->sourcePosition = fmax(0, instance->position - clip->start) * clip->lastPitch;
    }
    clip->started = true;
    clip->virtualized = true;
    if (system->activeVoices >= system->maxChannels) return true;
    if (!FmodBank_prepareSample(system, clip->bank, clip->sample)) {
        clip->virtualized = false;
        return false;
    }
    FsbSample* sample = &clip->bank->samples[clip->sample];
    sample->references++;
    clip->voice = system->audio->vtable->playEncoded(system->audio, sample->ogg, sample->oggSize, false);
    if (clip->voice == -1) {
        sample->references--;
        return true;
    }
    system->activeVoices++;
    clip->virtualized = false;
    clip->lastGain = -1;
    if (clip->voice >= 0 && system->audio->vtable->setSoundPitch) system->audio->vtable->setSoundPitch(system->audio, clip->voice, clip->lastPitch);
    if (clip->voice >= 0 && clip->sourcePosition > 0) system->audio->vtable->setTrackPosition(system->audio, clip->voice, (float) clip->sourcePosition);
    if (instance->paused && clip->voice >= 0) system->audio->vtable->pauseSound(system->audio, clip->voice);
    return true;
}

static bool updateClip(FmodSystem* system, EventInstance* instance, Clip* clip, double seconds) {
    if (clip->started && !instance->paused) clip->sourcePosition += seconds * clip->lastPitch;
    if (instance->stopping && !clip->started) {
        clip->finished = true;
        return false;
    }
    if (instance->held && !clip->started && clip->start >= instance->position - 0.000001) return true;
    if (instance->position < clip->start) return true;
    if (instance->position >= clip->end) {
        stopClip(system, clip);
        clip->finished = true;
        return false;
    }
    if (!clip->finished && (!clip->started || (clip->virtualized && system->activeVoices < system->maxChannels && !instance->paused))) {
        if (!startClip(system, instance, clip)) clip->finished = true;
    }
    float gain = clip->started && !clip->finished ? clipGain(system, instance, clip, nullptr) : 0;
    float pitch = clip->started && !clip->finished ? clipPitch(system, instance, clip) : 1;
    if (pitch != clip->lastPitch) {
        clip->lastPitch = pitch;
        if (clip->voice >= 0 && system->audio->vtable->setSoundPitch) system->audio->vtable->setSoundPitch(system->audio, clip->voice, pitch);
    }
    if (clip->voice >= 0) {
        if (gain != clip->lastGain) {
            system->audio->vtable->setSoundGain(system->audio, clip->voice, gain, 0);
            clip->lastGain = gain;
        }
        if (instance->spatial) system->audio->vtable->setSoundSpatial(system->audio, clip->voice, instance->x, instance->y, 0, 100, 100000, 1);
        if (!instance->paused && !system->audio->vtable->isPlaying(system->audio, clip->voice)) {
            stopClip(system, clip);
            clip->finished = true;
        }
    }
    return !clip->finished;
}

static void stepPlayback(FmodSystem* system, EventInstance* instance, double seconds) {
    advanceTimeline(system, instance, seconds);
    bool pending = false;
    if (instance->playing) {
        for (ptrdiff_t j = 0; j < arrlen(instance->clips); j++) {
            if (updateClip(system, instance, &instance->clips[j], seconds)) pending = true;
        }
        for (ptrdiff_t j = 0; j < arrlen(instance->children); j++) {
            NestedEvent* child = &instance->children[j];
            if (child->finished) continue;
            if (instance->position >= child->end || (instance->stopping && !child->playback)) {
                if (child->playback) stopPlayback(system, child->playback, true);
                child->finished = true;
                continue;
            }
            if (instance->position < child->start || (instance->held && !child->playback && child->start >= instance->position - 0.000001)) {
                pending = true;
                continue;
            }
            double delta = instance->paused ? 0 : seconds;
            if (!child->playback) {
                if (system->instanceCount >= FM_MAX_INSTANCES) {
                    child->finished = true;
                    continue;
                }
                child->playback = (EventInstance*) calloc(1, sizeof(*child->playback));
                if (!child->playback) {
                    child->finished = true;
                    continue;
                }
                system->instanceCount++;
                EventInstance* playback = child->playback;
                playback->event = child->event;
                playback->timeline = lookup(system, "TLNB", child->event->data + FM_EVENT_TIMELINE);
                playback->parent = instance;
                playback->parentInstrument = child->instrument;
                playback->depth = instance->depth + 1;
                playback->paused = instance->paused;
                if (!initializePlayback(system, playback)) {
                    child->finished = true;
                    continue;
                }
                double offset = fmax(0, instance->position - child->start);
                delta = instance->paused ? 0 : fmin(seconds, offset);
                playback->parentActivated = instance->elapsed - delta;
                if (offset > delta) reposition(system, playback, offset - delta);
            }
            child->playback->spatial = instance->spatial;
            child->playback->x = instance->x;
            child->playback->y = instance->y;
            stepPlayback(system, child->playback, delta);
            if (child->playback->playing) pending = true;
            else child->finished = true;
        }
    }
    if (instance->playing && !instance->paused && !instance->held && !pending && instance->position >= instance->length) {
        stopVoices(system, instance);
        instance->playing = false;
    }
}

void Fmod_step(FmodSystem* system, double seconds) {
    if (!system || !isfinite(seconds) || seconds < 0) return;
    for (ptrdiff_t i = 0; i < arrlen(system->index.parameters); i++) {
        Parameter* parameter = &system->index.parameters[i];
        if (parameter->global) parameter->current = advanceParameter(parameter->current, parameter->value, parameter, seconds);
    }
    for (ptrdiff_t i = 0; i < arrlen(system->instances);) {
        EventInstance* instance = system->instances[i];
        stepPlayback(system, instance, seconds);
        if (!instance->playing && (instance->released || instance->oneshot)) {
            freeInstance(system, instance);
            arrdel(system->instances, i);
        } else {
            i++;
        }
    }
    FmodBank_evict(system, nullptr);
}

bool Fmod_oneShot(FmodSystem* system, const char* path, bool spatial, float x, float y) {
    if (spatial && (!isfinite(x) || !isfinite(y) || !system || !system->audio->vtable->setSoundSpatial)) return false;
    int handle = Fmod_createInstance(system, path);
    if (handle < 0) return false;
    EventInstance* instance = findInstance(system, handle);
    instance->oneshot = true;
    instance->spatial = spatial;
    instance->x = x;
    instance->y = y;
    if (!Fmod_play(system, handle)) {
        Fmod_release(system, handle);
        return false;
    }
    return true;
}
