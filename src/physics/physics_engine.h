#ifndef BS_PHYSICS_ENGINE_H
#define BS_PHYSICS_ENGINE_H

/* the runtime is isolated from the Box2D implementation, and all lengths crossing this
 * interface are pixels, except forces, mass, inertia and joint field values */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct PhysicsEngine PhysicsEngine;
typedef struct PhysicsBody PhysicsBody;
typedef struct {
    void (*sync)(void* owner, void* instance, float x, float y, float angle);
    int (*filter)(void* owner, void* a, void* b);
    void (*contact)(void* owner, void* a, void* b, int points, float x, float y, float nx, float ny);
    void (*line)(void* owner, float x1, float y1, float x2, float y2);
} PhysicsCallbacks;
PhysicsEngine* PhysicsEngine_create(void* owner, PhysicsCallbacks callbacks, float scale, float fps, PhysicsEngine* resources);
void PhysicsEngine_free(PhysicsEngine* engine);
void PhysicsEngine_step(PhysicsEngine* engine, float fps);
PhysicsBody* PhysicsEngine_body(PhysicsEngine* engine, void* instance, int fixture, float x, float y, float angle, float xo, float yo, int visualOffset);
void PhysicsEngine_destroyBody(PhysicsBody* body);
void PhysicsEngine_transform(PhysicsBody* body, float x, float y, float angle, int active);
PhysicsBody* PhysicsEngine_rehome(PhysicsBody* body, PhysicsEngine* engine);
double PhysicsEngine_variable(PhysicsBody* body, int field, double value, int write, float fps);
double PhysicsEngine_call(PhysicsEngine* engine, const char* name, PhysicsBody* a, PhysicsBody* b, const double* args, int count);
int PhysicsEngine_overlap(PhysicsBody* a, PhysicsBody* b, float x, float y, float angle);
int PhysicsEngine_raycast(PhysicsBody* body, float x1, float y1, float x2, float y2, float fraction, float* result);
int PhysicsEngine_particleData(PhysicsEngine* engine, int particle, int group, int flags, void* buffer, int capacity);
void PhysicsEngine_drawParticles(PhysicsEngine* engine, int mask, int category, void (*draw)(void*, float, float, int, float), void* context);
void PhysicsEngine_polygon(PhysicsEngine* engine, const float* xy, int count, int deleteRegion);

enum PhysicsVariable {
    PHY_ROTATION, PHY_POSITION_X, PHY_POSITION_Y, PHY_ANGULAR_VELOCITY,
    PHY_LINEAR_VELOCITY_X, PHY_LINEAR_VELOCITY_Y, PHY_SPEED, PHY_SPEED_X, PHY_SPEED_Y,
    PHY_ANGULAR_DAMPING, PHY_LINEAR_DAMPING, PHY_BULLET, PHY_FIXED_ROTATION,
    PHY_ACTIVE, PHY_MASS, PHY_INERTIA, PHY_COM_X, PHY_COM_Y, PHY_DYNAMIC,
    PHY_KINEMATIC, PHY_SLEEPING, PHY_POSITION_XPREVIOUS, PHY_POSITION_YPREVIOUS,
    PHY_COLLISION_POINTS, PHY_COLLISION_X, PHY_COLLISION_Y, PHY_COL_NORMAL_X, PHY_COL_NORMAL_Y,
    PHY_VARIABLE_COUNT
};
#ifdef __cplusplus
}
#endif
#endif
