#include "physics_internal.h"
#include <limits.h>
#include <float.h>

/* the HTML5 particle API is implemented here because Box2D 3 has no particle
 * system and i didnt want to break stuff implementing the version of box2d html5 uses (its c++) this is a serial, spatially hashed fluid/constraint solver. */
enum { PP_ZOMBIE = 2, PP_WALL = 4, PP_SPRING = 8, PP_ELASTIC = 16, PP_VISCOUS = 32, PP_POWDER = 64, PP_TENSILE = 128, PP_COLOUR = 256 };
typedef struct {
    b2Vec2 position, velocity, rest;
    uint32_t flags;
    unsigned char color[4];
    int category, group, next, cellX, cellY;
    float weight;
} FluidParticle;
typedef struct {
    uint32_t flags;
    float strength, angle, omega;
    b2Vec2 position, center, velocity;
} FluidGroup;
typedef struct { int a, b; float length, strength; } FluidBond;
typedef struct { int a, b; b2Vec2 normal; float weight; } FluidContact;
struct PhysicsParticles {
    FluidParticle* items;
    FluidGroup** groups;
    FluidBond* bonds;
    FluidContact* contacts;
    int* buckets;
    int* remap;
    int maxCount;
    float radius, density, damping, gravityScale;
    FluidParticle pending;
    FluidGroup pendingGroup;
    b2Vec2* vertices;
    int shape;
    float shapeRadius, halfWidth, halfHeight;
};
PhysicsParticles* peParticlesCreate(void) {
    PhysicsParticles* p = (PhysicsParticles*)safeCalloc(1, sizeof(*p));
    p->radius = 1; p->density = 1; p->damping = 1; p->gravityScale = 1; p->shape = -1;
    return p;
}
void peParticlesFree(PhysicsParticles* p) {
    if (!p) return;
    for (int i = 0; i < arrlen(p->groups); ++i) free(p->groups[i]);
    arrfree(p->groups); arrfree(p->items); arrfree(p->bonds); arrfree(p->contacts);
    arrfree(p->buckets); arrfree(p->remap); arrfree(p->vertices); free(p);
}
static FluidGroup* groupGet(PhysicsParticles* p, int index) { return index >= 0 && index < arrlen(p->groups) ? p->groups[index] : NULL; }
static float particleMass(PhysicsParticles* p) { float stride = 1.5f * p->radius; return p->density * stride * stride; }
static int particleAdd(PhysicsParticles* p, FluidParticle item) {
    if (p->maxCount && arrlen(p->items) >= p->maxCount) return -1;
    item.next = -1; item.weight = 0; arrput(p->items, item); return (int)arrlen(p->items) - 1;
}
static void removeZombies(PhysicsParticles* p) {
    int count = (int)arrlen(p->items), out = 0; arrsetlen(p->remap, count);
    for (int i = 0; i < count; ++i) {
        if (p->items[i].flags & PP_ZOMBIE) p->remap[i] = -1;
        else { p->remap[i] = out; p->items[out++] = p->items[i]; }
    }
    arrsetlen(p->items, out);
    out = 0;
    for (int i = 0; i < arrlen(p->bonds); ++i) {
        FluidBond b = p->bonds[i]; b.a = p->remap[b.a]; b.b = p->remap[b.b];
        if (b.a >= 0 && b.b >= 0) p->bonds[out++] = b;
    }
    arrsetlen(p->bonds, out);
    for (int g = 0; g < arrlen(p->groups); ++g) if (p->groups[g]) {
        bool alive = false;
        for (int i = 0; i < arrlen(p->items); ++i) if (p->items[i].group == g) { alive = true; break; }
        if (!alive) { free(p->groups[g]); p->groups[g] = NULL; }
    }
}
static unsigned hashCell(int x, int y, unsigned mask) { return ((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u) & mask; }
static void buildContacts(PhysicsParticles* p) {
    int count = (int)arrlen(p->items), size = 16;
    while (size < count * 2 && size < INT_MAX / 2) size *= 2;
    arrsetlen(p->buckets, size); for (int i = 0; i < size; ++i) p->buckets[i] = -1;
    float diameter = 2 * p->radius, inv = 1 / diameter;
    for (int i = 0; i < count; ++i) {
        FluidParticle* a = &p->items[i]; a->weight = 0;
        a->cellX = (int)peClamp(floorf(a->position.x * inv), -1000000000, 1000000000);
        a->cellY = (int)peClamp(floorf(a->position.y * inv), -1000000000, 1000000000);
        unsigned bucket = hashCell(a->cellX, a->cellY, size - 1);
        a->next = p->buckets[bucket]; p->buckets[bucket] = i;
    }
    arrsetlen(p->contacts, 0);
    for (int i = 0; i < count; ++i) {
        FluidParticle* a = &p->items[i];
        for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y) {
            int cx = a->cellX + x, cy = a->cellY + y;
            for (int j = p->buckets[hashCell(cx, cy, size - 1)]; j >= 0; j = p->items[j].next) {
                FluidParticle* b = &p->items[j]; if (j <= i || b->cellX != cx || b->cellY != cy) continue;
                b2Vec2 delta = b2Sub(b->position, a->position); float length = b2Length(delta);
                if (length >= diameter) continue;
                b2Vec2 n = length > 1e-8f ? b2MulSV(1 / length, delta) : peVec(1, 0);
                float weight = 1 - length / diameter;
                FluidContact contact = {i, j, n, weight}; arrput(p->contacts, contact);
                a->weight += weight; b->weight += weight;
            }
        }
    }
}
static void groupStats(PhysicsParticles* p, int id, float* mass, float* inertia) {
    FluidGroup* g = groupGet(p, id); *mass = *inertia = 0; if (!g) return;
    b2Vec2 center = b2Vec2_zero, velocity = b2Vec2_zero, restCenter = b2Vec2_zero; int count = 0;
    for (int i = 0; i < arrlen(p->items); ++i) if (p->items[i].group == id) {
        center = b2Add(center, p->items[i].position); velocity = b2Add(velocity, p->items[i].velocity);
        restCenter = b2Add(restCenter, p->items[i].rest); ++count;
    }
    if (!count) return;
    float m = particleMass(p); center = b2MulSV(1.0f / count, center); velocity = b2MulSV(1.0f / count, velocity);
    restCenter = b2MulSV(1.0f / count, restCenter);
    float angular = 0, dot = 0, cross = 0;
    for (int i = 0; i < arrlen(p->items); ++i) if (p->items[i].group == id) {
        b2Vec2 offset = b2Sub(p->items[i].position, center), rest = b2Sub(p->items[i].rest, restCenter);
        *inertia += m * b2LengthSquared(offset); angular += m * b2Cross(offset, b2Sub(p->items[i].velocity, velocity));
        dot += b2Dot(rest, offset); cross += b2Cross(rest, offset);
    }
    g->center = center; g->velocity = velocity; g->omega = *inertia > 1e-12f ? angular / *inertia : 0;
    if (fabsf(dot) + fabsf(cross) > 1e-12f) g->angle = peAtan2(cross, dot);
    g->position = b2Sub(center, b2RotateVector(b2MakeRot(g->angle), restCenter)); *mass = m * count;
}
static void solveGroups(PhysicsParticles* p, float dt) {
    for (int id = 0; id < arrlen(p->groups); ++id) {
        FluidGroup* g = p->groups[id]; if (!g) continue;
        float mass, inertia; groupStats(p, id, &mass, &inertia);
        bool rigid = (g->flags & 2) != 0;
        for (int i = 0; i < arrlen(p->items); ++i) {
            FluidParticle* a = &p->items[i]; if (a->group != id || (a->flags & PP_WALL)) continue;
            if (!rigid && !(a->flags & PP_ELASTIC)) continue;
            b2Vec2 target = b2Add(g->position, b2RotateVector(b2MakeRot(g->angle), a->rest));
            b2Vec2 correction = b2MulSV(rigid ? 1 : peClamp(20 * dt * g->strength, 0, 1), b2Sub(target, a->position));
            a->position = b2Add(a->position, correction);
            if (rigid) a->velocity = b2Add(g->velocity, b2CrossSV(g->omega, b2Sub(a->position, g->center)));
            else a->velocity = b2MulAdd(a->velocity, 1 / dt, correction);
        }
    }
}
typedef struct { PhysicsEngine* engine; FluidParticle* particle; float radius, mass; } ParticleBodyContext;
static void bodyResponse(ParticleBodyContext* c, b2ShapeId shape, b2Vec2 normal, b2Vec2 point) {
    FluidParticle* p = c->particle; b2BodyId body = b2Shape_GetBody(shape);
    b2Vec2 relative = b2Sub(p->velocity, b2Body_GetWorldPointVelocity(body, point));
    float vn = b2Dot(relative, normal); if (vn >= 0) return;
    float im = b2Body_GetType(body) == b2_dynamicBody && b2Body_GetMass(body) > 0 ? 1 / b2Body_GetMass(body) : 0;
    float ii = b2Body_GetType(body) == b2_dynamicBody && b2Body_GetRotationalInertia(body) > 0 ? 1 / b2Body_GetRotationalInertia(body) : 0;
    b2Vec2 lever = b2Sub(point, b2Body_GetWorldCenterOfMass(body)); float rn = b2Cross(lever, normal);
    float particleInverseMass = (p->flags & PP_WALL) ? 0 : 1 / c->mass;
    float effectiveMass = particleInverseMass + im + ii * rn * rn;
    if (effectiveMass <= 1e-12f) return;
    float normalImpulse = -(1 + b2Shape_GetRestitution(shape)) * vn / effectiveMass;
    b2Vec2 tangent = peVec(-normal.y, normal.x); float rt = b2Cross(lever, tangent);
    float tangentMass = particleInverseMass + im + ii * rt * rt;
    float tangentImpulse = tangentMass > 1e-12f ? -b2Dot(relative, tangent) / tangentMass : 0;
    tangentImpulse = peClamp(tangentImpulse, -b2Shape_GetFriction(shape) * normalImpulse, b2Shape_GetFriction(shape) * normalImpulse);
    b2Vec2 impulse = b2Add(b2MulSV(normalImpulse, normal), b2MulSV(tangentImpulse, tangent));
    p->velocity = b2MulAdd(p->velocity, particleInverseMass, impulse);
    if (im) b2Body_ApplyLinearImpulse(body, b2Neg(impulse), point, true);
}
typedef struct { ParticleBodyContext collision; b2ShapeId shape; b2Vec2 point, normal; float fraction; } SweepContext;
static float sweepResult(b2ShapeId shape, b2Vec2 point, b2Vec2 normal, float fraction, void* context) {
    SweepContext* c = (SweepContext*)context; if (b2Shape_IsSensor(shape)) return -1;
    if (fraction < c->fraction) { c->shape = shape; c->point = point; c->normal = normal; c->fraction = fraction; }
    return c->fraction;
}
static bool overlapBody(b2ShapeId shape, void* context) {
    ParticleBodyContext* c = (ParticleBodyContext*)context; if (b2Shape_IsSensor(shape)) return true;
    b2Vec2 pos = c->particle->position, closest = b2Shape_GetClosestPoint(shape, pos), delta = b2Sub(pos, closest);
    float length = b2Length(delta); b2Vec2 normal = length > 1e-8f ? b2MulSV(1 / length, delta) : b2Vec2_zero;
    float depth = c->radius - length;
    if (length < 1e-8f) {
        b2Transform t = b2Body_GetTransform(b2Shape_GetBody(shape)); b2Vec2 local = b2InvTransformPoint(t, pos);
        if (b2Shape_GetType(shape) == b2_polygonShape) {
            b2Polygon poly = b2Shape_GetPolygon(shape); float distance = -FLT_MAX; int face = 0;
            for (int i = 0; i < poly.count; ++i) { float d = b2Dot(poly.normals[i], b2Sub(local, poly.vertices[i])); if (d > distance) { distance = d; face = i; } }
            normal = b2RotateVector(t.q, poly.normals[face]); depth = c->radius + poly.radius - distance;
        } else if (b2Shape_GetType(shape) == b2_circleShape) {
            b2Circle circle = b2Shape_GetCircle(shape); delta = b2Sub(local, circle.center); length = b2Length(delta);
            normal = b2RotateVector(t.q, length > 1e-8f ? b2MulSV(1 / length, delta) : peVec(1, 0)); depth = c->radius + circle.radius - length;
        } else return true;
    }
    if (depth > 0) {
        if (!(c->particle->flags & PP_WALL)) c->particle->position = b2MulAdd(pos, depth + 0.0001f, normal);
        bodyResponse(c, shape, normal, closest);
    }
    return true;
}
static void collideBodies(PhysicsEngine* e, FluidParticle* p, b2Vec2 old, float dt) {
    PhysicsParticles* ps = e->particles; ParticleBodyContext c = {e, p, ps->radius, particleMass(ps)};
    b2Vec2 delta = b2Sub(p->position, old);
    if (b2LengthSquared(delta) > ps->radius * ps->radius) {
        b2ShapeProxy proxy = b2MakeProxy(&old, 1, ps->radius);
        SweepContext sweep = {0}; sweep.collision = c; sweep.fraction = 1;
        b2World_CastShape(e->world, &proxy, delta, b2DefaultQueryFilter(), sweepResult, &sweep);
        if (B2_IS_NON_NULL(sweep.shape)) {
            p->position = b2MulAdd(old, sweep.fraction, delta); p->position = b2MulAdd(p->position, 0.0001f, sweep.normal);
            bodyResponse(&c, sweep.shape, sweep.normal, sweep.point);
            p->position = b2MulAdd(p->position, dt * (1 - sweep.fraction), p->velocity);
        }
    }
    b2AABB bounds = {b2Sub(p->position, peVec(ps->radius, ps->radius)), b2Add(p->position, peVec(ps->radius, ps->radius))};
    b2World_OverlapAABB(e->world, bounds, b2DefaultQueryFilter(), overlapBody, &c);
}
void peParticlesStep(PhysicsEngine* e, float dt) {
    PhysicsParticles* p = e->particles; removeZombies(p); if (!arrlen(p->items)) return;
    buildContacts(p); float diameter = 2 * p->radius, criticalVelocity = diameter / dt;
    b2Vec2 gravity = b2MulSV(dt * p->gravityScale, b2World_GetGravity(e->world));
    for (int i = 0; i < arrlen(p->items); ++i) if (!(p->items[i].flags & PP_WALL)) p->items[i].velocity = b2Add(p->items[i].velocity, gravity);
    for (int i = 0; i < arrlen(p->contacts); ++i) {
        FluidContact* contact = &p->contacts[i]; FluidParticle* a = &p->items[contact->a], *b = &p->items[contact->b];
        uint32_t flags = a->flags | b->flags;
        float wa = (a->flags & PP_WALL) ? 0 : (b->flags & PP_WALL) ? 1 : 0.5f;
        float wb = (b->flags & PP_WALL) ? 0 : (a->flags & PP_WALL) ? 1 : 0.5f;
        float pressure = 0.05f * criticalVelocity * contact->weight * (peMax(0, a->weight - 1) + peMax(0, b->weight - 1));
        if (flags & PP_POWDER) pressure = 0.5f * criticalVelocity * peMax(0, contact->weight - 0.25f);
        if ((flags & PP_TENSILE) && !(flags & PP_POWDER)) pressure -= 0.01f * criticalVelocity * contact->weight * (1 - contact->weight);
        if (a->group != b->group) {
            FluidGroup* ga = groupGet(p, a->group), *gb = groupGet(p, b->group);
            float depth = (ga && (ga->flags & 1) ? peMax(0, a->weight - 1) : 0) + (gb && (gb->flags & 1) ? peMax(0, b->weight - 1) : 0);
            pressure += 0.05f * criticalVelocity * contact->weight * depth;
        }
        float vn = b2Dot(b2Sub(b->velocity, a->velocity), contact->normal);
        pressure += peMax(0, -p->damping * contact->weight * vn);
        b2Vec2 impulse = b2MulSV(pressure, contact->normal);
        if (flags & PP_VISCOUS) impulse = b2Add(impulse, b2MulSV(-peClamp(10 * dt * contact->weight, 0, 0.5f), b2Sub(b->velocity, a->velocity)));
        a->velocity = b2MulSub(a->velocity, wa, impulse); b->velocity = b2MulAdd(b->velocity, wb, impulse);
        if ((a->flags & b->flags & PP_COLOUR) != 0) for (int k = 0; k < 4; ++k) {
            int deltaColor = (int)((b->color[k] - a->color[k]) * peClamp(5 * dt * contact->weight, 0, 0.5f));
            a->color[k] = (unsigned char)(a->color[k] + deltaColor); b->color[k] = (unsigned char)(b->color[k] - deltaColor);
        }
    }
    for (int i = 0; i < arrlen(p->bonds); ++i) {
        FluidBond* bond = &p->bonds[i]; FluidParticle* a = &p->items[bond->a], *b = &p->items[bond->b];
        if (!((a->flags | b->flags) & PP_SPRING)) continue;
        b2Vec2 d = b2Sub(b->position, a->position); float length = b2Length(d); if (length < 1e-8f) continue;
        float error = length - bond->length, speed = b2Dot(b2Sub(b->velocity, a->velocity), b2MulSV(1 / length, d));
        float strength = peClamp(30 * bond->strength * dt, 0, 1);
        b2Vec2 impulse = b2MulSV(strength * (error / dt + speed) / length, d);
        if (!(a->flags & PP_WALL)) a->velocity = b2MulAdd(a->velocity, 0.5f, impulse);
        if (!(b->flags & PP_WALL)) b->velocity = b2MulSub(b->velocity, 0.5f, impulse);
    }
    for (int i = 0; i < arrlen(p->items); ++i) {
        FluidParticle* a = &p->items[i];
        if (a->flags & PP_WALL) { a->velocity = b2Vec2_zero; collideBodies(e, a, a->position, dt); continue; }
        float speed = b2Length(a->velocity); if (speed > criticalVelocity) a->velocity = b2MulSV(criticalVelocity / speed, a->velocity);
        b2Vec2 old = a->position; a->position = b2MulAdd(old, dt, a->velocity); collideBodies(e, a, old, dt);
    }
    solveGroups(p, dt);
    for (int i = 0; i < arrlen(p->items); ++i) if (!(p->items[i].flags & PP_WALL)) collideBodies(e, &p->items[i], p->items[i].position, dt);
}
static bool inPolygon(const b2Vec2* vertices, int count, b2Vec2 point) {
    bool inside = false;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        b2Vec2 a = vertices[i], b = vertices[j];
        if ((a.y > point.y) != (b.y > point.y) && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}
static int finishGroup(PhysicsEngine* e) {
    PhysicsParticles* p = e->particles; if (p->shape < 0) return -1;
    b2Vec2 minimum, maximum;
    if (p->shape == PE_CIRCLE) { minimum = peVec(-p->shapeRadius, -p->shapeRadius); maximum = b2Neg(minimum); }
    else if (p->shape == PE_POLYGON && !arrlen(p->vertices)) { minimum = peVec(-p->halfWidth, -p->halfHeight); maximum = b2Neg(minimum); }
    else {
        if (arrlen(p->vertices) < 3) return -1;
        minimum = maximum = p->vertices[0];
        for (int i = 1; i < arrlen(p->vertices); ++i) { minimum = b2Min(minimum, p->vertices[i]); maximum = b2Max(maximum, p->vertices[i]); }
    }
    float stride = 1.5f * p->radius;
    int nx = (int)peClamp(peCeil((maximum.x - minimum.x) / stride), 0, 1000000);
    int ny = (int)peClamp(peCeil((maximum.y - minimum.y) / stride), 0, 1000000);
    if (!nx || !ny || (uint64_t)nx * ny > 1000000) return -1;
    int id = 0; while (id < arrlen(p->groups) && p->groups[id]) ++id;
    FluidGroup* g = (FluidGroup*)safeMalloc(sizeof(*g)); *g = p->pendingGroup;
    if (id == arrlen(p->groups)) arrput(p->groups, g); else p->groups[id] = g;
    int first = (int)arrlen(p->items);
    for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        b2Vec2 local = peVec(minimum.x + (x + 0.5f) * stride, minimum.y + (y + 0.5f) * stride);
        if (p->shape == PE_CIRCLE && b2LengthSquared(local) > p->shapeRadius * p->shapeRadius) continue;
        if (arrlen(p->vertices) && !inPolygon(p->vertices, (int)arrlen(p->vertices), local)) continue;
        FluidParticle a = p->pending; a.group = id; a.rest = local;
        b2Vec2 offset = b2RotateVector(b2MakeRot(g->angle), local); a.position = b2Add(g->position, offset);
        a.velocity = b2Add(g->velocity, b2CrossSV(g->omega, offset));
        if (particleAdd(p, a) < 0) goto populated;
    }
populated:
    if (arrlen(p->items) == first) { free(g); p->groups[id] = NULL; return -1; }
    if (p->pending.flags & PP_SPRING) {
        buildContacts(p);
        for (int i = 0; i < arrlen(p->contacts); ++i) {
            FluidContact* c = &p->contacts[i]; if (c->a < first || c->b < first) continue;
            FluidBond bond = {c->a, c->b, b2Distance(p->items[c->a].position, p->items[c->b].position), g->strength}; arrput(p->bonds, bond);
        }
    }
    float mass, inertia; groupStats(p, id, &mass, &inertia); return id;
}
double peParticleCall(PhysicsEngine* e, const char* name, const double* args, int count) {
    PhysicsParticles* p = e->particles;
#define ARG(i) peArg(args, count, i)
#define IS(s) (!strcmp(name, s))
#define POINT(i) b2MulSV(e->scale, peVec(ARG(i), ARG((i) + 1)))
    if (IS("physics_particle_create")) {
        FluidParticle a = {0}; a.group = -1; a.flags = (uint32_t)ARG(0); a.position = POINT(1); a.velocity = POINT(3);
        uint32_t color = (uint32_t)ARG(5); a.color[0] = color & 255; a.color[1] = (color >> 8) & 255; a.color[2] = (color >> 16) & 255;
        a.color[3] = (unsigned char)(255 * peClamp(ARG(6), 0, 1)); a.category = (int)ARG(7); return particleAdd(p, a);
    }
    if (IS("physics_particle_delete") || IS("physics_particle_set_flags")) {
        int i = (int)ARG(0); if (i >= 0 && i < arrlen(p->items)) { if (IS("physics_particle_delete")) p->items[i].flags |= PP_ZOMBIE; else p->items[i].flags = (uint32_t)ARG(1); } return 0;
    }
    if (IS("physics_particle_delete_region_circle") || IS("physics_particle_delete_region_box")) {
        b2Vec2 center = POINT(0); float rx = fabsf(ARG(2) * e->scale), ry = fabsf(ARG(3) * e->scale);
        for (int i = 0; i < arrlen(p->items); ++i) {
            b2Vec2 d = b2Sub(p->items[i].position, center);
            if (IS("physics_particle_delete_region_circle") ? b2LengthSquared(d) <= rx * rx : fabsf(d.x) <= rx && fabsf(d.y) <= ry) p->items[i].flags |= PP_ZOMBIE;
        }
        return 0;
    }
    if (IS("physics_particle_group_begin")) {
        memset(&p->pending, 0, sizeof(p->pending)); memset(&p->pendingGroup, 0, sizeof(p->pendingGroup));
        p->pending.flags = (uint32_t)ARG(0); p->pendingGroup.flags = (uint32_t)ARG(1); p->pendingGroup.position = POINT(2);
        p->pendingGroup.angle = -ARG(4) * PE_RAD; p->pendingGroup.velocity = POINT(5); p->pendingGroup.omega = ARG(7) * PE_RAD;
        uint32_t color = (uint32_t)ARG(8); p->pending.color[0] = color & 255; p->pending.color[1] = (color >> 8) & 255; p->pending.color[2] = (color >> 16) & 255;
        p->pending.color[3] = (unsigned char)(255 * peClamp(ARG(9), 0, 1)); p->pendingGroup.strength = peMax(0, ARG(10));
        p->pending.category = (int)ARG(11); p->shape = -1; arrsetlen(p->vertices, 0); return 0;
    }
    if (IS("physics_particle_group_circle")) { p->shape = PE_CIRCLE; p->shapeRadius = fabsf(ARG(0) * e->scale); arrsetlen(p->vertices, 0); return 0; }
    if (IS("physics_particle_group_box")) { p->shape = PE_POLYGON; p->halfWidth = fabsf(ARG(0) * e->scale); p->halfHeight = fabsf(ARG(1) * e->scale); arrsetlen(p->vertices, 0); return 0; }
    if (IS("physics_particle_group_polygon")) { p->shape = PE_POLYGON; p->halfWidth = p->halfHeight = 0; arrsetlen(p->vertices, 0); return 0; }
    if (IS("physics_particle_group_add_point")) { arrput(p->vertices, POINT(0)); return 0; }
    if (IS("physics_particle_group_end")) return finishGroup(e);
    if (IS("physics_particle_group_join")) {
        int to = (int)ARG(0), from = (int)ARG(1); FluidGroup* a = groupGet(p, to), *b = groupGet(p, from);
        if (!a || !b || a == b) return 0;
        float mass, inertia; groupStats(p, to, &mass, &inertia); groupStats(p, from, &mass, &inertia);
        for (int i = 0; i < arrlen(p->items); ++i) if (p->items[i].group == from) {
            p->items[i].group = to; p->items[i].rest = b2InvRotateVector(b2MakeRot(a->angle), b2Sub(p->items[i].position, a->position));
        }
        a->flags |= b->flags; free(b); p->groups[from] = NULL; return 0;
    }
    if (IS("physics_particle_set_category_flags")) { for (int i = 0; i < arrlen(p->items); ++i) if (ARG(0) == 0 || p->items[i].category == (int)ARG(0)) p->items[i].flags = (uint32_t)ARG(1); return 0; }
    if (IS("physics_particle_set_group_flags") || IS("physics_particle_get_group_flags")) {
        FluidGroup* g = groupGet(p, (int)ARG(0)); if (!g) return 0;
        if (IS("physics_particle_set_group_flags")) g->flags = (uint32_t)ARG(1);
        return g->flags;
    }
    if (!strncmp(name, "physics_particle_group_", 23)) {
        int id = (int)ARG(0); FluidGroup* g = groupGet(p, id); if (!g) return 0;
        float mass, inertia; groupStats(p, id, &mass, &inertia);
        if (IS("physics_particle_group_delete")) { for (int i = 0; i < arrlen(p->items); ++i) if (p->items[i].group == id) p->items[i].flags |= PP_ZOMBIE; }
        else if (IS("physics_particle_group_count")) { int n = 0; for (int i = 0; i < arrlen(p->items); ++i) if (p->items[i].group == id) ++n; return n; }
        else if (IS("physics_particle_group_get_mass")) return mass;
        else if (IS("physics_particle_group_get_inertia")) return inertia;
        else if (IS("physics_particle_group_get_centre_x")) return g->center.x / e->scale;
        else if (IS("physics_particle_group_get_centre_y")) return g->center.y / e->scale;
        else if (IS("physics_particle_group_get_vel_x")) return g->velocity.x / e->scale;
        else if (IS("physics_particle_group_get_vel_y")) return g->velocity.y / e->scale;
        else if (IS("physics_particle_group_get_ang_vel")) return g->omega / PE_RAD;
        else if (IS("physics_particle_group_get_x")) return g->position.x / e->scale;
        else if (IS("physics_particle_group_get_y")) return g->position.y / e->scale;
        else if (IS("physics_particle_group_get_angle")) return g->angle / PE_RAD;
        return 0;
    }
    if (IS("physics_particle_count")) return arrlen(p->items);
    if (IS("physics_particle_get_max_count")) return p->maxCount;
    if (IS("physics_particle_get_radius")) return p->radius / e->scale;
    if (IS("physics_particle_get_density")) return p->density;
    if (IS("physics_particle_get_damping")) return p->damping;
    if (IS("physics_particle_get_gravity_scale")) return p->gravityScale;
    if (IS("physics_particle_set_max_count")) p->maxCount = (int)peMax(0, ARG(0));
    else if (IS("physics_particle_set_radius") && ARG(0) > 0) p->radius = ARG(0) * e->scale;
    else if (IS("physics_particle_set_density") && ARG(0) > 0) p->density = ARG(0);
    else if (IS("physics_particle_set_damping")) p->damping = peMax(0, ARG(0));
    else if (IS("physics_particle_set_gravity_scale")) p->gravityScale = ARG(0);
    return 0;
#undef ARG
#undef IS
#undef POINT
}
void PhysicsEngine_polygon(PhysicsEngine* e, const float* xy, int count, int deleteRegion) {
    if (!e || !deleteRegion || count < 3 || count > B2_MAX_POLYGON_VERTICES) return;
    b2Vec2 vertices[B2_MAX_POLYGON_VERTICES]; for (int i = 0; i < count; ++i) vertices[i] = b2MulSV(e->scale, peVec(xy[i * 2], xy[i * 2 + 1]));
    for (int i = 0; i < arrlen(e->particles->items); ++i) if (inPolygon(vertices, count, e->particles->items[i].position)) e->particles->items[i].flags |= PP_ZOMBIE;
}
int PhysicsEngine_particleData(PhysicsEngine* e, int index, int group, int flags, void* buffer, int capacity) {
    if (!e || (group >= 0 && !groupGet(e->particles, group)) || index >= arrlen(e->particles->items)) return 0;
    PhysicsParticles* p = e->particles; int count = 0;
    for (int i = 0; i < arrlen(p->items); ++i) if ((index < 0 || index == i) && (group < 0 || p->items[i].group == group)) ++count;
    int stride = ((flags & 1) ? 4 : 0) + ((flags & 2) ? 8 : 0) + ((flags & 4) ? 8 : 0) + ((flags & 8) ? 4 : 0) + ((flags & 16) ? 4 : 0);
    if (stride && count > INT_MAX / stride) return 0;
    int size = stride * count; if (!buffer || capacity < size) return size;
    unsigned char* out = (unsigned char*)buffer;
#define PUT(v) do { memcpy(out, &(v), 4); out += 4; } while (0)
    for (int i = 0; i < arrlen(p->items); ++i) {
        FluidParticle* a = &p->items[i]; if ((index >= 0 && index != i) || (group >= 0 && a->group != group)) continue;
        if (flags & 1) PUT(a->flags);
        if (flags & 2) { b2Vec2 v = b2MulSV(1 / e->scale, a->position); PUT(v.x); PUT(v.y); }
        if (flags & 4) { b2Vec2 v = b2MulSV(1 / e->scale, a->velocity); PUT(v.x); PUT(v.y); }
        if (flags & 8) { uint32_t color = ((uint32_t)a->color[3] << 24) | ((uint32_t)a->color[0] << 16) | ((uint32_t)a->color[1] << 8) | a->color[2]; PUT(color); }
        if (flags & 16) { int32_t category = a->category; PUT(category); }
    }
#undef PUT
    return size;
}
void PhysicsEngine_drawParticles(PhysicsEngine* e, int mask, int category, void (*draw)(void*, float, float, int, float), void* context) {
    if (!e || !draw) return;
    for (int i = 0; i < arrlen(e->particles->items); ++i) {
        FluidParticle* p = &e->particles->items[i];
        if ((p->flags == 0 || (p->flags & (uint32_t)mask)) && (!category || p->category == category)) {
            int color = p->color[0] | (p->color[1] << 8) | (p->color[2] << 16);
            draw(context, p->position.x / e->scale, p->position.y / e->scale, color, p->color[3] / 255.0f);
        }
    }
}
void peParticlesDebug(PhysicsEngine* e) {
    for (int i = 0; i < arrlen(e->particles->items); ++i) peDrawCircle(e, e->particles->items[i].position, e->particles->radius);
}
