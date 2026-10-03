#ifndef BS_PHYSICS_INTERNAL_H
#define BS_PHYSICS_INTERNAL_H
#include "physics_engine.h"
#include "common.h"
#include "utils.h"
#include "math_compat.h"
#include <box2d/box2d.h>
#include <stb_ds.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>

#define PE_PI 3.14159265358979323846f
#define PE_RAD (PE_PI / 180.0f)
enum { PE_CIRCLE, PE_POLYGON, PE_EDGE, PE_CHAIN };
enum { PE_DISTANCE, PE_REVOLUTE, PE_PRISMATIC, PE_WHEEL, PE_WELD, PE_ROPE, PE_PULLEY, PE_GEAR, PE_FRICTION };
typedef struct {
    int shape;
    float radius, width, height, density, friction, restitution;
    float linearDamping, angularDamping;
    int group;
    bool sensor, awake, kinematic, loop;
    b2Vec2* vertices;
} PhysicsFixture;
typedef struct {
    PhysicsBody* owner;
    PhysicsFixture definition;
    b2Vec2 offset;
    b2ShapeId* shapes;
} PhysicsBoundFixture;
typedef struct PhysicsJoint {
    PhysicsEngine* engine;
    PhysicsBody *a, *b;
    int type, source1, source2;
    b2JointId id;
    b2Vec2 anchorA, anchorB, groundA, groundB, axis;
    float reference, length, ratio, constant, maxForce, maxTorque;
    float impulse, angularImpulse, lowerAngle, upperAngle, limitImpulse;
    b2Vec2 linearImpulse, reaction;
    bool collide, enableLimit;
} PhysicsJoint;
struct PhysicsResources {
    int references;
    PhysicsFixture** fixtures;
    PhysicsJoint** joints;
};
typedef struct {
    b2ShapeId a, b;
    b2Vec2 point, normal;
    int count, slice, sliceCount;
} PhysicsContact;
typedef struct PhysicsParticles PhysicsParticles;
struct PhysicsBody {
    PhysicsEngine* engine;
    b2BodyId id;
    void* instance;
    b2Vec2 offset, previous, force;
    float torque, linearDamping, angularDamping, angle;
    bool enabled;
    int lastFixture;
    PhysicsBoundFixture** fixtures;
};
struct PhysicsEngine {
    void* owner;
    PhysicsCallbacks callbacks;
    b2WorldId world;
    PhysicsResources* resources;
    PhysicsBody** bodies;
    PhysicsContact* contacts;
    PhysicsParticles* particles;
    float scale, speed, fps, timeStep;
    int iterations;
    int contactSlice;
    bool paused;
};
static inline b2Vec2 peVec(float x, float y) { b2Vec2 v = {x, y}; return v; }
static inline float peMin(float a, float b) { return a < b ? a : b; }
static inline float peMax(float a, float b) { return a > b ? a : b; }
static inline float peClamp(float x, float a, float b) { return peMin(peMax(x, a), b); }
static inline bool peFinite(float x) { return x >= -FLT_MAX && x <= FLT_MAX; }
static inline float peAtan2(float y, float x) { return (float)GMLReal_atan2((GMLReal)y, (GMLReal)x); }
static inline float pePow(float x, float y) { return (float)GMLReal_pow((GMLReal)x, (GMLReal)y); }
static inline float peCeil(float x) { return (float)GMLReal_ceil((GMLReal)x); }
static inline float peAngle(b2BodyId id) { b2Rot q = b2Body_GetRotation(id); return peAtan2(q.s, q.c); }
static inline float peWrap(float x) {
    if (!peFinite(x)) return 0;
    if (x > PE_PI || x < -PE_PI) x = fmodf(x, 2 * PE_PI);
    if (x > PE_PI) x -= 2 * PE_PI;
    if (x < -PE_PI) x += 2 * PE_PI;
    return x;
}
static inline bool peBodyValid(const PhysicsBody* p) { return p && p->engine && b2Body_IsValid(p->id); }
static inline float peArg(const double* args, int count, int index) { return index < count ? (float)args[index] : 0; }
static inline PhysicsJoint* peJoint(PhysicsResources* r, int id) { return id >= 0 && id < arrlen(r->joints) ? r->joints[id] : NULL; }
void peSync(PhysicsBody* p);
void peDrawLine(PhysicsEngine* e, b2Vec2 a, b2Vec2 b);
void peDrawCircle(PhysicsEngine* e, b2Vec2 center, float radius);
void peDestroyJoint(PhysicsResources* r, int id);
void peSolveJoints(PhysicsEngine* e, float dt, bool reset);
double peJointCall(PhysicsEngine* e, const char* name, PhysicsBody* a, PhysicsBody* b, const double* args, int count);
PhysicsParticles* peParticlesCreate(void);
void peParticlesFree(PhysicsParticles* p);
void peParticlesStep(PhysicsEngine* e, float dt);
double peParticleCall(PhysicsEngine* e, const char* name, const double* args, int count);
void peParticlesDebug(PhysicsEngine* e);
#endif
