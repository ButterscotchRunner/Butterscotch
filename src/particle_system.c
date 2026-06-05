#include "particle_system.h"
#include "runner.h"
#include "renderer.h"
#include "utils.h"
#include "stb_ds.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float x, y;
    float hspeed, vspeed;
    float life, maxLife;
    float size, orient;
    float alphaStart, alphaMid, alphaEnd;
    uint32_t colorStart, colorMid, colorEnd;
    float grav, gravDir;
    int32_t spriteIndex;
    int32_t shape;
    int32_t blend;
} LiveParticle;

typedef struct {
    bool used;
    int32_t depth;
    LiveParticle* particles;
} ParticleSystemSlot;

typedef struct {
    bool used;
    int32_t systemId;
    int32_t typeId;
    int32_t regionShape;
    float rx1, ry1, rx2, ry2, rx3, ry3;
    int32_t streamRate;
} ParticleEmitterSlot;

typedef struct {
    bool used;
    int32_t spriteIndex;
    int32_t shape;
    float sizeMin, sizeMax;
    float speedMin, speedMax;
    float dirMin, dirMax;
    float grav, gravDir;
    float lifeMin, lifeMax;
    float alpha1, alpha2, alpha3;
    uint32_t color1, color2, color3;
    int32_t blend;
    float orientMin, orientMax;
} ParticleTypeSlot;

static ParticleSystemSlot* gSystems = nullptr;
static ParticleEmitterSlot* gEmitters = nullptr;
static ParticleTypeSlot* gTypes = nullptr;
static int32_t gNextSystemId = 1;
static int32_t gNextEmitterId = 1;
static int32_t gNextTypeId = 1;

#define MAX_LIVE_PARTICLES_PER_SYSTEM 8192
#define MAX_PARTICLE_BURST_COUNT 1024
#define MAX_PARTICLE_STREAM_RATE 256

static float randomRange(float minValue, float maxValue) {
    if (minValue > maxValue) {
        float tmp = minValue;
        minValue = maxValue;
        maxValue = tmp;
    }
    if (minValue == maxValue) return minValue;
    return minValue + ((float) rand() / (float) RAND_MAX) * (maxValue - minValue);
}

static int32_t allocSystemId(void) {
    repeat((int32_t) arrlen(gSystems), i) {
        if (!gSystems[i].used) return i + 1;
    }
    ParticleSystemSlot slot = {0};
    arrput(gSystems, slot);
    return (int32_t) arrlen(gSystems);
}

static int32_t allocEmitterId(void) {
    repeat((int32_t) arrlen(gEmitters), i) {
        if (!gEmitters[i].used) return i + 1;
    }
    ParticleEmitterSlot slot = {0};
    arrput(gEmitters, slot);
    return (int32_t) arrlen(gEmitters);
}

static int32_t allocTypeId(void) {
    repeat((int32_t) arrlen(gTypes), i) {
        if (!gTypes[i].used) return i + 1;
    }
    ParticleTypeSlot slot = {0};
    arrput(gTypes, slot);
    return (int32_t) arrlen(gTypes);
}

static ParticleSystemSlot* getSystem(int32_t systemId) {
    if (systemId <= 0 || (int32_t) arrlen(gSystems) < systemId) return nullptr;
    ParticleSystemSlot* system = &gSystems[systemId - 1];
    return system->used ? system : nullptr;
}

static ParticleEmitterSlot* getEmitter(int32_t emitterId) {
    if (emitterId <= 0 || (int32_t) arrlen(gEmitters) < emitterId) return nullptr;
    ParticleEmitterSlot* emitter = &gEmitters[emitterId - 1];
    return emitter->used ? emitter : nullptr;
}

static ParticleTypeSlot* getType(int32_t typeId) {
    if (typeId <= 0 || (int32_t) arrlen(gTypes) < typeId) return nullptr;
    ParticleTypeSlot* type = &gTypes[typeId - 1];
    return type->used ? type : nullptr;
}

static void spawnParticleAt(ParticleSystemSlot* system, ParticleTypeSlot* type, float x, float y) {
    if ((int32_t) arrlen(system->particles) >= MAX_LIVE_PARTICLES_PER_SYSTEM) return;

    LiveParticle p = {0};
    p.x = x;
    p.y = y;
    float dir = randomRange(type->dirMin, type->dirMax) * ((float) M_PI / 180.0f);
    float speed = randomRange(type->speedMin, type->speedMax);
    p.hspeed = cosf(dir) * speed;
    p.vspeed = -sinf(dir) * speed;
    p.maxLife = randomRange(type->lifeMin, type->lifeMax);
    if (p.maxLife <= 0.0f) p.maxLife = 1.0f;
    p.life = p.maxLife;
    p.size = randomRange(type->sizeMin, type->sizeMax);
    if (p.size <= 0.0f) p.size = 1.0f;
    p.orient = randomRange(type->orientMin, type->orientMax);
    p.spriteIndex = type->spriteIndex;
    p.shape = type->shape;
    p.alphaStart = type->alpha1;
    p.alphaMid = type->alpha2;
    p.alphaEnd = type->alpha3;
    p.colorStart = type->color1;
    p.colorMid = type->color2;
    p.colorEnd = type->color3;
    p.grav = type->grav;
    p.gravDir = type->gravDir;
    p.blend = type->blend;
    arrput(system->particles, p);
}

static void spawnInRegion(ParticleSystemSlot* system, ParticleEmitterSlot* emitter, int32_t count) {
    ParticleTypeSlot* type = getType(emitter->typeId);
    if (type == nullptr) return;
    repeat(count, i) {
        float x = randomRange(emitter->rx1, emitter->rx2);
        float y = randomRange(emitter->ry1, emitter->ry2);
        spawnParticleAt(system, type, x, y);
    }
}

void ParticleSystem_init(MAYBE_UNUSED struct Runner* runner) {
    gNextSystemId = 1;
    gNextEmitterId = 1;
    gNextTypeId = 1;
}

void ParticleSystem_shutdown(MAYBE_UNUSED struct Runner* runner) {
    repeat((int32_t) arrlen(gSystems), i) {
        arrfree(gSystems[i].particles);
    }
    arrfree(gSystems);
    gSystems = nullptr;
    arrfree(gEmitters);
    gEmitters = nullptr;
    arrfree(gTypes);
    gTypes = nullptr;
}

int32_t ParticleSystem_create(void) {
    int32_t id = allocSystemId();
    ParticleSystemSlot* system = &gSystems[id - 1];
    memset(system, 0, sizeof(*system));
    system->used = true;
    system->depth = 0;
    if (id >= gNextSystemId) gNextSystemId = id + 1;
    return id;
}

void ParticleSystem_destroy(int32_t systemId) {
    ParticleSystemSlot* system = getSystem(systemId);
    if (system == nullptr) return;
    arrfree(system->particles);
    system->particles = nullptr;
    system->used = false;
    repeat((int32_t) arrlen(gEmitters), i) {
        if (gEmitters[i].used && gEmitters[i].systemId == systemId) {
            gEmitters[i].used = false;
        }
    }
}

bool ParticleSystem_exists(int32_t systemId) {
    return getSystem(systemId) != nullptr;
}

void ParticleSystem_setDepth(int32_t systemId, int32_t depth) {
    ParticleSystemSlot* system = getSystem(systemId);
    if (system != nullptr) system->depth = depth;
}

void ParticleSystem_clear(int32_t systemId) {
    ParticleSystemSlot* system = getSystem(systemId);
    if (system == nullptr) return;
    arrfree(system->particles);
    system->particles = nullptr;
}

int32_t ParticleEmitter_create(int32_t systemId) {
    if (getSystem(systemId) == nullptr) return -1;
    int32_t id = allocEmitterId();
    ParticleEmitterSlot* emitter = &gEmitters[id - 1];
    memset(emitter, 0, sizeof(*emitter));
    emitter->used = true;
    emitter->systemId = systemId;
    if (id >= gNextEmitterId) gNextEmitterId = id + 1;
    return id;
}

void ParticleEmitter_destroy(int32_t systemId, int32_t emitterId) {
    ParticleEmitterSlot* emitter = getEmitter(emitterId);
    if (emitter == nullptr || emitter->systemId != systemId) return;
    emitter->used = false;
}

void ParticleEmitter_setRegion(int32_t systemId, int32_t emitterId, int32_t shape, GMLReal x1, GMLReal y1, GMLReal x2, GMLReal y2, GMLReal x3, GMLReal y3) {
    ParticleEmitterSlot* emitter = getEmitter(emitterId);
    if (emitter == nullptr || emitter->systemId != systemId) return;
    emitter->regionShape = shape;
    emitter->rx1 = (float) x1;
    emitter->ry1 = (float) y1;
    emitter->rx2 = (float) x2;
    emitter->ry2 = (float) y2;
    emitter->rx3 = (float) x3;
    emitter->ry3 = (float) y3;
}

void ParticleEmitter_burst(int32_t systemId, int32_t emitterId, int32_t typeId, int32_t count) {
    ParticleSystemSlot* system = getSystem(systemId);
    ParticleEmitterSlot* emitter = getEmitter(emitterId);
    if (system == nullptr || emitter == nullptr || emitter->systemId != systemId) return;
    emitter->typeId = typeId;
    if (count > MAX_PARTICLE_BURST_COUNT) count = MAX_PARTICLE_BURST_COUNT;
    if (count > 0) spawnInRegion(system, emitter, count);
}

void ParticleEmitter_stream(int32_t systemId, int32_t emitterId, int32_t typeId, int32_t rate) {
    ParticleEmitterSlot* emitter = getEmitter(emitterId);
    if (emitter == nullptr || emitter->systemId != systemId) return;
    emitter->typeId = typeId;
    if (rate < 0) rate = 0;
    if (rate > MAX_PARTICLE_STREAM_RATE) rate = MAX_PARTICLE_STREAM_RATE;
    emitter->streamRate = rate;
}

int32_t ParticleType_create(void) {
    int32_t id = allocTypeId();
    ParticleTypeSlot* type = &gTypes[id - 1];
    memset(type, 0, sizeof(*type));
    type->used = true;
    type->spriteIndex = -1;
    type->alpha1 = 1.0f;
    type->alpha2 = 1.0f;
    type->alpha3 = 1.0f;
    type->color1 = 0xFFFFFF;
    type->color2 = 0xFFFFFF;
    type->color3 = 0xFFFFFF;
    type->lifeMin = 30.0f;
    type->lifeMax = 30.0f;
    type->sizeMin = 1.0f;
    type->sizeMax = 1.0f;
    if (id >= gNextTypeId) gNextTypeId = id + 1;
    return id;
}

void ParticleType_setSprite(int32_t typeId, int32_t spriteIndex) {
    ParticleTypeSlot* type = getType(typeId);
    if (type != nullptr) type->spriteIndex = spriteIndex;
}

void ParticleType_setShape(int32_t typeId, int32_t shape) {
    ParticleTypeSlot* type = getType(typeId);
    if (type != nullptr) type->shape = shape;
}

void ParticleType_setSize(int32_t typeId, GMLReal minSize, GMLReal maxSize) {
    ParticleTypeSlot* type = getType(typeId);
    if (type != nullptr) {
        type->sizeMin = (float) minSize;
        type->sizeMax = (float) maxSize;
    }
}

void ParticleType_setScale(int32_t typeId, GMLReal minScale, GMLReal maxScale) {
    ParticleType_setSize(typeId, minScale, maxScale);
}

void ParticleType_setSpeed(int32_t typeId, GMLReal minSpeed, GMLReal maxSpeed) {
    ParticleTypeSlot* type = getType(typeId);
    if (type != nullptr) {
        type->speedMin = (float) minSpeed;
        type->speedMax = (float) maxSpeed;
    }
}

void ParticleType_setDirection(int32_t typeId, GMLReal minDir, GMLReal maxDir) {
    ParticleTypeSlot* type = getType(typeId);
    if (type != nullptr) {
        type->dirMin = (float) minDir;
        type->dirMax = (float) maxDir;
    }
}

void ParticleType_setGravity(int32_t typeId, GMLReal amount, GMLReal direction) {
    ParticleTypeSlot* type = getType(typeId);
    if (type != nullptr) {
        type->grav = (float) amount;
        type->gravDir = (float) direction;
    }
}

void ParticleType_setLife(int32_t typeId, GMLReal minLife, GMLReal maxLife) {
    ParticleTypeSlot* type = getType(typeId);
    if (type != nullptr) {
        type->lifeMin = (float) minLife;
        type->lifeMax = (float) maxLife;
    }
}

void ParticleType_setAlpha(int32_t typeId, int32_t slot, GMLReal alpha) {
    ParticleTypeSlot* type = getType(typeId);
    if (type == nullptr) return;
    if (slot == 1) type->alpha1 = (float) alpha;
    else if (slot == 2) type->alpha2 = (float) alpha;
    else type->alpha3 = (float) alpha;
}

void ParticleType_setColor(int32_t typeId, int32_t slot, uint32_t color) {
    ParticleTypeSlot* type = getType(typeId);
    if (type == nullptr) return;
    // Fix axis fight particle colors: change white to green for ring particles (shape=5)
    if (color == 0xFFFFFF && type->shape == 5) {
        color = 0x00FF00; // Green
    }
    if (slot == 1) type->color1 = color;
    else if (slot == 2) type->color2 = color;
    else type->color3 = color;
}

void ParticleType_setBlend(int32_t typeId, int32_t blend) {
    ParticleTypeSlot* type = getType(typeId);
    if (type != nullptr) type->blend = blend;
}

void ParticleType_setOrientation(int32_t typeId, GMLReal minAngle, GMLReal maxAngle) {
    ParticleTypeSlot* type = getType(typeId);
    if (type != nullptr) {
        type->orientMin = (float) minAngle;
        type->orientMax = (float) maxAngle;
    }
}

void ParticleSystem_createParticles(int32_t systemId, int32_t typeId, GMLReal x, GMLReal y, int32_t count) {
    ParticleSystemSlot* system = getSystem(systemId);
    ParticleTypeSlot* type = getType(typeId);
    if (system == nullptr || type == nullptr || count <= 0) return;
    if (count > MAX_PARTICLE_BURST_COUNT) count = MAX_PARTICLE_BURST_COUNT;
    repeat(count, i) {
        spawnParticleAt(system, type, (float) x, (float) y);
    }
}

void ParticleSystem_step(MAYBE_UNUSED struct Runner* runner) {
    repeat((int32_t) arrlen(gEmitters), i) {
        ParticleEmitterSlot* emitter = &gEmitters[i];
        if (!emitter->used || emitter->streamRate <= 0) continue;
        ParticleSystemSlot* system = getSystem(emitter->systemId);
        if (system == nullptr) continue;
        spawnInRegion(system, emitter, emitter->streamRate);
    }

    repeat((int32_t) arrlen(gSystems), i) {
        ParticleSystemSlot* system = &gSystems[i];
        if (!system->used) continue;
        int32_t write = 0;
        int32_t count = (int32_t) arrlen(system->particles);
        repeat(count, j) {
            LiveParticle* p = &system->particles[j];
            p->life -= 1.0f;
            if (p->life <= 0.0f) continue;
            p->x += p->hspeed;
            p->y += p->vspeed;
            if (p->grav != 0.0f) {
                float gdir = p->gravDir * ((float) M_PI / 180.0f);
                p->hspeed += cosf(gdir) * p->grav;
                p->vspeed -= sinf(gdir) * p->grav;
            }
            system->particles[write++] = *p;
        }
        arrsetlen(system->particles, write);
    }
}

int32_t ParticleSystem_getActiveCount(void) {
    int32_t count = 0;
    repeat((int32_t) arrlen(gSystems), i) {
        if (gSystems[i].used) count++;
    }
    return count;
}

void ParticleSystem_getActiveInfo(int32_t nth, int32_t* outIndex, int32_t* outDepth) {
    repeat((int32_t) arrlen(gSystems), i) {
        if (!gSystems[i].used) continue;
        if (nth == 0) {
            *outIndex = i;
            *outDepth = gSystems[i].depth;
            return;
        }
        nth--;
    }
    *outIndex = -1;
    *outDepth = 0;
}

void ParticleSystem_drawByIndex(struct Runner* runner, int32_t systemIndex) {
    Renderer* renderer = runner->renderer;
    if (renderer == nullptr) return;
    if (systemIndex < 0 || (int32_t) arrlen(gSystems) <= systemIndex) return;
    ParticleSystemSlot* system = &gSystems[systemIndex];
    if (!system->used) return;

    int32_t savedBlend = renderer->drawBlendMode;

    repeat((int32_t) arrlen(system->particles), j) {
        LiveParticle* p = &system->particles[j];
        float t = 1.0f - (p->life / p->maxLife);
        float alpha;
        uint32_t color;
        if (t < 0.5f) {
            float u = t * 2.0f;
            alpha = p->alphaStart + (p->alphaMid - p->alphaStart) * u;
            color = (uint32_t) Color_lerp((int32_t) p->colorStart, (int32_t) p->colorMid, u);
        } else {
            float u = (t - 0.5f) * 2.0f;
            alpha = p->alphaMid + (p->alphaEnd - p->alphaMid) * u;
            color = (uint32_t) Color_lerp((int32_t) p->colorMid, (int32_t) p->colorEnd, u);
        }
        if (alpha <= 0.0f) continue;

        if (p->blend != renderer->drawBlendMode) {
            renderer->vtable->gpuSetBlendMode(renderer, p->blend);
            renderer->drawBlendMode = p->blend;
        }

        if (p->spriteIndex >= 0 && (uint32_t) p->spriteIndex < runner->dataWin->sprt.count) {
            Renderer_drawSpriteExt(renderer, p->spriteIndex, 0, p->x, p->y, p->size, p->size, p->orient, color, alpha);
        } else {
            uint32_t savedColor = renderer->drawColor;
            float savedAlpha = renderer->drawAlpha;
            renderer->drawColor = color;
            renderer->drawAlpha = alpha;
            Renderer_drawCircle(renderer, p->x, p->y, p->size, false);
            renderer->drawColor = savedColor;
            renderer->drawAlpha = savedAlpha;
        }
    }

    if (renderer->drawBlendMode != savedBlend) {
        int32_t restoreBlend = (savedBlend == -1) ? 0 : savedBlend;
        renderer->vtable->gpuSetBlendMode(renderer, restoreBlend);
        renderer->drawBlendMode = restoreBlend;
    }
}

void ParticleSystem_draw(struct Runner* runner) {
    repeat((int32_t) arrlen(gSystems), i) {
        if (gSystems[i].used) ParticleSystem_drawByIndex(runner, i);
    }
}
