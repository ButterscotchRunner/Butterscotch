#ifndef _BS_SEQUENCE_H_
#define _BS_SEQUENCE_H_

#include "common.h"
#include "data_win.h"

typedef struct SequenceNode SequenceNode;
typedef struct SequenceEvent SequenceEvent;
struct Runner;
typedef struct RuntimeSequence {
    struct Runner* runner;
    struct RuntimeSequence* root;
    Sequence* asset;
    int32_t assetIndex;
    int32_t elementId;
    int32_t layerId;
    float x, y;
    float scaleX, scaleY, angle;
    uint32_t blend;
    float alpha;
    float headPosition;
    float direction;
    float speedScale;
    bool paused, finished, initialized, destroyed;
    bool transformDirty;
    uint32_t references;
    SequenceNode* nodes;
} RuntimeSequence;

float AnimCurveChannel_evaluate(const AnimCurveChannel* channel, float x);
int32_t Sequence_create(struct Runner* runner, int32_t layerId, int32_t assetIndex, float x, float y);
RuntimeSequence* Sequence_find(struct Runner* runner, int32_t elementId);
void Sequence_destroy(struct Runner* runner, int32_t elementId);
void Sequence_release(RuntimeSequence* sequence);
void Sequence_setPaused(RuntimeSequence* sequence, bool paused);
void Sequence_play(RuntimeSequence* sequence);
void Sequence_stepRoom(struct Runner* runner);
void Sequence_draw(RuntimeSequence* sequence);

#endif
