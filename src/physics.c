#include "physics.h"
#include "box2d/box2d.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define DEFAULT_MPP (1.0f / 30.0f)
#define DEG2RAD (3.14159265358979323846f / 180.0f)
#define RAD2DEG (180.0f / 3.14159265358979323846f)

typedef struct {
    bool used;
    int id;
    int shapeType;
    float radius;
    float w, h;
    int vertexCount;
    float vertices[PHYSICS_MAX_VERTICES * 2];
    float density, friction, restitution;
    bool sensor;
    int collisionGroup;
    float linearDamping, angularDamping;
} FixtureTemplate;

typedef struct {
    bool used;
    int instanceId;
    b2BodyId bodyId;
    float metersPerPixel;
} BodyEntry;

struct PhysicsWorld_ {
    b2WorldId worldId;
    bool worldValid;
    float metersPerPixel;
    FixtureTemplate templates[PHYSICS_MAX_FIXTURES];
    int nextTemplateId;
    BodyEntry bodies[PHYSICS_MAX_BODIES];
    float gravityX, gravityY;
    bool paused;
};

PhysicsWorld* PhysicsWorld_create(float gx, float gy) {
    PhysicsWorld* world = (PhysicsWorld*)calloc(1, sizeof(PhysicsWorld));
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity.x = gx * DEFAULT_MPP; def.gravity.y = gy * DEFAULT_MPP;
    world->worldId = b2CreateWorld(&def);
    world->worldValid = true;
    world->metersPerPixel = DEFAULT_MPP;
    world->gravityX = gx;
    world->gravityY = gy;
    world->nextTemplateId = 1;
    return world;
}

void PhysicsWorld_destroy(PhysicsWorld* world) {
    if (!world) return;
    if (world->worldValid) b2DestroyWorld(world->worldId);
    free(world);
}

void PhysicsWorld_setGravity(PhysicsWorld* world, float gx, float gy) {
    if (!world || !world->worldValid) return;
    world->gravityX = gx;
    world->gravityY = gy;
    {
        b2Vec2 _g = { gx * world->metersPerPixel, gy * world->metersPerPixel };
        b2World_SetGravity(world->worldId, _g);
    }
}

void PhysicsWorld_step(PhysicsWorld* world, float dt) {
    if (!world || !world->worldValid || world->paused) return;
    if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;
    b2World_Step(world->worldId, dt, 4);
}

void PhysicsWorld_setPaused(PhysicsWorld* world, bool paused) {
    if (world) world->paused = paused;
}

int PhysicsFixture_create(PhysicsWorld* world) {
    if (!world) return -1;
    int id = world->nextTemplateId++;
    for (int i = 0; i < PHYSICS_MAX_FIXTURES; i++) {
        if (!world->templates[i].used) {
            FixtureTemplate* ft = &world->templates[i];
            memset(ft, 0, sizeof(FixtureTemplate));
            ft->used = true;
            ft->id = id;
            ft->radius = 1.0f;
            ft->density = 1.0f;
            ft->friction = 0.2f;
            ft->restitution = 0.0f;
            return id;
        }
    }
    return -1;
}

static FixtureTemplate* getTemplate(PhysicsWorld* world, int id) {
    if (!world || id < 1) return NULL;
    for (int i = 0; i < PHYSICS_MAX_FIXTURES; i++) {
        if (world->templates[i].used && world->templates[i].id == id)
            return &world->templates[i];
    }
    return NULL;
}

void PhysicsFixture_delete(PhysicsWorld* world, int id) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (ft) ft->used = false;
}

void PhysicsFixture_setCircle(PhysicsWorld* world, int id, float radius) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (!ft) return;
    ft->shapeType = 0;
    ft->radius = radius;
}

void PhysicsFixture_setBox(PhysicsWorld* world, int id, float w, float h) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (!ft) return;
    ft->shapeType = 1;
    ft->w = w;
    ft->h = h;
}

void PhysicsFixture_setPolygon(PhysicsWorld* world, int id) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (!ft) return;
    ft->shapeType = 2;
    ft->vertexCount = 0;
}

void PhysicsFixture_addPoint(PhysicsWorld* world, int id, float x, float y) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (!ft || ft->shapeType != 2) return;
    if (ft->vertexCount >= PHYSICS_MAX_VERTICES) return;
    ft->vertices[ft->vertexCount * 2 + 0] = x;
    ft->vertices[ft->vertexCount * 2 + 1] = y;
    ft->vertexCount++;
}

void PhysicsFixture_setDensity(PhysicsWorld* world, int id, float density) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (ft) ft->density = density;
}

void PhysicsFixture_setFriction(PhysicsWorld* world, int id, float friction) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (ft) ft->friction = friction;
}

void PhysicsFixture_setRestitution(PhysicsWorld* world, int id, float restitution) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (ft) ft->restitution = restitution;
}

void PhysicsFixture_setSensor(PhysicsWorld* world, int id, bool sensor) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (ft) ft->sensor = sensor;
}

void PhysicsFixture_setCollisionGroup(PhysicsWorld* world, int id, int group) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (ft) ft->collisionGroup = group;
}

void PhysicsFixture_setLinearDamping(PhysicsWorld* world, int id, float damping) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (ft) ft->linearDamping = damping;
}

void PhysicsFixture_setAngularDamping(PhysicsWorld* world, int id, float damping) {
    FixtureTemplate* ft = getTemplate(world, id);
    if (ft) ft->angularDamping = damping;
}

static BodyEntry* decodeBody(PhysicsBody* ptr) {
    if (!ptr) return NULL;
    BodyEntry* be = (BodyEntry*)ptr;
    if (!be->used) return NULL;
    return be;
}

PhysicsBody* PhysicsFixture_bind(PhysicsWorld* world, int id, int instanceId, float x, float y) {
    if (!world || !world->worldValid) return NULL;
    FixtureTemplate* ft = getTemplate(world, id);
    if (!ft) return NULL;

    int bodyIdx = -1;
    for (int i = 0; i < PHYSICS_MAX_BODIES; i++) {
        if (!world->bodies[i].used) { bodyIdx = i; break; }
    }
    if (bodyIdx < 0) return NULL;

    float mpp = world->metersPerPixel;
    float mx = x * mpp;
    float my = y * mpp;

    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = (ft->density > 0) ? b2_dynamicBody : b2_staticBody;
    bodyDef.position.x = mx; bodyDef.position.y = my;
    bodyDef.linearDamping = ft->linearDamping;
    bodyDef.angularDamping = ft->angularDamping;
    bodyDef.userData = (void*)(intptr_t)instanceId;

    b2BodyId bodyId = b2CreateBody(world->worldId, &bodyDef);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = ft->density;
    shapeDef.material.friction = ft->friction;
    shapeDef.material.restitution = ft->restitution;
    shapeDef.isSensor = ft->sensor;
    shapeDef.filter.groupIndex = ft->collisionGroup;

    if (ft->shapeType == 0) {
        b2Circle circle;
        circle.center.x = 0;
        circle.center.y = 0;
        circle.radius = ft->radius * mpp;
        b2CreateCircleShape(bodyId, &shapeDef, &circle);
    } else if (ft->shapeType == 1) {
        b2Polygon box = b2MakeBox(ft->w * 0.5f * mpp, ft->h * 0.5f * mpp);
        b2CreatePolygonShape(bodyId, &shapeDef, &box);
    } else if (ft->shapeType == 2 && ft->vertexCount >= 3) {
        b2Vec2 verts[PHYSICS_MAX_VERTICES];
        int vc = ft->vertexCount < PHYSICS_MAX_VERTICES ? ft->vertexCount : PHYSICS_MAX_VERTICES;
        for (int vi = 0; vi < vc; vi++) {
            verts[vi].x = ft->vertices[vi * 2 + 0] * mpp;
            verts[vi].y = ft->vertices[vi * 2 + 1] * mpp;
        }
        b2Hull hull = b2ComputeHull(verts, vc);
        if (hull.count > 0) {
            b2Polygon poly = b2MakePolygon(&hull, 0);
            b2CreatePolygonShape(bodyId, &shapeDef, &poly);
        }
    }

    BodyEntry* entry = &world->bodies[bodyIdx];
    entry->used = true;
    entry->instanceId = instanceId;
    entry->bodyId = bodyId;
    entry->metersPerPixel = mpp;

    return (PhysicsBody*)entry;
}

PhysicsBody* PhysicsWorld_findBodyByInstance(PhysicsWorld* world, int instanceId) {
    if (!world) return NULL;
    for (int i = 0; i < PHYSICS_MAX_BODIES; i++) {
        if (world->bodies[i].used && world->bodies[i].instanceId == instanceId) {
            return (PhysicsBody*)&world->bodies[i];
        }
    }
    return NULL;
}

void PhysicsBody_applyForce(PhysicsBody* body, float fx, float fy) {
    BodyEntry* be = decodeBody(body);
    if (!be) return;
    {
        b2Vec2 _fv = { fx, fy };
        b2Body_ApplyForceToCenter(be->bodyId, _fv, true);
    }
}

void PhysicsBody_applyLocalForce(PhysicsBody* body, float fx, float fy, float lx, float ly) {
    BodyEntry* be = decodeBody(body);
    if (!be) return;
    {
        b2Vec2 _fa = { fx, fy };
        b2Vec2 _la = { lx, ly };
        b2Vec2 worldForce = b2Body_GetWorldVector(be->bodyId, _fa);
        b2Vec2 worldPoint = b2Body_GetWorldPoint(be->bodyId, _la);
        b2Body_ApplyForce(be->bodyId, worldForce, worldPoint, true);
    }
}

void PhysicsBody_applyImpulse(PhysicsBody* body, float ix, float iy) {
    BodyEntry* be = decodeBody(body);
    if (!be) return;
    {
        b2Vec2 _iv = { ix, iy };
        b2Body_ApplyLinearImpulseToCenter(be->bodyId, _iv, true);
    }
}

void PhysicsBody_applyLocalImpulse(PhysicsBody* body, float ix, float iy, float lx, float ly) {
    BodyEntry* be = decodeBody(body);
    if (!be) return;
    {
        b2Vec2 _ia = { ix, iy };
        b2Vec2 _la = { lx, ly };
        b2Vec2 worldImpulse = b2Body_GetWorldVector(be->bodyId, _ia);
        b2Vec2 worldPoint = b2Body_GetWorldPoint(be->bodyId, _la);
        b2Body_ApplyLinearImpulse(be->bodyId, worldImpulse, worldPoint, true);
    }
}

void PhysicsBody_applyTorque(PhysicsBody* body, float torque) {
    BodyEntry* be = decodeBody(body);
    if (!be) return;
    b2Body_ApplyTorque(be->bodyId, torque, true);
}

void PhysicsBody_applyAngularImpulse(PhysicsBody* body, float impulse) {
    BodyEntry* be = decodeBody(body);
    if (!be) return;
    b2Body_ApplyAngularImpulse(be->bodyId, impulse, true);
}

void PhysicsBody_setPosition(PhysicsBody* body, float x, float y) {
    BodyEntry* be = decodeBody(body);
    if (!be) return;
    {
        float mpp = be->metersPerPixel;
        b2Vec2 _p = { x * mpp, y * mpp };
        b2Body_SetTransform(be->bodyId, _p, b2Body_GetRotation(be->bodyId));
    }
    b2Body_SetAwake(be->bodyId, true);
}

void PhysicsBody_setVelocity(PhysicsBody* body, float vx, float vy) {
    BodyEntry* be = decodeBody(body);
    if (!be) return;
    {
        float mpp = be->metersPerPixel;
        b2Vec2 _v = { vx * mpp, vy * mpp };
        b2Body_SetLinearVelocity(be->bodyId, _v);
    }
}

void PhysicsBody_setAngle(PhysicsBody* body, float angle) {
    BodyEntry* be = decodeBody(body);
    if (!be) return;
    b2Body_SetTransform(be->bodyId, b2Body_GetPosition(be->bodyId), b2MakeRot(angle * DEG2RAD));
    b2Body_SetAwake(be->bodyId, true);
}

void PhysicsBody_setAngularVelocity(PhysicsBody* body, float av) {
    BodyEntry* be = decodeBody(body);
    if (!be) return;
    b2Body_SetAngularVelocity(be->bodyId, av * DEG2RAD);
}

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
) {
    if (!world || !world->worldValid) return NULL;

    int bodyIdx = -1;
    for (int i = 0; i < PHYSICS_MAX_BODIES; i++) {
        if (!world->bodies[i].used) { bodyIdx = i; break; }
    }
    if (bodyIdx < 0) return NULL;

    float mpp = world->metersPerPixel;
    float mx = x * mpp;
    float my = y * mpp;

    // Sanitize input values: Box2D asserts that density/friction/restitution are valid finite floats >= 0.
    // Data files (especially UTY) may contain NaN for unused physics fields, so clamp them here.
    float safeDensity = (isfinite(density) && density >= 0.0f) ? density : 0.0f;
    float safeFriction = (isfinite(friction) && friction >= 0.0f) ? friction : 0.0f;
    float safeRestitution = (isfinite(restitution) && restitution >= 0.0f) ? restitution : 0.0f;
    float safeLinearDamping = isfinite(linearDamping) ? linearDamping : 0.0f;
    float safeAngularDamping = isfinite(angularDamping) ? angularDamping : 0.0f;
    int safeGroup = isfinite((float)collisionGroup) ? collisionGroup : 0;

    b2BodyType bodyType = b2_staticBody;
    if (kinematic) {
        bodyType = b2_kinematicBody;
    } else if (safeDensity > 0) {
        bodyType = b2_dynamicBody;
    }

    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = bodyType;
    bodyDef.position.x = mx; bodyDef.position.y = my;
    bodyDef.linearDamping = safeLinearDamping;
    bodyDef.angularDamping = safeAngularDamping;
    bodyDef.userData = (void*)(intptr_t)instanceId;

    b2BodyId bodyId = b2CreateBody(world->worldId, &bodyDef);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = safeDensity;
    shapeDef.material.friction = safeFriction;
    shapeDef.material.restitution = safeRestitution;
    shapeDef.isSensor = sensor;
    shapeDef.filter.groupIndex = safeGroup;

    if (shapeType == 0 && shapeW > 0) {
        b2Circle circle;
        circle.center.x = 0;
        circle.center.y = 0;
        circle.radius = shapeW * mpp;
        b2CreateCircleShape(bodyId, &shapeDef, &circle);
    } else if (shapeType == 1 && shapeW > 0 && shapeH > 0) {
        b2Polygon box = b2MakeBox(shapeW * 0.5f * mpp, shapeH * 0.5f * mpp);
        b2CreatePolygonShape(bodyId, &shapeDef, &box);
    } else if (shapeType == 3 && vertexCount >= 3) {
        b2Vec2 verts[PHYSICS_MAX_VERTICES];
        int vc = vertexCount < PHYSICS_MAX_VERTICES ? vertexCount : PHYSICS_MAX_VERTICES;
        for (int vi = 0; vi < vc; vi++) {
            verts[vi].x = vertices[vi * 2 + 0] * mpp;
            verts[vi].y = vertices[vi * 2 + 1] * mpp;
        }
        b2Hull hull = b2ComputeHull(verts, vc);
        if (hull.count > 0) {
            b2Polygon poly = b2MakePolygon(&hull, 0);
            b2CreatePolygonShape(bodyId, &shapeDef, &poly);
        }
    }

    BodyEntry* entry = &world->bodies[bodyIdx];
    entry->used = true;
    entry->instanceId = instanceId;
    entry->bodyId = bodyId;
    entry->metersPerPixel = mpp;

    return (PhysicsBody*)entry;
}

void PhysicsWorld_syncBodies(PhysicsWorld* world, void* user, PhysicsBodySyncCallback cb) {
    if (!world || !world->worldValid) return;
    float inv_mpp = 1.0f / world->metersPerPixel;
    for (int i = 0; i < PHYSICS_MAX_BODIES; i++) {
        BodyEntry* be = &world->bodies[i];
        if (!be->used) continue;
        if (!b2Body_IsValid(be->bodyId)) continue;
        b2Vec2 pos = b2Body_GetPosition(be->bodyId);
        b2Vec2 vel = b2Body_GetLinearVelocity(be->bodyId);
        b2Rot rot = b2Body_GetRotation(be->bodyId);
        float angle = atan2f(rot.s, rot.c);
        cb(be->instanceId,
           pos.x * inv_mpp, pos.y * inv_mpp,
           vel.x * inv_mpp, vel.y * inv_mpp,
           angle * RAD2DEG,
           user);
    }
}
