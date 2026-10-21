/* Stand-in for the Box2D-backed physics engine in builds configured without physics.
 * The rest of the runtime only talks to physics_engine.h, so providing this file keeps
 * every phy_* builtin linked and reading safe defaults. */
#include "common.h"
#include "physics/physics_engine.h"
#include <stddef.h>

PhysicsResources* PhysicsResources_create(void) {
    return nullptr;
}

void PhysicsResources_free(MAYBE_UNUSED PhysicsResources* resources) {}

double PhysicsResources_call(MAYBE_UNUSED PhysicsResources* resources, MAYBE_UNUSED float scale, MAYBE_UNUSED const char* name, MAYBE_UNUSED const double* args, MAYBE_UNUSED int count) {
    return 0.0;
}

PhysicsEngine* PhysicsEngine_create(MAYBE_UNUSED void* owner, MAYBE_UNUSED PhysicsCallbacks callbacks, MAYBE_UNUSED float scale, MAYBE_UNUSED float fps, MAYBE_UNUSED PhysicsResources* resources) {
    return nullptr;
}

void PhysicsEngine_free(MAYBE_UNUSED PhysicsEngine* engine) {}

void PhysicsEngine_step(MAYBE_UNUSED PhysicsEngine* engine, MAYBE_UNUSED float fps) {}

PhysicsBody* PhysicsEngine_body(MAYBE_UNUSED PhysicsEngine* engine, MAYBE_UNUSED void* instance, MAYBE_UNUSED PhysicsBody* existing, MAYBE_UNUSED int fixture, MAYBE_UNUSED float x, MAYBE_UNUSED float y, MAYBE_UNUSED float angle, MAYBE_UNUSED float xo, MAYBE_UNUSED float yo, MAYBE_UNUSED int visualOffset) {
    return nullptr;
}

void PhysicsEngine_destroyBody(MAYBE_UNUSED PhysicsBody* body) {}

void PhysicsEngine_pathPosition(MAYBE_UNUSED PhysicsBody* body, MAYBE_UNUSED float x, MAYBE_UNUSED float y) {}

double PhysicsEngine_variable(MAYBE_UNUSED PhysicsBody* body, MAYBE_UNUSED int field, MAYBE_UNUSED double value, MAYBE_UNUSED int write, MAYBE_UNUSED float fps) {
    return 0.0;
}

double PhysicsEngine_call(MAYBE_UNUSED PhysicsEngine* engine, MAYBE_UNUSED const char* name, MAYBE_UNUSED PhysicsBody* a, MAYBE_UNUSED PhysicsBody* b, MAYBE_UNUSED const double* args, MAYBE_UNUSED int count) {
    return 0.0;
}

int PhysicsEngine_overlap(MAYBE_UNUSED PhysicsBody* a, MAYBE_UNUSED PhysicsBody* b, MAYBE_UNUSED float x, MAYBE_UNUSED float y, MAYBE_UNUSED float angle) {
    return 0;
}

int PhysicsEngine_raycast(MAYBE_UNUSED PhysicsBody* body, MAYBE_UNUSED float x1, MAYBE_UNUSED float y1, MAYBE_UNUSED float x2, MAYBE_UNUSED float y2, MAYBE_UNUSED float fraction, MAYBE_UNUSED float* result) {
    return 0;
}

int PhysicsEngine_particleData(MAYBE_UNUSED PhysicsEngine* engine, MAYBE_UNUSED int particle, MAYBE_UNUSED int group, MAYBE_UNUSED int flags, MAYBE_UNUSED void* buffer, MAYBE_UNUSED int capacity) {
    return 0;
}

void PhysicsEngine_drawParticles(MAYBE_UNUSED PhysicsEngine* engine, MAYBE_UNUSED int mask, MAYBE_UNUSED int category, MAYBE_UNUSED void (*draw)(void*, float, float, int, float), MAYBE_UNUSED void* context) {}

void PhysicsEngine_polygon(MAYBE_UNUSED PhysicsEngine* engine, MAYBE_UNUSED const float* xy, MAYBE_UNUSED int count, MAYBE_UNUSED int deleteRegion) {}