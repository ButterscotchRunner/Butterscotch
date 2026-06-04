#pragma once

#include "common.h"

#define PHYSICS_MAX_FIXTURES 256
#define PHYSICS_MAX_BODIES 512
#define PHYSICS_MAX_VERTICES 12

typedef struct PhysicsWorld_ PhysicsWorld;
typedef struct PhysicsBody_ PhysicsBody;

PhysicsWorld* PhysicsWorld_create(float gx, float gy);
void PhysicsWorld_destroy(PhysicsWorld* world);
void PhysicsWorld_setGravity(PhysicsWorld* world, float gx, float gy);
void PhysicsWorld_step(PhysicsWorld* world, float dt);

int PhysicsFixture_create(PhysicsWorld* world);
void PhysicsFixture_delete(PhysicsWorld* world, int id);
void PhysicsFixture_setCircle(PhysicsWorld* world, int id, float radius);
void PhysicsFixture_setBox(PhysicsWorld* world, int id, float w, float h);
void PhysicsFixture_setPolygon(PhysicsWorld* world, int id);
void PhysicsFixture_addPoint(PhysicsWorld* world, int id, float x, float y);
void PhysicsFixture_setDensity(PhysicsWorld* world, int id, float density);
void PhysicsFixture_setFriction(PhysicsWorld* world, int id, float friction);
void PhysicsFixture_setRestitution(PhysicsWorld* world, int id, float restitution);
void PhysicsFixture_setSensor(PhysicsWorld* world, int id, bool sensor);
void PhysicsFixture_setCollisionGroup(PhysicsWorld* world, int id, int group);
void PhysicsFixture_setLinearDamping(PhysicsWorld* world, int id, float damping);
void PhysicsFixture_setAngularDamping(PhysicsWorld* world, int id, float damping);
PhysicsBody* PhysicsFixture_bind(PhysicsWorld* world, int id, int instanceId, float x, float y);

PhysicsBody* PhysicsWorld_findBodyByInstance(PhysicsWorld* world, int instanceId);
void PhysicsBody_applyForce(PhysicsBody* body, float fx, float fy);
void PhysicsBody_applyLocalForce(PhysicsBody* body, float fx, float fy, float lx, float ly);
void PhysicsBody_applyImpulse(PhysicsBody* body, float ix, float iy);
void PhysicsBody_applyLocalImpulse(PhysicsBody* body, float ix, float iy, float lx, float ly);
void PhysicsBody_applyTorque(PhysicsBody* body, float torque);
void PhysicsBody_applyAngularImpulse(PhysicsBody* body, float impulse);
void PhysicsBody_setPosition(PhysicsBody* body, float x, float y);
void PhysicsBody_setVelocity(PhysicsBody* body, float vx, float vy);
void PhysicsBody_setAngle(PhysicsBody* body, float angle);
void PhysicsBody_setAngularVelocity(PhysicsBody* body, float av);

void PhysicsWorld_setPaused(PhysicsWorld* world, bool paused);

// Auto-create a physics body from object-definition fields (usesPhysics = true).
// shapeType: 0=circle, 1=box, 3=polygon. For circle, shapeW = radius, shapeH = 0.
// For box, shapeW = full width, shapeH = full height (half-dimensions are computed internally).
// vertices is (x0,y0,x1,y1,...) in sprite-local pixels for polygons.
PhysicsBody* PhysicsWorld_createBodyFromDef(
    PhysicsWorld* world,
    int instanceId,
    float x, float y,
    int shapeType,
    float shapeW, float shapeH,
    int vertexCount,
    const float* vertices,
    float density,
    float friction,
    float restitution,
    bool sensor,
    int collisionGroup,
    float linearDamping,
    float angularDamping,
    bool kinematic
);

typedef void (*PhysicsBodySyncCallback)(int instanceId, float x, float y, float vx, float vy, float angleDeg, void* userData);
void PhysicsWorld_syncBodies(PhysicsWorld* world, void* user, PhysicsBodySyncCallback cb);
