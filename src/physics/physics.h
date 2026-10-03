#ifndef BS_PHYSICS_H
#define BS_PHYSICS_H
#include "common.h"
#include "rvalue.h"
#include "physics_engine.h"
struct Runner;
struct VMContext;
struct Instance;
#define PHYSICS_VARIABLE_BASE 10000
#define PHYSICS_CONSTANT_BASE 10100
PhysicsEngine* Physics_ensureResources(struct Runner* runner, float scale);
PhysicsEngine* Physics_createWorld(struct Runner* runner, float scale);
int16_t Physics_resolveVariable(const char* name);
RValue Physics_getVariable(struct Runner* runner, struct Instance* inst, int16_t id);
void Physics_setVariable(struct Runner* runner, struct Instance* inst, int16_t id, RValue value);
void Physics_initInstance(struct Runner* runner, struct Instance* inst);
void Physics_initRoom(struct Runner* runner);
void Physics_releaseRoom(struct Runner* runner, int32_t roomIndex);
void Physics_step(struct Runner* runner);
void Physics_free(struct Runner* runner);
#endif
