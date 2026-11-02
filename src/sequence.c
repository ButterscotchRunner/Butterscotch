#include "sequence.h"
#include "runner.h"
#include "text_utils.h"
#include "stb_ds.h"
#include "math_compat.h"

typedef struct {
    int32_t instanceId;
    int32_t soundId;
    float soundDirection;
    RuntimeSequence* nested;
    PreprocessedText text;
    int32_t wrapWidth;
    bool textPrepared;
} SequenceChannelState;

typedef struct {
    float x, y, scaleX, scaleY, rotation, originX, originY;
    float color[4];
    float imageSpeed, imageIndex, gain, pitch;
    float frameWidth, frameHeight, characterSpacing, lineSpacing, paragraphSpacing;
    bool hasImageSpeed, hasImageIndex;
} SequenceValues;

struct SequenceNode {
    const SequenceTrack* track;
    SequenceNode* children;
    SequenceChannelState* states;
    uint32_t stateCount;
    int32_t activeKey;
    bool visible;
    Matrix4f matrix;
    SequenceValues values;
};

struct SequenceEvent {
    int32_t elementId;
    const char* message;
    bool moment;
};

static void evaluateSequence(RuntimeSequence* seq, const Matrix4f* parent, const float* color, float previous, SequenceEvent** events, bool update, bool visible);

static float smoothCurveValue(const AnimCurveChannel* ch, uint32_t lo, float x) {
    float minimum = ch->points[0].value, maximum = minimum;
    for (uint32_t pointIndex = 1; pointIndex < ch->pointCount; pointIndex++) {
        minimum = (float)GMLReal_fmin(minimum, ch->points[pointIndex].value);
        maximum = (float)GMLReal_fmax(maximum, ch->points[pointIndex].value);
    }
    float scale = (float)GMLReal_fmax(1.0f, maximum - minimum);
    uint32_t indices[4] = {lo > 0 ? lo - 1 : lo, lo, lo + 1,
        lo + 2 < ch->pointCount ? lo + 2 : lo + 1};
    float px[4], py[4], times[4] = {0};
    repeat(4, i) {
        px[i] = ch->points[indices[i]].x;
        py[i] = (ch->points[indices[i]].value - minimum) / scale;
        if (i > 0) {
            float dx = px[i] - px[i - 1], dy = py[i] - py[i - 1];
            float distance = dx * dx + dy * dy;
            times[i] = times[i - 1] + sqrtf(sqrtf(distance == 0.0f ? 0.0001f : distance));
        }
    }
    uint32_t iterations = ch->iterations > 0 ? ch->iterations : 1;
    float lastX = px[1], lastY = py[1];
    for (uint32_t sampleIndex = 1; sampleIndex <= iterations; sampleIndex++) {
        float t = times[1] + (times[2] - times[1]) * ((float)sampleIndex / iterations);
        float ax[3], ay[3], bx[2], by[2];
        repeat(3, j) {
            float f = (t - times[j]) / (times[j + 1] - times[j]);
            ax[j] = px[j] + (px[j + 1] - px[j]) * f;
            ay[j] = py[j] + (py[j + 1] - py[j]) * f;
        }
        repeat(2, bIndex) {
            float f = (t - times[bIndex]) / (times[bIndex + 2] - times[bIndex]);
            bx[bIndex] = ax[bIndex] + (ax[bIndex + 1] - ax[bIndex]) * f;
            by[bIndex] = ay[bIndex] + (ay[bIndex + 1] - ay[bIndex]) * f;
        }
        float f = (t - times[1]) / (times[2] - times[1]);
        float cx = (float)GMLReal_fmin(px[2], GMLReal_fmax(lastX, bx[0] + (bx[1] - bx[0]) * f));
        float cy = by[0] + (by[1] - by[0]) * f;
        if (cx >= x && cx > lastX) {
            return (lastY + (cy - lastY) * (x - lastX) / (cx - lastX)) * scale + minimum;
        }
        lastX = cx;
        lastY = cy;
    }
    return ch->points[lo + 1].value;
}

float AnimCurveChannel_evaluate(const AnimCurveChannel* ch, float x) {
    if (ch == nullptr || ch->pointCount == 0) return 0.0f;
    if (ch->pointCount == 1 || x <= ch->points[0].x) return ch->points[0].value;
    if (x >= ch->points[ch->pointCount - 1].x) return ch->points[ch->pointCount - 1].value;
    uint32_t lo = 0, hi = ch->pointCount - 1;
    while (hi - lo > 1) {
        uint32_t mid = (lo + hi) / 2;
        if (ch->points[mid].x <= x) lo = mid;
        else hi = mid;
    }
    const AnimCurvePoint* a = &ch->points[lo];
    const AnimCurvePoint* b = &ch->points[hi];
    if (b->x <= a->x) return a->value;
    float t = (x - a->x) / (b->x - a->x);
    if (ch->curveType == ANIMCURVE_TYPE_SMOOTH) {
        return smoothCurveValue(ch, lo, x);
    }
    if (ch->curveType == ANIMCURVE_TYPE_BEZIER) {
        float left = 0.0f, right = 1.0f;
        repeat(20, n) {
            t = (left + right) * 0.5f;
            float q = 1.0f - t;
            float bx = q*q*q*a->x + 3*q*q*t*(a->x + a->bezierX1) + 3*q*t*t*(b->x + b->bezierX0) + t*t*t*b->x;
            if (bx < x) left = t;
            else right = t;
        }
        float q = 1.0f - t;
        return q*q*q*a->value + 3*q*q*t*(a->value + a->bezierY1) + 3*q*t*t*(b->value + b->bezierY0) + t*t*t*b->value;
    }
    return a->value + (b->value - a->value) * t;
}

static bool isParameter(const SequenceTrack* track) {
    return track->type >= SEQUENCE_TRACK_REAL;
}

static const SequenceChannel* findChannel(const SequenceKeyframe* key, int32_t id) {
    for (uint32_t c = 0; c < key->channelCount; c++) {
        if (key->channels[c].id == id) return &key->channels[c];
    }
    return nullptr;
}

static float keyEnd(const SequenceTrack* track, uint32_t index, float length) {
    const SequenceKeyframe* key = &track->keyframes[index];
    if (key->stretch) return index + 1 < track->keyframeCount ? track->keyframes[index + 1].key : length;
    return key->key + key->length;
}

static int32_t activeKey(const SequenceTrack* track, float head, float length) {
    for (int32_t k = (int32_t)track->keyframeCount - 1; k >= 0; k--) {
        const SequenceKeyframe* key = &track->keyframes[k];
        if (key->disabled) continue;
        float end = keyEnd(track, (uint32_t)k, length);
        if (head >= key->key && head < end) return k;
        if ((uint32_t)k == track->keyframeCount - 1 && head == length && end == length) return k;
    }
    return -1;
}

static const SequenceChannel* parameterChannel(const SequenceKeyframe* key, int32_t id) {
    const SequenceChannel* channel = findChannel(key, id);
    if (channel == nullptr && key->channelCount > 0) {
        const SequenceChannel* first = &key->channels[0];
        if (first->embeddedCurve != nullptr || first->curveIndex >= 0) channel = first;
    }
    return channel;
}

static float channelValue(const SequenceTrack* track, const SequenceChannel* channel, int32_t component) {
    if (track->type != SEQUENCE_TRACK_COLOR) return channel->real;
    return (float)(((uint32_t)channel->integer >> (component * 8)) & 255u) / 255.0f;
}

static float evaluateParameter(Runner* runner, const SequenceTrack* track, int32_t channelId, int32_t component, float head, float length, float fallback) {
    const SequenceChannel* previous = nullptr;
    const SequenceChannel* next = nullptr;
    uint32_t keyIndex = 0;
    float previousTime = 0.0f, nextTime = 0.0f;
    for (uint32_t k = 0; k < track->keyframeCount; k++) {
        const SequenceKeyframe* key = &track->keyframes[k];
        if (key->disabled) continue;
        const SequenceChannel* channel = parameterChannel(key, channelId);
        if (channel == nullptr) continue;
        if (key->key > head && previous != nullptr) {
            next = channel;
            nextTime = key->key;
            break;
        }
        previous = channel;
        keyIndex = k;
        previousTime = key->key;
        if (key->key > head) break;
    }
    if (previous == nullptr) return fallback;
    const SequenceKeyframe* key = &track->keyframes[keyIndex];
    const AnimCurve* curve = previous->embeddedCurve;
    if (curve == nullptr && previous->curveIndex >= 0 && (uint32_t)previous->curveIndex < runner->dataWin->acrv.count) {
        curve = &runner->dataWin->acrv.curves[previous->curveIndex];
    }
    float end = keyEnd(track, keyIndex, length + 1.0f);
    if (curve != nullptr && curve->channelCount > 0) {
        uint32_t c = (uint32_t)channelId < curve->channelCount ? (uint32_t)channelId : curve->channelCount - 1;
        float fraction = end > key->key ? (head - key->key) / (end - key->key) : 0.0f;
        return AnimCurveChannel_evaluate(&curve->channels[c], fraction);
    }
    float value = channelValue(track, previous, component);
    if (!track->interpolation || next == nullptr || key->stretch) return value;
    if (key->length > 1.0f) previousTime = end;
    if (head <= previousTime || nextTime <= previousTime) return value;
    float fraction = (head - previousTime) / (nextTime - previousTime);
    return value + (channelValue(track, next, component) - value) * fraction;
}

static SequenceValues evaluateValues(RuntimeSequence* seq, const SequenceTrack* track, float head) {
    SequenceValues value = {0};
    value.scaleX = value.scaleY = value.imageSpeed = value.gain = value.pitch = 1.0f;
    repeat(4, c) value.color[c] = 1.0f;
    for (uint32_t t = 0; t < track->trackCount; t++) {
        const SequenceTrack* parameter = &track->tracks[t];
        if (!isParameter(parameter) || !parameter->enabled) continue;
        float at = parameter->creationTrack ? 0.0f : head;
        float x = evaluateParameter(seq->runner, parameter, 0, 0, at, seq->asset->length, 0.0f);
        float y = evaluateParameter(seq->runner, parameter, 1, 0, at, seq->asset->length, 0.0f);
        switch (parameter->builtinName) {
            case 5: value.gain = x; break;
            case 6: value.pitch = x; break;
            case 8: value.rotation = x; break;
            case 10: {
                repeat(4, c) value.color[c] = evaluateParameter(seq->runner, parameter, 0, c, at, seq->asset->length, 1.0f);
                break;
            }
            case 14: value.x = x; value.y = y; break;
            case 15: value.scaleX = x; value.scaleY = y; break;
            case 16: value.originX = x; value.originY = y; break;
            case 17: value.imageSpeed = x; value.hasImageSpeed = true; break;
            case 18: value.imageIndex = x; value.hasImageIndex = true; break;
            case 20: value.frameWidth = x; value.frameHeight = y; break;
            case 21: value.characterSpacing = x; break;
            case 22: value.lineSpacing = x; break;
            case 23: value.paragraphSpacing = x; break;
        }
    }
    return value;
}

static RuntimeSequence* newSequence(Runner* runner, int32_t assetIndex, int32_t elementId, int32_t layerId) {
    if (assetIndex < 0 || (uint32_t)assetIndex >= runner->dataWin->seqn.count) return nullptr;
    Sequence* asset = &runner->dataWin->seqn.sequences[assetIndex];
    if (!asset->present) return nullptr;
    RuntimeSequence* seq = (RuntimeSequence*)safeCalloc(1, sizeof(RuntimeSequence));
    seq->runner = runner;
    seq->root = seq;
    seq->asset = asset;
    seq->assetIndex = assetIndex;
    seq->elementId = elementId;
    seq->layerId = layerId;
    seq->scaleX = seq->scaleY = seq->direction = seq->speedScale = seq->alpha = 1.0f;
    seq->blend = 0xFFFFFFu;
    seq->references = 1;
    return seq;
}

RuntimeSequence* Sequence_find(Runner* runner, int32_t elementId) {
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, elementId, nullptr);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sequence) return nullptr;
    return el->sequenceElement;
}

int32_t Sequence_create(Runner* runner, int32_t layerId, int32_t assetIndex, float x, float y) {
    RuntimeLayer* layer = Runner_findRuntimeLayerById(runner, layerId);
    if (layer == nullptr) return -1;
    int32_t elementId = (int32_t)Runner_getNextLayerId(runner);
    RuntimeSequence* seq = newSequence(runner, assetIndex, elementId, layerId);
    if (seq == nullptr) return -1;
    seq->x = x;
    seq->y = y;
    RuntimeLayerElement el = {0};
    el.id = (uint32_t)elementId;
    el.type = RuntimeLayerElementType_Sequence;
    el.sequenceElement = seq;
    el.visible = true;
    el.alpha = 1.0f;
    el.blend = 0xFFFFFFu;
    arrput(layer->elements, el);
    runner->drawableListStructureDirty = true;
    return elementId;
}

static void freeNodes(RuntimeSequence* seq, SequenceNode* nodes, uint32_t count) {
    repeat(count, t) {
        SequenceNode* node = &nodes[t];
        freeNodes(seq, node->children, node->track->trackCount);
        repeat(node->stateCount, c) {
            SequenceChannelState* state = &node->states[c];
            if (state->soundId >= 0 && seq->runner->audioSystem != nullptr) {
                seq->runner->audioSystem->vtable->stopSound(seq->runner->audioSystem, state->soundId);
            }
            if (state->nested != nullptr) Sequence_release(state->nested);
            PreprocessedText_free(state->text);
        }
        free(node->states);
    }
    free(nodes);
}

void Sequence_release(RuntimeSequence* seq) {
    if (seq == nullptr || --seq->references != 0) return;
    if (seq->nodes != nullptr) freeNodes(seq, seq->nodes, seq->asset->trackCount);
    free(seq);
}

static void destroyNodeInstances(RuntimeSequence* seq, SequenceNode* nodes, uint32_t count) {
    repeat(count, t) {
        SequenceNode* node = &nodes[t];
        destroyNodeInstances(seq, node->children, node->track->trackCount);
        repeat(node->stateCount, c) {
            SequenceChannelState* state = &node->states[c];
            if (state->instanceId >= 0) {
                Instance* inst = hmget(seq->runner->instancesById, state->instanceId);
                if (inst != nullptr && !inst->destroyed) Runner_destroyInstance(seq->runner, inst, true);
            }
            if (state->nested != nullptr && state->nested->nodes != nullptr) {
                destroyNodeInstances(state->nested, state->nested->nodes, state->nested->asset->trackCount);
            }
        }
    }
}

void Sequence_destroy(Runner* runner, int32_t elementId) {
    RuntimeLayer* layer = nullptr;
    RuntimeLayerElement* el = Runner_findLayerElementById(runner, elementId, &layer);
    if (el == nullptr || el->type != RuntimeLayerElementType_Sequence) return;
    RuntimeSequence* seq = el->sequenceElement;
    seq->destroyed = true;
    arrdel(layer->elements, el - layer->elements);
    if (seq->nodes != nullptr) destroyNodeInstances(seq, seq->nodes, seq->asset->trackCount);
    Sequence_release(seq);
}

static void pauseNodes(RuntimeSequence* seq, SequenceNode* nodes, uint32_t count, bool paused) {
    repeat(count, t) {
        SequenceNode* node = &nodes[t];
        pauseNodes(seq, node->children, node->track->trackCount, paused);
        repeat(node->stateCount, c) {
            SequenceChannelState* state = &node->states[c];
            AudioSystem* audio = seq->runner->audioSystem;
            if (state->soundId >= 0 && audio != nullptr) {
                if (paused) audio->vtable->pauseSound(audio, state->soundId);
                else audio->vtable->resumeSound(audio, state->soundId);
            }
            if (state->nested != nullptr) Sequence_setPaused(state->nested, paused);
        }
    }
}

void Sequence_setPaused(RuntimeSequence* seq, bool paused) {
    if (seq == nullptr || seq->paused == paused) return;
    seq->paused = paused;
    if (seq->nodes != nullptr) pauseNodes(seq, seq->nodes, seq->asset->trackCount, paused);
}

void Sequence_play(RuntimeSequence* seq) {
    if (seq == nullptr) return;
    Sequence_setPaused(seq, false);
    if (seq->finished) {
        seq->headPosition = seq->direction < 0.0f ? (float)GMLReal_fmax(0.0f, seq->asset->length - 1.0f) : 0.0f;
        seq->finished = false;
    }
}

static SequenceNode* createNodes(RuntimeSequence* seq, const SequenceTrack* tracks, uint32_t count, uint32_t depth) {
    requireMessageFormatted(__FILE__, __LINE__, depth < 32, "Sequence nesting exceeds 32 levels");
    SequenceNode* nodes = (SequenceNode*)safeCalloc(count, sizeof(SequenceNode));
    repeat(count, t) {
        SequenceNode* node = &nodes[t];
        node->track = &tracks[t];
        node->activeKey = -1;
        node->children = createNodes(seq, tracks[t].tracks, tracks[t].trackCount, depth + 1);
        if (isParameter(node->track)) continue;
        for (uint32_t keyIndex = 0; keyIndex < tracks[t].keyframeCount; keyIndex++) node->stateCount += tracks[t].keyframes[keyIndex].channelCount;
        node->states = (SequenceChannelState*)safeCalloc(node->stateCount, sizeof(SequenceChannelState));
        uint32_t index = 0;
        for (uint32_t k = 0; k < tracks[t].keyframeCount; k++) {
            const SequenceKeyframe* key = &tracks[t].keyframes[k];
            for (uint32_t c = 0; c < key->channelCount; c++) {
                SequenceChannelState* state = &node->states[index++];
                state->instanceId = state->soundId = -1;
                if (tracks[t].type == SEQUENCE_TRACK_SEQUENCE) {
                    state->nested = newSequence(seq->runner, key->channels[c].integer, seq->elementId, seq->layerId);
                    if (state->nested != nullptr) {
                        state->nested->root = seq->root;
                        state->nested->nodes = createNodes(state->nested, state->nested->asset->tracks, state->nested->asset->trackCount, depth + 1);
                        state->nested->initialized = true;
                    }
                }
            }
        }
    }
    return nodes;
}

static void initializeNodeInstances(RuntimeSequence* seq, SequenceNode* nodes, uint32_t count) {
    repeat(count, t) {
        SequenceNode* node = &nodes[t];
        const SequenceTrack* track = node->track;
        uint32_t index = 0;
        for (uint32_t k = 0; k < track->keyframeCount; k++) {
            const SequenceKeyframe* key = &track->keyframes[k];
            for (uint32_t c = 0; c < key->channelCount; c++) {
                if (isParameter(track)) continue;
                if (seq->root->destroyed) return;
                SequenceChannelState* state = &node->states[index++];
                if (track->type == SEQUENCE_TRACK_INSTANCE && key->channels[c].integer >= 0) {
                    Instance* inst = Runner_createInstanceWithLayer(seq->runner, 0, 0, key->channels[c].integer, seq->layerId);
                    if (inst != nullptr) {
                        state->instanceId = (int32_t)inst->instanceId;
                        if (seq->root->destroyed) {
                            if (!inst->destroyed) Runner_destroyInstance(seq->runner, inst, true);
                            return;
                        }
                        inst->sequenceControlled = true;
                        inst->visible = false;
                    }
                } else if (state->nested != nullptr) {
                    initializeNodeInstances(state->nested, state->nested->nodes, state->nested->asset->trackCount);
                }
            }
        }
        if (seq->root->destroyed) return;
        initializeNodeInstances(seq, node->children, track->trackCount);
    }
}

static void initializeSequence(RuntimeSequence* seq) {
    if (seq->initialized) return;
    seq->initialized = true;
    seq->nodes = createNodes(seq, seq->asset->tracks, seq->asset->trackCount, 0);
    initializeNodeInstances(seq, seq->nodes, seq->asset->trackCount);
}

static float speedPerSecond(const Sequence* asset, float fps) {
    return asset->playbackSpeedType == 0 ? asset->playbackSpeed : asset->playbackSpeed * fps;
}

static void wrapSequenceHead(RuntimeSequence* seq) {
    float length = seq->asset->length;
    if (length <= 0.0f) {
        seq->headPosition = 0.0f;
        seq->finished = true;
        return;
    }
    if (seq->asset->playback == 0) {
        if (seq->headPosition >= length) {
            seq->headPosition = length;
            seq->finished = seq->direction > 0.0f;
        } else if (seq->headPosition <= 0.0f) {
            seq->headPosition = 0.0f;
            seq->finished = seq->direction < 0.0f;
        }
    } else if (seq->asset->playback == 1) {
        seq->headPosition = fmodf(seq->headPosition, length);
        if (seq->headPosition < 0.0f) seq->headPosition += length;
    } else if (seq->headPosition < 0.0f || seq->headPosition >= length) {
        float position = fabsf(seq->headPosition);
        float cycle = fmodf(position, length * 2.0f);
        seq->direction = cycle >= length ? -1.0f : 1.0f;
        seq->headPosition = cycle >= length ? length * 2.0f - cycle : cycle;
    }
}

static float imageSpeedDistance(RuntimeSequence* seq, const SequenceTrack* track, float start, float end) {
    float distance = 0.0f;
    float position = start;
    while (position < end) {
        float next = end;
        for (uint32_t k = 0; k < track->keyframeCount; k++) {
            const SequenceKeyframe* key = &track->keyframes[k];
            if (key->disabled) continue;
            if (key->key > position && key->key < next) next = key->key;
            if (key->length > 1.0f && !key->stretch) {
                float heldEnd = keyEnd(track, k, seq->asset->length + 1.0f);
                if (heldEnd > position && heldEnd < next) next = heldEnd;
            }
        }
        float middle = position + (next - position) * 0.5f;
        float speed = evaluateParameter(seq->runner, track, 0, 0, middle, seq->asset->length, 0.0f);
        distance += (next - position) * speed;
        position = next;
    }
    return (float)GMLReal_fmax(0.0f, distance);
}

static float spriteFrame(RuntimeSequence* seq, const SequenceNode* node, int32_t spriteIndex, float elapsed) {
    if (node->values.hasImageIndex) return node->values.imageIndex;
    if (spriteIndex < 0 || (uint32_t)spriteIndex >= seq->runner->dataWin->sprt.count) return 0.0f;
    Sprite* sprite = &seq->runner->dataWin->sprt.sprites[spriteIndex];
    float fps = (float)Runner_getEffectiveGameSpeed(seq->runner);
    float speed = speedPerSecond(seq->asset, fps);
    if (speed == 0.0f) return 0.0f;
    float spriteSpeed = sprite->gms2PlaybackSpeedType == 0 ? sprite->gms2PlaybackSpeed : sprite->gms2PlaybackSpeed * fps;
    float distance = elapsed * node->values.imageSpeed;
    if (node->values.hasImageSpeed) {
        for (uint32_t t = 0; t < node->track->trackCount; t++) {
            const SequenceTrack* parameter = &node->track->tracks[t];
            if (parameter->builtinName != 17 || !parameter->enabled || parameter->creationTrack) continue;
            distance = imageSpeedDistance(seq, parameter, seq->headPosition - elapsed, seq->headPosition);
        }
    }
    return distance * spriteSpeed / speed;
}

static uint32_t packedColor(const float* color) {
    uint32_t result = 0;
    repeat(3, c) result |= (uint32_t)(GMLReal_fmin(1.0f, GMLReal_fmax(0.0f, color[c])) * 255.0f) << (c * 8);
    return result;
}

static void buildMatrix(Matrix4f* matrix, const Matrix4f* parent, const SequenceValues* value) {
    *matrix = *parent;
    Matrix4f_translate(matrix, value->x, value->y, 0.0f);
    Matrix4f_rotateZ(matrix, -value->rotation * (float)(M_PI / 180.0));
    Matrix4f_scale(matrix, value->scaleX, value->scaleY, 1.0f);
    Matrix4f_translate(matrix, -value->originX, -value->originY, 0.0f);
}

static void hideNodeInstances(RuntimeSequence* seq, SequenceNode* nodes, uint32_t count) {
    repeat(count, t) {
        SequenceNode* node = &nodes[t];
        hideNodeInstances(seq, node->children, node->track->trackCount);
        repeat(node->stateCount, c) {
            SequenceChannelState* state = &node->states[c];
            if (state->instanceId >= 0) {
                Instance* inst = hmget(seq->runner->instancesById, state->instanceId);
                if (inst != nullptr && !inst->destroyed) inst->visible = false;
            }
            if (state->nested != nullptr) {
                hideNodeInstances(state->nested, state->nested->nodes, state->nested->asset->trackCount);
            }
        }
    }
}

static void evaluateNodes(RuntimeSequence* seq, SequenceNode* nodes, uint32_t count, const Matrix4f* parent, const float* parentColor, float previous, SequenceEvent** events, bool update, bool parentVisible) {
    Runner* runner = seq->runner;
    float head = seq->headPosition;
    repeat(count, t) {
        if (seq->root->destroyed) return;
        SequenceNode* node = &nodes[t];
        const SequenceTrack* track = node->track;
        if (isParameter(track) || !track->enabled) continue;
        node->visible = parentVisible && track->visible;
        node->values = evaluateValues(seq, track, head);
        repeat(4, c) node->values.color[c] *= parentColor[c];
        int32_t selected = activeKey(track, head, seq->asset->length);
        node->activeKey = selected;
        const SequenceKeyframe* key = selected >= 0 ? &track->keyframes[selected] : nullptr;
        const SequenceChannel* channel = key != nullptr && key->channelCount > 0 ? &key->channels[0] : nullptr;
        if (track->type == SEQUENCE_TRACK_GRAPHIC && channel != nullptr && channel->integer >= 0 && (uint32_t)channel->integer < runner->dataWin->sprt.count) {
            const Sprite* sprite = &runner->dataWin->sprt.sprites[channel->integer];
            node->values.originX += (float)sprite->originX;
            node->values.originY += (float)sprite->originY;
        }
        buildMatrix(&node->matrix, parent, &node->values);
        uint32_t stateIndex = 0;
        for (uint32_t k = 0; k < track->keyframeCount; k++) {
            const SequenceKeyframe* current = &track->keyframes[k];
            for (uint32_t c = 0; c < current->channelCount; c++) {
                SequenceChannelState* state = &node->states[stateIndex++];
                const SequenceChannel* data = &current->channels[c];
                bool active = (int32_t)k == selected;
                if (track->type == SEQUENCE_TRACK_INSTANCE && state->instanceId >= 0) {
                    Instance* inst = hmget(runner->instancesById, state->instanceId);
                    if (inst == nullptr || inst->destroyed) continue;
                    inst->visible = active && node->visible;
                    inst->sequenceControlled = true;
                    if (!active) continue;
                    Matrix4f_transformPoint(&node->matrix, node->values.originX, node->values.originY, &inst->x, &inst->y);
                    inst->imageAngle = -GMLReal_atan2(node->matrix.m[1], node->matrix.m[0]) * (180.0 / M_PI);
                    inst->imageXscale = sqrtf(node->matrix.m[0] * node->matrix.m[0] + node->matrix.m[1] * node->matrix.m[1]);
                    float determinant = node->matrix.m[0]*node->matrix.m[5] - node->matrix.m[1]*node->matrix.m[4];
                    inst->imageYscale = inst->imageXscale != 0.0f ? determinant / inst->imageXscale : 0.0f;
                    inst->imageBlend = packedColor(node->values.color);
                    inst->imageAlpha = node->values.color[3];
                    if (node->values.hasImageIndex || node->values.hasImageSpeed) inst->imageIndex = spriteFrame(seq, node, inst->spriteIndex, head - current->key);
                    SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
                } else if (track->type == SEQUENCE_TRACK_AUDIO && update && runner->audioSystem != nullptr) {
                    AudioSystem* audio = runner->audioSystem;
                    active = active && node->visible;
                    bool finished = seq->finished || seq->root->finished;
                    bool restarted = state->soundDirection != seq->direction || (head - previous) * state->soundDirection < 0.0f;
                    if ((!active || finished || restarted) && state->soundId >= 0) {
                        audio->vtable->stopSound(audio, state->soundId);
                        state->soundId = -1;
                    }
                    if (active && !finished && !seq->paused) {
                        if (state->soundId < 0) {
                            state->soundId = audio->vtable->playSound(audio, data->integer, 1, data->audioMode == 1);
                            state->soundDirection = seq->direction;
                            float speed = speedPerSecond(seq->asset, (float)Runner_getEffectiveGameSpeed(runner));
                            if (state->soundId >= 0 && speed > 0.0f && head > current->key) audio->vtable->setTrackPosition(audio, state->soundId, (head - current->key) / speed);
                        }
                        if (state->soundId >= 0) {
                            audio->vtable->setSoundGain(audio, state->soundId, node->values.gain * seq->asset->volume, 0);
                            audio->vtable->setSoundPitch(audio, state->soundId, node->values.pitch);
                        }
                    }
                } else if (state->nested != nullptr) {
                    RuntimeSequence* nested = state->nested;
                    if (active) {
                        float fps = (float)Runner_getEffectiveGameSpeed(runner);
                        float parentSpeed = speedPerSecond(seq->asset, fps);
                        float ratio = parentSpeed != 0.0f ? speedPerSecond(nested->asset, fps) / parentSpeed : 0.0f;
                        float old = nested->headPosition;
                        nested->headPosition = (head - current->key) * ratio;
                        Sequence_setPaused(nested, seq->paused);
                        nested->direction = seq->direction;
                        nested->finished = false;
                        wrapSequenceHead(nested);
                        evaluateSequence(nested, &node->matrix, node->values.color, old, events, update, node->visible);
                    } else if (update && nested->nodes != nullptr) {
                        Sequence_setPaused(nested, true);
                        hideNodeInstances(nested, nested->nodes, nested->asset->trackCount);
                    }
                }
            }
        }
        Matrix4f childrenMatrix = node->matrix;
        if ((track->traits & 1u) == 0) Matrix4f_translate(&childrenMatrix, node->values.originX, node->values.originY, 0.0f);
        evaluateNodes(seq, node->children, track->trackCount, &childrenMatrix, node->values.color, previous, events, update, node->visible);
    }
}

static void queueEvents(RuntimeSequence* seq, SequenceKeyframe* keys, uint32_t count, float previous, bool moment, SequenceEvent** events) {
    bool forward = seq->asset->playbackSpeed * seq->speedScale * seq->direction >= 0.0f;
    bool wrapped = forward ? seq->headPosition < previous : seq->headPosition > previous;
    uint32_t passes = wrapped ? 2 : 1;
    repeat(passes, pass) {
        repeat(count, i) {
            uint32_t k = forward ? i : count - i - 1;
            SequenceKeyframe* key = &keys[k];
            bool afterStart = forward ? key->key >= previous : key->key <= previous;
            bool beforeEnd = forward ? key->key < seq->headPosition : key->key > seq->headPosition;
            bool crossed = afterStart && beforeEnd;
            if (wrapped) crossed = pass == 0 ? afterStart : beforeEnd;
            if (key->disabled || !crossed) continue;
            repeat(key->channelCount, c) {
                SequenceChannel* channel = &key->channels[c];
                repeat(channel->messageCount, m) {
                    SequenceEvent event = {seq->elementId, channel->messages[m], moment};
                    arrput(*events, event);
                }
            }
        }
    }
}

static void evaluateSequence(RuntimeSequence* seq, const Matrix4f* parent, const float* color, float previous, SequenceEvent** events, bool update, bool visible) {
    initializeSequence(seq);
    if (seq->root->destroyed) return;
    Matrix4f matrix = *parent;
    Matrix4f_translate(&matrix, -seq->asset->originX, -seq->asset->originY, 0.0f);
    evaluateNodes(seq, seq->nodes, seq->asset->trackCount, &matrix, color, previous, events, update, visible);
    if (update && !seq->paused && !seq->root->destroyed) {
        queueEvents(seq, seq->asset->messages, seq->asset->messageCount, previous, false, events);
        queueEvents(seq, seq->asset->moments, seq->asset->momentCount, previous, true, events);
    }
}

static void evaluateRoot(RuntimeSequence* seq, float previous, SequenceEvent** events, bool update) {
    RuntimeLayer* layer = Runner_findRuntimeLayerById(seq->runner, seq->layerId);
    if (layer == nullptr) return;
    Matrix4f matrix;
    Matrix4f_identity(&matrix);
    Matrix4f_translate(&matrix, seq->x + layer->xOffset, seq->y + layer->yOffset, 0.0f);
    Matrix4f_rotateZ(&matrix, -seq->angle * (float)(M_PI / 180.0));
    Matrix4f_scale(&matrix, seq->scaleX, seq->scaleY, 1.0f);
    float color[4] = {
        (float)BGR_R(seq->blend) / 255.0f,
        (float)BGR_G(seq->blend) / 255.0f,
        (float)BGR_B(seq->blend) / 255.0f,
        seq->alpha
    };
    evaluateSequence(seq, &matrix, color, previous, events, update, true);
    seq->transformDirty = false;
}

void Sequence_stepRoom(Runner* runner) {
    RuntimeSequence** snapshot = runner->sequenceSnapshots;
    arrsetlen(snapshot, 0);
    repeat(arrlenu(runner->runtimeLayers), l) {
        RuntimeLayer* layer = &runner->runtimeLayers[l];
        repeat(arrlenu(layer->elements), e) {
            RuntimeSequence* seq = layer->elements[e].sequenceElement;
            if (seq == nullptr) continue;
            seq->references++;
            arrput(snapshot, seq);
        }
    }
    SequenceEvent* events = runner->sequenceEvents;
    arrsetlen(events, 0);
    repeat(arrlenu(snapshot), s) {
        RuntimeSequence* seq = snapshot[s];
        if (seq->destroyed) continue;
        initializeSequence(seq);
        if (seq->destroyed || runner->pendingRoom >= 0) continue;
        float previous = seq->headPosition;
        if (!seq->paused && !seq->finished) {
            float fps = (float)Runner_getEffectiveGameSpeed(runner);
            float advance = fps > 0.0f ? speedPerSecond(seq->asset, fps) / fps : 0.0f;
            seq->headPosition += advance * seq->speedScale * seq->direction;
            wrapSequenceHead(seq);
        }
        evaluateRoot(seq, previous, &events, !seq->paused);
    }
    repeat(arrlenu(events), e) {
        if (runner->pendingRoom >= 0) break;
        SequenceEvent* event = &events[e];
        if (Sequence_find(runner, event->elementId) == nullptr) continue;
        if (!event->moment) {
            Runner_broadcastMessage(runner, "sequence event", event->message, event->elementId);
        } else {
            VMContext* ctx = runner->vmContext;
            ptrdiff_t index = shgeti(ctx->codeIndexByName, event->message);
            if (index >= 0) {
                RValue result = VM_executeCode(ctx, ctx->codeIndexByName[index].value, ctx->globalScopeInstance, nullptr, CODE_NOT_RELATED_TO_EVENTS, EVENT_OTHER, OTHER_BROADCAST_MESSAGE, nullptr, 0);
                RValue_free(&result);
            }
        }
        if (runner->pendingRoom >= 0) break;
    }
    repeat(arrlenu(snapshot), releaseIndex) Sequence_release(snapshot[releaseIndex]);
    arrsetlen(snapshot, 0);
    arrsetlen(events, 0);
    runner->sequenceSnapshots = snapshot;
    runner->sequenceEvents = events;
}

static void drawSequenceText(Renderer* renderer, const SequenceChannel* channel, SequenceChannelState* state, const SequenceValues* values) {
    if (channel->fontIndex < 0 || (uint32_t)channel->fontIndex >= renderer->dataWin->font.count) return;
    Font* font = &renderer->dataWin->font.fonts[channel->fontIndex];
    int32_t savedFont = renderer->drawFont;
    int32_t savedHalign = renderer->drawHalign, savedValign = renderer->drawValign;
    renderer->drawFont = channel->fontIndex;
    renderer->drawHalign = renderer->drawValign = 0;
    int32_t wrapWidth = -1;
    if (channel->wrap && values->frameWidth > 0.0f) wrapWidth = (int32_t)values->frameWidth;
    if (!state->textPrepared || state->wrapWidth != wrapWidth) {
        PreprocessedText_free(state->text);
        ZERO_STRUCT(state->text);
        if (wrapWidth >= 0) state->text = TextUtils_wrapText(font, channel->text, wrapWidth);
        else state->text.text = channel->text;
        state->wrapWidth = wrapWidth;
        state->textPrepared = true;
    }
    const char* str = state->text.text;
    int32_t length = (int32_t)strlen(str);
    int32_t start = 0;
    float y = 0.0f;
    int32_t lineCount = length > 0 ? 1 : 0;
    for (int32_t pos = 0; pos < length;) {
        if (str[pos] == '\r' || str[pos] == '\n') {
            lineCount++;
            pos = TextUtils_skipNewline(str, pos, length);
        } else pos++;
    }
    float stride = TextUtils_lineStride(font) + values->lineSpacing + values->paragraphSpacing;
    float height = lineCount > 0 ? TextUtils_lineStride(font) + (lineCount - 1) * stride : 0.0f;
    int32_t valign = (channel->alignment >> 8) & 255;
    if (valign == 1) y = (values->frameHeight - height) * 0.5f;
    else if (valign == 2) y = values->frameHeight - height;
    uint32_t color = packedColor(values->color);
    if (values->characterSpacing == 0.0f) {
        int32_t align = channel->alignment & 255;
        float x = 0.0f;
        if (align == 1) x = values->frameWidth * 0.5f;
        else if (align == 2) x = values->frameWidth;
        renderer->drawHalign = align;
        renderer->vtable->drawTextColor(renderer, str, x, y, 1.0f, 1.0f, 0.0f, color, color, color, color, values->color[3], stride * font->scaleY);
    }
    while (values->characterSpacing != 0.0f && start < length) {
        int32_t end = start;
        while (end < length && str[end] != '\r' && str[end] != '\n') end++;
        float width = TextUtils_measureLineWidth(font, str + start, end - start);
        width += values->characterSpacing * (float)GMLReal_fmax(0.0f, (float)TextUtils_utf8CodepointCount(str + start, end - start) - 1.0f);
        int32_t align = channel->alignment & 255;
        float x = align == 1 ? (values->frameWidth - width) * 0.5f : align == 2 ? values->frameWidth - width : 0.0f;
        int32_t pos = start;
        while (pos < end) {
            int32_t before = pos;
            uint16_t ch = TextUtils_decodeUtf8(str, end, &pos);
            char glyphText[5] = {0};
            memcpy(glyphText, str + before, (size_t)(pos - before));
            renderer->vtable->drawTextColor(renderer, glyphText, x, y, 1.0f, 1.0f, 0.0f, color, color, color, color, values->color[3], -1.0f);
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);
            if (glyph != nullptr) {
                x += glyph->shift + values->characterSpacing;
                if (pos < end) {
                    int32_t nextPos = pos;
                    uint16_t next = TextUtils_decodeUtf8(str, end, &nextPos);
                    x += TextUtils_getKerningOffset(glyph, next);
                }
            }
        }
        y += stride;
        start = end < length ? TextUtils_skipNewline(str, end, length) : end;
    }
    renderer->drawFont = savedFont;
    renderer->drawHalign = savedHalign;
    renderer->drawValign = savedValign;
}

static void drawNodes(RuntimeSequence* seq, SequenceNode* nodes, uint32_t count, const Matrix4f* world) {
    Renderer* renderer = seq->runner->renderer;
    for (uint32_t t = count; t > 0; t--) {
        if (seq->root->destroyed) return;
        SequenceNode* node = &nodes[t - 1];
        const SequenceTrack* track = node->track;
        if (isParameter(track) || !track->enabled || !node->visible) continue;
        Matrix4f matrix;
        Matrix4f_multiply(&matrix, world, &node->matrix);
        if (node->activeKey >= 0) {
            const SequenceKeyframe* key = &track->keyframes[node->activeKey];
            uint32_t stateIndex = 0;
            for (int32_t k = 0; k < node->activeKey; k++) stateIndex += track->keyframes[k].channelCount;
            for (uint32_t c = 0; c < key->channelCount; c++) {
                const SequenceChannel* channel = &key->channels[c];
                SequenceChannelState* state = &node->states[stateIndex + c];
                if (track->type == SEQUENCE_TRACK_INSTANCE) {
                    Instance* inst = hmget(seq->runner->instancesById, state->instanceId);
                    if (inst != nullptr && inst->active && inst->visible && !inst->destroyed && inst->sequenceControlled) {
                        renderer->vtable->setMatrix(renderer, MATRIX_WORLD, *world);
                        Runner_drawInstance(seq->runner, inst);
                    }
                } else if (state->nested != nullptr) {
                    drawNodes(state->nested, state->nested->nodes, state->nested->asset->trackCount, world);
                } else if (track->type == SEQUENCE_TRACK_GRAPHIC) {
                    renderer->vtable->setMatrix(renderer, MATRIX_WORLD, matrix);
                    int32_t subimg = (int32_t)spriteFrame(seq, node, channel->integer, seq->headPosition - key->key);
                    int32_t tpag = Renderer_resolveTPAGIndex(renderer->dataWin, channel->integer, subimg);
                    if (tpag >= 0) renderer->vtable->drawSprite(renderer, tpag, 0, 0, 0, 0, 1, 1, 0, packedColor(node->values.color), node->values.color[3]);
                } else if (track->type == SEQUENCE_TRACK_TEXT) {
                    renderer->vtable->setMatrix(renderer, MATRIX_WORLD, matrix);
                    drawSequenceText(renderer, channel, state, &node->values);
                }
            }
        }
        drawNodes(seq, node->children, track->trackCount, world);
    }
}

void Sequence_draw(RuntimeSequence* seq) {
    if (seq == nullptr || seq->destroyed || seq->nodes == nullptr || seq->runner->renderer == nullptr) return;
    seq->references++;
    Renderer* renderer = seq->runner->renderer;
    Matrix4f world = renderer->gmlMatrices[MATRIX_WORLD];
    if (seq->transformDirty) evaluateRoot(seq, seq->headPosition, nullptr, false);
    drawNodes(seq, seq->nodes, seq->asset->trackCount, &world);
    renderer->vtable->setMatrix(renderer, MATRIX_WORLD, world);
    Sequence_release(seq);
}
