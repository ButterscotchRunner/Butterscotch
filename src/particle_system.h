#pragma once

#include "real_type.h"
#include <stdint.h>
#include <stdbool.h>

struct Runner;
struct Renderer;

void ParticleSystem_init(struct Runner* runner);
void ParticleSystem_shutdown(struct Runner* runner);
void ParticleSystem_step(struct Runner* runner);
void ParticleSystem_draw(struct Runner* runner);

int32_t ParticleSystem_create(void);
void ParticleSystem_destroy(int32_t systemId);
bool ParticleSystem_exists(int32_t systemId);
void ParticleSystem_setDepth(int32_t systemId, int32_t depth);
void ParticleSystem_clear(int32_t systemId);

int32_t ParticleEmitter_create(int32_t systemId);
void ParticleEmitter_destroy(int32_t systemId, int32_t emitterId);
void ParticleEmitter_setRegion(int32_t systemId, int32_t emitterId, int32_t shape, GMLReal x1, GMLReal y1, GMLReal x2, GMLReal y2, GMLReal x3, GMLReal y3);
void ParticleEmitter_burst(int32_t systemId, int32_t emitterId, int32_t typeId, int32_t count);
void ParticleEmitter_stream(int32_t systemId, int32_t emitterId, int32_t typeId, int32_t rate);

int32_t ParticleType_create(void);
void ParticleType_setSprite(int32_t typeId, int32_t spriteIndex);
void ParticleType_setShape(int32_t typeId, int32_t shape);
void ParticleType_setSize(int32_t typeId, GMLReal minSize, GMLReal maxSize);
void ParticleType_setScale(int32_t typeId, GMLReal minScale, GMLReal maxScale);
void ParticleType_setSpeed(int32_t typeId, GMLReal minSpeed, GMLReal maxSpeed);
void ParticleType_setDirection(int32_t typeId, GMLReal minDir, GMLReal maxDir);
void ParticleType_setGravity(int32_t typeId, GMLReal amount, GMLReal direction);
void ParticleType_setLife(int32_t typeId, GMLReal minLife, GMLReal maxLife);
void ParticleType_setAlpha(int32_t typeId, int32_t slot, GMLReal alpha);
void ParticleType_setColor(int32_t typeId, int32_t slot, uint32_t color);
void ParticleType_setBlend(int32_t typeId, int32_t blend);
void ParticleType_setOrientation(int32_t typeId, GMLReal minAngle, GMLReal maxAngle);

void ParticleSystem_createParticles(int32_t systemId, int32_t typeId, GMLReal x, GMLReal y, int32_t count);

// Used by runner.c to integrate particle systems into the depth-sorted draw list.
// Returns the number of active particle systems.
int32_t ParticleSystem_getActiveCount(void);
// Gets the 0-based array index and depth of the nth active system (n from 0..count-1).
void ParticleSystem_getActiveInfo(int32_t nth, int32_t* outIndex, int32_t* outDepth);
// Draws all particles for the system at the given 0-based array index.
void ParticleSystem_drawByIndex(struct Runner* runner, int32_t systemIndex);
