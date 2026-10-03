#include "physics_internal.h"

static bool sameShape(b2ShapeId a, b2ShapeId b) { return B2_ID_EQUALS(a, b); }
static PhysicsFixture* fixtureGet(PhysicsResources* r, int id) {
    return id >= 0 && id < arrlen(r->fixtures) ? r->fixtures[id] : NULL;
}
static void fixtureFree(PhysicsFixture* f) { if (f) { arrfree(f->vertices); free(f); } }
static void ensureMass(b2BodyId id) {
    // liquidfun gives massless dynamic bodies a unit mass rather than zero inverse mass
    if (b2Body_GetType(id) == b2_dynamicBody && b2Body_GetMass(id) <= 0) {
        b2MassData mass = {1, {0, 0}, 0}; b2Body_SetMassData(id, mass);
    }
}
static bool filter(b2ShapeId a, b2ShapeId b, void* context) {
    PhysicsEngine* e = context;
    PhysicsBoundFixture* fa = b2Shape_GetUserData(a);
    PhysicsBoundFixture* fb = b2Shape_GetUserData(b);
    if (!fa || !fb) return false;
    PhysicsBody* pa = fa->owner; PhysicsBody* pb = fb->owner;
    if (b2Body_GetType(pa->id) != b2_dynamicBody && b2Body_GetType(pb->id) != b2_dynamicBody) return false;
    for (int i = 0; i < arrlen(e->resources->joints); ++i) {
        PhysicsJoint* j = e->resources->joints[i];
        if (j && !j->collide && ((j->a == pa && j->b == pb) || (j->a == pb && j->b == pa))) return false;
    }
    if (fa->definition.group && fa->definition.group == fb->definition.group) return fa->definition.group > 0;
    return e->callbacks.filter && e->callbacks.filter(e->owner, pa->instance, pb->instance);
}
static void recordContact(PhysicsEngine* e, b2ShapeId a, b2ShapeId b, const b2Manifold* m) {
    PhysicsContact c = {0}; c.a = a; c.b = b;
    c.slice = e->contactSlice;
    if (m) {
        for (int i = 0; i < m->pointCount; ++i) if (m->points[i].separation <= 0.005f) {
            if (!c.count) c.point = m->points[i].point;
            ++c.count;
        }
        if (!c.count) return;
        c.normal = m->normal;
    }
    for (int i = 0; i < arrlen(e->contacts); ++i) {
        PhysicsContact* old = &e->contacts[i];
        if (sameShape(old->a, a) && sameShape(old->b, b)) {
            if (old->slice != c.slice) { old->count += c.count; old->sliceCount = c.count; old->slice = c.slice; }
            else if (c.count > old->sliceCount) { old->count += c.count - old->sliceCount; old->sliceCount = c.count; }
            return;
        }
    }
    c.sliceCount = c.count;
    arrput(e->contacts, c);
}
static bool preSolve(b2ShapeId a, b2ShapeId b, b2Manifold* manifold, void* context) {
    recordContact(context, a, b, manifold); return true;
}
PhysicsEngine* PhysicsEngine_create(void* owner, PhysicsCallbacks cb, float scale, float fps, PhysicsEngine* shared) {
    if (!isfinite(scale) || !isfinite(fps) || scale <= 0 || fps <= 0) return NULL;
    PhysicsEngine* e = safeCalloc(1, sizeof(*e));
    e->owner = owner; e->callbacks = cb; e->scale = scale; e->speed = e->fps = fps;
    e->timeStep = 1 / fps; e->iterations = 10;
    e->resources = shared ? shared->resources : safeCalloc(1, sizeof(*e->resources));
    ++e->resources->references;
    b2WorldDef d = b2DefaultWorldDef(); d.gravity = peVec(0, 10);
    d.workerCount = 0; // callbacks and portability fallbacks require the serial solver
    d.restitutionThreshold = 1; d.maximumLinearSpeed = 10000;
    e->world = b2CreateWorld(&d);
    if (!b2World_IsValid(e->world)) {
        if (--e->resources->references == 0) free(e->resources);
        free(e); return NULL;
    }
    b2World_SetCustomFilterCallback(e->world, filter, e);
    b2World_SetPreSolveCallback(e->world, preSolve, e);
    e->particles = peParticlesCreate();
    return e;
}
void PhysicsEngine_free(PhysicsEngine* e) {
    if (!e) return;
    for (int i = 0; i < arrlen(e->resources->joints); ++i) {
        PhysicsJoint* j = e->resources->joints[i]; if (j && j->engine == e) peDestroyJoint(e->resources, i);
    }
    for (int i = 0; i < arrlen(e->bodies); ++i) { e->bodies[i]->engine = NULL; e->bodies[i]->id = b2_nullBodyId; }
    b2DestroyWorld(e->world);
    peParticlesFree(e->particles); arrfree(e->bodies); arrfree(e->contacts);
    if (--e->resources->references == 0) {
        for (int i = 0; i < arrlen(e->resources->fixtures); ++i) fixtureFree(e->resources->fixtures[i]);
        arrfree(e->resources->fixtures); arrfree(e->resources->joints); free(e->resources);
    }
    free(e);
}
void peSync(PhysicsBody* p) {
    if (!peBodyValid(p)) return;
    float a = peAngle(p->id);
    // Box2D 3 rotations wrap, GML's phy_rotation is an accumulated angle
    p->angle += peWrap(a - peWrap(p->angle));
    b2Vec2 pos = b2Add(b2MulSV(1 / p->engine->scale, b2Body_GetPosition(p->id)), b2RotateVector(b2Body_GetRotation(p->id), p->offset));
    if (p->engine->callbacks.sync) p->engine->callbacks.sync(p->engine->owner, p->instance, pos.x, pos.y, -p->angle / PE_RAD);
}
void PhysicsEngine_step(PhysicsEngine* e, float fps) {
    if (!e || e->paused || !isfinite(fps) || fps <= 0) return;
    e->fps = fps;
    for (int i = 0; i < arrlen(e->bodies); ++i) e->bodies[i]->previous = b2Body_GetPosition(e->bodies[i]->id);
    float remaining = e->speed / fps;
    e->contactSlice = 0;
    while (remaining > 0) {
        ++e->contactSlice;
        float slice = peMin(remaining, 1) / e->speed;
        // The modern solver sub-steps instead of exposing velocity/position iteration counts.
        int subSteps = e->iterations;
        float dt = slice / subSteps;
        e->timeStep = dt;
        b2World_SetMaximumLinearSpeed(e->world, 2 / slice);
        for (int i = 0; i < arrlen(e->bodies); ++i) {
            PhysicsBody* p = e->bodies[i];
            float linear = peMax(0.000001f, 1 - slice * p->linearDamping);
            float angular = peMax(0.000001f, 1 - slice * p->angularDamping);
            b2Body_SetLinearDamping(p->id, (powf(linear, -1.0f / subSteps) - 1) / dt);
            b2Body_SetAngularDamping(p->id, (powf(angular, -1.0f / subSteps) - 1) / dt);
            float omega = b2Body_GetAngularVelocity(p->id), maximum = 0.5f * PE_PI / slice;
            if (fabsf(omega) > maximum) b2Body_SetAngularVelocity(p->id, peClamp(omega, -maximum, maximum));
        }
        for (int sub = 0; sub < subSteps; ++sub) {
            for (int i = 0; i < arrlen(e->bodies); ++i) {
                PhysicsBody* p = e->bodies[i];
                if (!b2Body_IsEnabled(p->id)) continue;
                if (b2LengthSquared(p->force) || p->torque) {
                    b2Body_ApplyForceToCenter(p->id, p->force, true); b2Body_ApplyTorque(p->id, p->torque, true);
                }
            }
            peSolveJoints(e, dt, true);
            b2World_Step(e->world, dt, 1);
            peSolveJoints(e, dt, false);
            peParticlesStep(e, dt);
            b2SensorEvents sensors = b2World_GetSensorEvents(e->world);
            for (int i = 0; i < sensors.beginCount; ++i) {
                b2SensorBeginTouchEvent* s = &sensors.beginEvents[i];
                if (b2Shape_IsValid(s->sensorShapeId) && b2Shape_IsValid(s->visitorShapeId) && filter(s->sensorShapeId, s->visitorShapeId, e))
                    recordContact(e, s->sensorShapeId, s->visitorShapeId, NULL);
            }
            for (int i = 0; i < arrlen(e->bodies); ++i) peSync(e->bodies[i]);
        }
        remaining -= 1;
    }
    for (int i = 0; i < arrlen(e->bodies); ++i) { e->bodies[i]->force = b2Vec2_zero; e->bodies[i]->torque = 0; }
    // Scripts may destroy bodies and fixtures. Revalidate ids for every queued event.
    for (int i = 0; i < arrlen(e->contacts); ++i) {
        PhysicsContact c = e->contacts[i];
        if (!b2Shape_IsValid(c.a) || !b2Shape_IsValid(c.b)) continue;
        PhysicsBoundFixture* a = b2Shape_GetUserData(c.a); PhysicsBoundFixture* b = b2Shape_GetUserData(c.b);
        if (a && b && e->callbacks.contact) e->callbacks.contact(e->owner, a->owner->instance, b->owner->instance, c.count,
            c.point.x / e->scale, c.point.y / e->scale, c.normal.x, c.normal.y);
    }
    arrsetlen(e->contacts, 0);
}
static bool validFixture(const PhysicsFixture* f) {
    int n = (int)arrlen(f->vertices);
    if (f->shape == PE_CIRCLE) return f->radius > 0;
    if (f->shape == PE_POLYGON) {
        if (!n) return f->width > 0 && f->height > 0;
        if (n < 3 || n > B2_MAX_POLYGON_VERTICES) return false;
        b2Hull hull = b2ComputeHull(f->vertices, n); return hull.count >= 3;
    }
    if (f->shape != PE_EDGE && f->shape != PE_CHAIN) return false;
    if (n < (f->shape == PE_CHAIN && f->loop ? 3 : 2)) return false;
    for (int i = 1; i < n; ++i) if (b2DistanceSquared(f->vertices[i - 1], f->vertices[i]) < 0.000025f) return false;
    return !f->loop || b2DistanceSquared(f->vertices[0], f->vertices[n - 1]) >= 0.000025f;
}
static PhysicsBoundFixture* bindFixture(PhysicsBody* p, const PhysicsFixture* source, b2Vec2 offset) {
    PhysicsBoundFixture* f = safeCalloc(1, sizeof(*f)); f->owner = p; f->definition = *source; f->offset = offset;
    f->definition.vertices = NULL;
    for (int i = 0; i < arrlen(source->vertices); ++i) arrput(f->definition.vertices, source->vertices[i]);
    b2ShapeDef d = b2DefaultShapeDef(); d.userData = f; d.density = source->density;
    d.material.friction = source->friction; d.material.restitution = source->restitution;
    d.filter.groupIndex = source->group; d.isSensor = source->sensor;
    d.enableSensorEvents = true; d.enablePreSolveEvents = true; d.invokeContactCreation = true;
    if (source->shape == PE_CIRCLE) {
        b2Circle c = {offset, source->radius}; b2ShapeId id = b2CreateCircleShape(p->id, &d, &c); arrput(f->shapes, id);
    } else if (source->shape == PE_POLYGON) {
        b2Polygon poly;
        if (!arrlen(source->vertices)) { poly = b2MakeOffsetBox(source->width, source->height, offset, b2Rot_identity); poly.radius = 0.01f; }
        else {
            b2Vec2 vertices[B2_MAX_POLYGON_VERTICES];
            for (int i = 0; i < arrlen(source->vertices); ++i) vertices[i] = b2Add(source->vertices[i], offset);
            b2Hull hull = b2ComputeHull(vertices, (int)arrlen(source->vertices)); poly = b2MakePolygon(&hull, 0.01f);
        }
        b2ShapeId id = b2CreatePolygonShape(p->id, &d, &poly); arrput(f->shapes, id);
    } else {
        // legacy chains support 2 point open chains and sensors, and modern chains do not, so
        // bind all edges as one GML fixture, retaining the original fixture indexing.
        int n = (int)arrlen(source->vertices), edges = source->loop ? n : n - 1;
        for (int i = 0; i < edges; ++i) {
            b2Segment segment = {b2Add(source->vertices[i], offset), b2Add(source->vertices[(i + 1) % n], offset)};
            b2ShapeId id = b2CreateSegmentShape(p->id, &d, &segment); arrput(f->shapes, id);
        }
    }
    ensureMass(p->id);
    return f;
}
PhysicsBody* PhysicsEngine_body(PhysicsEngine* e, void* inst, int index, float x, float y, float angle, float xo, float yo, int visualOffset) {
    if (!e) return NULL;
    PhysicsFixture* f = fixtureGet(e->resources, index); if (!f || !validFixture(f)) return NULL;
    PhysicsBody* p = NULL;
    for (int i = 0; i < arrlen(e->bodies); ++i) if (e->bodies[i]->instance == inst) { p = e->bodies[i]; break; }
    b2BodyType type = f->density > 0 ? b2_dynamicBody : f->kinematic ? b2_kinematicBody : b2_staticBody;
    b2Rot rotation = b2MakeRot(-angle * PE_RAD);
    if (!p) {
        p = safeCalloc(1, sizeof(*p)); p->engine = e; p->instance = inst; p->enabled = true; p->lastFixture = -1;
        p->offset = visualOffset ? peVec(xo, yo) : b2Vec2_zero; p->angle = -angle * PE_RAD;
        p->linearDamping = f->linearDamping; p->angularDamping = f->angularDamping;
        b2BodyDef d = b2DefaultBodyDef(); d.type = type; d.rotation = rotation; d.userData = p;
        d.position = b2MulSV(e->scale, b2Sub(peVec(x, y), b2RotateVector(rotation, p->offset)));
        d.linearDamping = f->linearDamping; d.angularDamping = f->angularDamping; d.isAwake = f->awake;
        p->id = b2CreateBody(e->world, &d); p->previous = d.position; arrput(e->bodies, p);
    } else if (!b2Body_GetShapeCount(p->id)) b2Body_SetType(p->id, type);
    b2Vec2 offset = visualOffset ? b2Vec2_zero : b2RotateVector(rotation, b2MulSV(e->scale, peVec(xo, yo)));
    PhysicsBoundFixture* bound = bindFixture(p, f, offset);
    int slot = 0; while (slot < arrlen(p->fixtures) && p->fixtures[slot]) ++slot;
    if (slot == arrlen(p->fixtures)) arrput(p->fixtures, bound); else p->fixtures[slot] = bound;
    p->lastFixture = slot; peSync(p); return p;
}
static void boundFree(PhysicsBoundFixture* f, bool destroy) {
    if (!f) return;
    if (destroy) {
        for (int i = 0; i < arrlen(f->shapes); ++i) if (b2Shape_IsValid(f->shapes[i])) b2DestroyShape(f->shapes[i], true);
        ensureMass(f->owner->id);
    }
    arrfree(f->shapes); arrfree(f->definition.vertices); free(f);
}
void PhysicsEngine_destroyBody(PhysicsBody* p) {
    if (!p) return;
    if (peBodyValid(p)) {
        PhysicsEngine* e = p->engine;
        for (int i = 0; i < arrlen(e->resources->joints); ++i) {
            PhysicsJoint* j = e->resources->joints[i]; if (j && (j->a == p || j->b == p)) peDestroyJoint(e->resources, i);
        }
        b2DestroyBody(p->id);
        for (int i = 0; i < arrlen(e->bodies); ++i) if (e->bodies[i] == p) { arrdel(e->bodies, i); break; }
    }
    for (int i = 0; i < arrlen(p->fixtures); ++i) boundFree(p->fixtures[i], false);
    arrfree(p->fixtures); free(p);
}
void PhysicsEngine_transform(PhysicsBody* p, float x, float y, float angle, int active) {
    if (!peBodyValid(p)) return;
    b2Rot q = b2MakeRot(-angle * PE_RAD);
    b2Vec2 pos = b2MulSV(p->engine->scale, b2Sub(peVec(x, y), b2RotateVector(q, p->offset)));
    if (b2DistanceSquared(pos, b2Body_GetPosition(p->id)) > 1e-12f || fabsf(peWrap(-angle * PE_RAD - peAngle(p->id))) > 1e-6f) {
        p->angle = -angle * PE_RAD; b2Body_SetTransform(p->id, pos, q); b2Body_SetAwake(p->id, true);
    }
    bool enabled = active && p->enabled;
    if (enabled != b2Body_IsEnabled(p->id)) { if (enabled) b2Body_Enable(p->id); else b2Body_Disable(p->id); }
}
PhysicsBody* PhysicsEngine_rehome(PhysicsBody* old, PhysicsEngine* e) {
    if (!old || old->engine == e) return old;
    if (!e || !peBodyValid(old)) { PhysicsEngine_destroyBody(old); return NULL; }
    PhysicsBody* p = safeCalloc(1, sizeof(*p)); *p = *old; p->engine = e; p->fixtures = NULL;
    float ratio = e->scale / old->engine->scale;
    b2BodyDef d = b2DefaultBodyDef(); d.type = b2Body_GetType(old->id); d.position = b2MulSV(ratio, b2Body_GetPosition(old->id));
    d.rotation = b2Body_GetRotation(old->id); d.linearVelocity = b2MulSV(ratio, b2Body_GetLinearVelocity(old->id));
    d.angularVelocity = b2Body_GetAngularVelocity(old->id); d.linearDamping = old->linearDamping; d.angularDamping = old->angularDamping;
    d.isAwake = b2Body_IsAwake(old->id); d.isEnabled = b2Body_IsEnabled(old->id);
    d.fixedRotation = b2Body_IsFixedRotation(old->id); d.isBullet = b2Body_IsBullet(old->id); d.userData = p;
    p->id = b2CreateBody(e->world, &d); p->previous = d.position;
    for (int i = 0; i < arrlen(old->fixtures); ++i) {
        PhysicsBoundFixture* f = old->fixtures[i]; PhysicsBoundFixture* copy = NULL;
        if (f) {
            PhysicsFixture scaled = f->definition; scaled.vertices = NULL;
            scaled.radius *= ratio; scaled.width *= ratio; scaled.height *= ratio;
            for (int j = 0; j < arrlen(f->definition.vertices); ++j) arrput(scaled.vertices, b2MulSV(ratio, f->definition.vertices[j]));
            copy = bindFixture(p, &scaled, b2MulSV(ratio, f->offset)); arrfree(scaled.vertices);
        }
        arrput(p->fixtures, copy);
    }
    arrput(e->bodies, p); PhysicsEngine_destroyBody(old); return p;
}
double PhysicsEngine_variable(PhysicsBody* p, int field, double value, int write, float fps) {
    if (!peBodyValid(p)) return 0;
    b2BodyId id = p->id; float s = p->engine->scale, v = (float)value;
    b2Vec2 position = b2Body_GetPosition(id), velocity = b2Body_GetLinearVelocity(id);
    if (write) {
        switch (field) {
        case PHY_ROTATION: p->angle = v * PE_RAD; b2Body_SetTransform(id, position, b2MakeRot(p->angle)); break;
        case PHY_POSITION_X: position.x = v * s; b2Body_SetTransform(id, position, b2Body_GetRotation(id)); break;
        case PHY_POSITION_Y: position.y = v * s; b2Body_SetTransform(id, position, b2Body_GetRotation(id)); break;
        case PHY_ANGULAR_VELOCITY: b2Body_SetAngularVelocity(id, v * PE_RAD); break;
        case PHY_LINEAR_VELOCITY_X: velocity.x = v * s; b2Body_SetLinearVelocity(id, velocity); break;
        case PHY_LINEAR_VELOCITY_Y: velocity.y = v * s; b2Body_SetLinearVelocity(id, velocity); break;
        case PHY_SPEED_X: velocity.x = v * s * fps; b2Body_SetLinearVelocity(id, velocity); break;
        case PHY_SPEED_Y: velocity.y = v * s * fps; b2Body_SetLinearVelocity(id, velocity); break;
        case PHY_ANGULAR_DAMPING: p->angularDamping = peMax(0, v); b2Body_SetAngularDamping(id, p->angularDamping); break;
        case PHY_LINEAR_DAMPING: p->linearDamping = peMax(0, v); b2Body_SetLinearDamping(id, p->linearDamping); break;
        case PHY_BULLET: b2Body_SetBullet(id, v != 0); break;
        case PHY_FIXED_ROTATION: b2Body_SetFixedRotation(id, v != 0); break;
        case PHY_ACTIVE: p->enabled = v != 0; if (p->enabled) b2Body_Enable(id); else b2Body_Disable(id); break;
        case PHY_POSITION_XPREVIOUS: p->previous.x = v * s; break;
        case PHY_POSITION_YPREVIOUS: p->previous.y = v * s; break;
        default: return 0;
        }
        b2Body_SetAwake(id, true); peSync(p); return value;
    }
    switch (field) {
    case PHY_ROTATION: return p->angle / PE_RAD;
    case PHY_POSITION_X: return position.x / s; case PHY_POSITION_Y: return position.y / s;
    case PHY_ANGULAR_VELOCITY: return b2Body_GetAngularVelocity(id) / PE_RAD;
    case PHY_LINEAR_VELOCITY_X: return velocity.x / s; case PHY_LINEAR_VELOCITY_Y: return velocity.y / s;
    case PHY_SPEED: return b2Length(velocity) / (s * fps);
    case PHY_SPEED_X: return velocity.x / (s * fps); case PHY_SPEED_Y: return velocity.y / (s * fps);
    case PHY_ANGULAR_DAMPING: return p->angularDamping; case PHY_LINEAR_DAMPING: return p->linearDamping;
    case PHY_BULLET: return b2Body_IsBullet(id); case PHY_FIXED_ROTATION: return b2Body_IsFixedRotation(id);
    case PHY_ACTIVE: return b2Body_IsEnabled(id); case PHY_MASS: return b2Body_GetMass(id);
    case PHY_INERTIA: { b2MassData m = b2Body_GetMassData(id); return m.rotationalInertia + m.mass * b2LengthSquared(m.center); }
    case PHY_COM_X: return b2Body_GetWorldCenterOfMass(id).x / s; case PHY_COM_Y: return b2Body_GetWorldCenterOfMass(id).y / s;
    case PHY_DYNAMIC: return b2Body_GetType(id) == b2_dynamicBody; case PHY_KINEMATIC: return b2Body_GetType(id) == b2_kinematicBody;
    case PHY_SLEEPING: return !b2Body_IsAwake(id);
    case PHY_POSITION_XPREVIOUS: return p->previous.x / s; case PHY_POSITION_YPREVIOUS: return p->previous.y / s;
    default: return 0;
    }
}
static b2ShapeProxy shapeProxy(b2ShapeId id) {
    b2ShapeProxy p = {0}; b2ShapeType t = b2Shape_GetType(id);
    if (t == b2_circleShape) { b2Circle c = b2Shape_GetCircle(id); p.points[0] = c.center; p.count = 1; p.radius = c.radius; }
    else if (t == b2_polygonShape) { b2Polygon poly = b2Shape_GetPolygon(id); p.count = poly.count; p.radius = poly.radius; memcpy(p.points, poly.vertices, p.count * sizeof(b2Vec2)); }
    else { b2Segment s = b2Shape_GetSegment(id); p.points[0] = s.point1; p.points[1] = s.point2; p.count = 2; }
    return p;
}
int PhysicsEngine_overlap(PhysicsBody* a, PhysicsBody* b, float x, float y, float angle) {
    if (!peBodyValid(a) || !peBodyValid(b) || a->engine != b->engine) return 0;
    b2Rot q = b2MakeRot(angle * PE_RAD);
    b2Transform t = {b2MulSV(a->engine->scale, b2Sub(peVec(x, y), b2RotateVector(q, a->offset))), q};
    for (int i = 0; i < arrlen(a->fixtures); ++i) if (a->fixtures[i]) for (int j = 0; j < arrlen(b->fixtures); ++j) if (b->fixtures[j]) {
        for (int u = 0; u < arrlen(a->fixtures[i]->shapes); ++u) for (int v = 0; v < arrlen(b->fixtures[j]->shapes); ++v) {
            b2DistanceInput d = {0}; d.proxyA = shapeProxy(a->fixtures[i]->shapes[u]); d.proxyB = shapeProxy(b->fixtures[j]->shapes[v]);
            d.transformA = t; d.transformB = b2Body_GetTransform(b->id); d.useRadii = true;
            b2SimplexCache cache = {0}; b2DistanceOutput out = b2ShapeDistance(&d, &cache, NULL, 0);
            if (out.distance < 1e-6f) return 1;
        }
    }
    return 0;
}
int PhysicsEngine_raycast(PhysicsBody* p, float x1, float y1, float x2, float y2, float fraction, float* result) {
    if (!peBodyValid(p) || fraction < 0) return 0;
    b2RayCastInput d = {b2MulSV(p->engine->scale, peVec(x1, y1)), b2MulSV(p->engine->scale, peVec(x2 - x1, y2 - y1)), fraction};
    bool hit = false;
    for (int i = 0; i < arrlen(p->fixtures); ++i) if (p->fixtures[i]) for (int j = 0; j < arrlen(p->fixtures[i]->shapes); ++j) {
        b2CastOutput out = b2Shape_RayCast(p->fixtures[i]->shapes[j], &d);
        if (out.hit && (!hit || out.fraction < result[0])) { hit = true; result[0] = out.fraction; result[1] = out.normal.x; result[2] = out.normal.y; }
    }
    return hit;
}
void peDrawLine(PhysicsEngine* e, b2Vec2 a, b2Vec2 b) { if (e->callbacks.line) e->callbacks.line(e->owner, a.x / e->scale, a.y / e->scale, b.x / e->scale, b.y / e->scale); }
void peDrawCircle(PhysicsEngine* e, b2Vec2 center, float radius) {
    b2Vec2 prev = b2Add(center, peVec(radius, 0));
    for (int i = 1; i <= 32; ++i) { float a = 2 * PE_PI * i / 32; b2Vec2 p = b2Add(center, peVec(radius * cosf(a), radius * sinf(a))); peDrawLine(e, prev, p); prev = p; }
}
static void debugPolygon(const b2Vec2* v, int n, b2HexColor color, void* context) {
    (void)color; for (int i = 0; i < n; ++i) peDrawLine(context, v[i], v[(i + 1) % n]);
}
static void debugSolidPolygon(b2Transform t, const b2Vec2* v, int n, float radius, b2HexColor color, void* context) {
    (void)radius; (void)color; for (int i = 0; i < n; ++i) peDrawLine(context, b2TransformPoint(t, v[i]), b2TransformPoint(t, v[(i + 1) % n]));
}
static void debugCircle(b2Vec2 c, float r, b2HexColor color, void* context) { (void)color; peDrawCircle(context, c, r); }
static void debugSolidCircle(b2Transform t, float r, b2HexColor color, void* context) { debugCircle(t.p, r, color, context); }
static void debugSegment(b2Vec2 a, b2Vec2 b, b2HexColor color, void* context) { (void)color; peDrawLine(context, a, b); }
static void debugTransform(b2Transform t, void* context) { peDrawLine(context, t.p, b2Add(t.p, b2RotateVector(t.q, peVec(0.4f, 0)))); peDrawLine(context, t.p, b2Add(t.p, b2RotateVector(t.q, peVec(0, 0.4f)))); }
static void debugPoint(b2Vec2 p, float size, b2HexColor color, void* context) { (void)size; debugCircle(p, 0.01f, color, context); }
static void debugString(b2Vec2 p, const char* text, b2HexColor color, void* context) { (void)p; (void)text; (void)color; (void)context; }
static void drawBody(PhysicsBody* p) {
    b2Transform t = b2Body_GetTransform(p->id);
    for (int i = 0; i < arrlen(p->fixtures); ++i) if (p->fixtures[i]) for (int j = 0; j < arrlen(p->fixtures[i]->shapes); ++j) {
        b2ShapeId id = p->fixtures[i]->shapes[j]; b2ShapeType type = b2Shape_GetType(id);
        if (type == b2_circleShape) { b2Circle c = b2Shape_GetCircle(id); peDrawCircle(p->engine, b2TransformPoint(t, c.center), c.radius); }
        else if (type == b2_polygonShape) { b2Polygon poly = b2Shape_GetPolygon(id); debugSolidPolygon(t, poly.vertices, poly.count, 0, 0, p->engine); }
        else { b2Segment s = b2Shape_GetSegment(id); peDrawLine(p->engine, b2TransformPoint(t, s.point1), b2TransformPoint(t, s.point2)); }
    }
}
double PhysicsEngine_call(PhysicsEngine* e, const char* name, PhysicsBody* a, PhysicsBody* b, const double* args, int count) {
    if (!e) return -1;
#define ARG(i) peArg(args, count, i)
#define IS(s) (!strcmp(name, s))
#define VEC(i) peVec(ARG(i), ARG((i) + 1))
#define POINT(i) b2MulSV(e->scale, VEC(i))
    if (IS("physics_world_create")) { if (ARG(0) > 0) e->scale = ARG(0); e->speed = e->fps; return 0; }
    if (IS("physics_world_gravity")) { b2World_SetGravity(e->world, VEC(0)); return 0; }
    if (IS("physics_world_update_speed")) { if (ARG(0) > 0) e->speed = ARG(0); return 0; }
    if (IS("physics_world_update_iterations")) { e->iterations = (int)peClamp(ARG(0), 1, 256); return 0; }
    if (IS("physics_pause_enable")) { e->paused = ARG(0) != 0; return 0; }
    if (IS("physics_debug")) return 0;
    if (IS("physics_world_draw_debug")) {
        int flags = (int)ARG(0); b2DebugDraw d = b2DefaultDebugDraw(); d.context = e;
        d.DrawPolygonFcn = debugPolygon; d.DrawSolidPolygonFcn = debugSolidPolygon; d.DrawCircleFcn = debugCircle;
        d.DrawSolidCircleFcn = debugSolidCircle; d.DrawSegmentFcn = debugSegment; d.DrawTransformFcn = debugTransform;
        d.DrawPointFcn = debugPoint; d.DrawStringFcn = debugString;
        d.drawShapes = (flags & 1) != 0; d.drawJoints = (flags & 2) != 0; d.drawMass = (flags & 4) != 0;
        d.drawBounds = (flags & 8) != 0; d.drawContacts = (flags & 64) != 0; b2World_Draw(e->world, &d);
        if (flags & 1) peParticlesDebug(e);
        if (flags & 2) for (int i = 0; i < arrlen(e->resources->joints); ++i) {
            PhysicsJoint* j = e->resources->joints[i]; if (!j || j->engine != e || j->type < PE_ROPE) continue;
            b2Vec2 pa = b2Body_GetWorldPoint(j->a->id, j->anchorA), pb = b2Body_GetWorldPoint(j->b->id, j->anchorB);
            if (j->type == PE_PULLEY) { peDrawLine(e, pa, j->groundA); peDrawLine(e, pb, j->groundB); peDrawLine(e, j->groundA, j->groundB); }
            else peDrawLine(e, pa, pb);
        }
        return 0;
    }
    if (IS("physics_fixture_index")) return a ? a->lastFixture : -1;
    if (IS("physics_fixture_create")) {
        PhysicsFixture* f = safeCalloc(1, sizeof(*f)); f->shape = -1; f->friction = 0.2f; f->awake = true;
        int index = 0; while (index < arrlen(e->resources->fixtures) && e->resources->fixtures[index]) ++index;
        if (index == arrlen(e->resources->fixtures)) arrput(e->resources->fixtures, f); else e->resources->fixtures[index] = f;
        return index;
    }
    if (!strncmp(name, "physics_fixture_", 16)) {
        int index = (int)ARG(0); PhysicsFixture* f = fixtureGet(e->resources, index); if (!f) return -1;
        if (IS("physics_fixture_delete")) { fixtureFree(f); e->resources->fixtures[index] = NULL; }
        else if (IS("physics_fixture_set_density")) f->density = peMax(0, ARG(1));
        else if (IS("physics_fixture_set_friction")) f->friction = peMax(0, ARG(1));
        else if (IS("physics_fixture_set_restitution")) f->restitution = peMax(0, ARG(1));
        else if (IS("physics_fixture_set_sensor")) f->sensor = ARG(1) != 0;
        else if (IS("physics_fixture_set_collision_group")) f->group = (int16_t)ARG(1);
        else if (IS("physics_fixture_set_linear_damping")) f->linearDamping = peMax(0, ARG(1));
        else if (IS("physics_fixture_set_angular_damping")) f->angularDamping = peMax(0, ARG(1));
        else if (IS("physics_fixture_set_awake")) f->awake = ARG(1) != 0;
        else if (IS("physics_fixture_set_kinematic")) f->kinematic = true;
        else if (IS("physics_fixture_set_circle_shape")) { f->shape = PE_CIRCLE; f->radius = ARG(1) * e->scale; arrsetlen(f->vertices, 0); }
        else if (IS("physics_fixture_set_box_shape")) { f->shape = PE_POLYGON; f->width = ARG(1) * e->scale; f->height = ARG(2) * e->scale; arrsetlen(f->vertices, 0); }
        else if (IS("physics_fixture_set_polygon_shape")) { f->shape = PE_POLYGON; f->width = f->height = 0; arrsetlen(f->vertices, 0); }
        else if (IS("physics_fixture_set_chain_shape")) { f->shape = PE_CHAIN; f->loop = ARG(1) != 0; arrsetlen(f->vertices, 0); }
        else if (IS("physics_fixture_set_edge_shape")) { f->shape = PE_EDGE; arrsetlen(f->vertices, 0); arrput(f->vertices, POINT(1)); arrput(f->vertices, POINT(3)); }
        else if (IS("physics_fixture_add_point")) arrput(f->vertices, POINT(1));
        return 0;
    }
    if (!strncmp(name, "physics_joint_", 14)) return peJointCall(e, name, a, b, args, count);
    if (!strncmp(name, "physics_particle_", 17)) return peParticleCall(e, name, args, count);
    if (!peBodyValid(a)) return 0;
    if (IS("physics_apply_force") || IS("physics_apply_impulse") || IS("physics_apply_local_force") || IS("physics_apply_local_impulse")) {
        b2Vec2 point = POINT(0), force = VEC(2);
        if (IS("physics_apply_local_force") || IS("physics_apply_local_impulse")) { point = b2Body_GetWorldPoint(a->id, point); force = b2Body_GetWorldVector(a->id, force); }
        if (IS("physics_apply_impulse") || IS("physics_apply_local_impulse")) b2Body_ApplyLinearImpulse(a->id, force, point, true);
        else { a->force = b2Add(a->force, force); a->torque += b2Cross(b2Sub(point, b2Body_GetWorldCenterOfMass(a->id)), force); b2Body_SetAwake(a->id, true); }
        return 0;
    }
    if (IS("physics_apply_angular_impulse")) { b2Body_ApplyAngularImpulse(a->id, ARG(0), true); return 0; }
    if (IS("physics_apply_torque")) { a->torque += ARG(0); b2Body_SetAwake(a->id, true); return 0; }
    if (IS("physics_mass_properties")) {
        b2MassData m = {ARG(0), POINT(1), ARG(3)};
        if (m.rotationalInertia > 0) m.rotationalInertia -= m.mass * b2LengthSquared(m.center);
        if (b2Body_IsFixedRotation(a->id)) m.rotationalInertia = 0;
        if (m.mass > 0 && m.rotationalInertia >= 0 && b2Body_GetType(a->id) == b2_dynamicBody) {
            b2Vec2 center = b2Body_GetWorldCenterOfMass(a->id), velocity = b2Body_GetLinearVelocity(a->id);
            b2Body_SetMassData(a->id, m);
            b2Vec2 shift = b2Sub(b2Body_GetWorldCenterOfMass(a->id), center);
            b2Body_SetLinearVelocity(a->id, b2Add(velocity, b2CrossSV(b2Body_GetAngularVelocity(a->id), shift)));
        }
        return 0;
    }
    if (IS("physics_draw_debug")) { drawBody(a); return 0; }
    int index = (int)ARG(0);
    PhysicsBoundFixture* f = index >= 0 && index < arrlen(a->fixtures) ? a->fixtures[index] : NULL;
    if (!f) return 0;
    if (IS("physics_remove_fixture")) { boundFree(f, true); a->fixtures[index] = NULL; return 0; }
    if (IS("physics_get_density")) return f->definition.density;
    if (IS("physics_get_friction")) return f->definition.friction;
    if (IS("physics_get_restitution")) return f->definition.restitution;
    float value = peMax(0, ARG(1));
    if (IS("physics_set_density")) f->definition.density = value;
    if (IS("physics_set_friction")) f->definition.friction = value;
    if (IS("physics_set_restitution")) f->definition.restitution = value;
    for (int i = 0; i < arrlen(f->shapes); ++i) {
        if (IS("physics_set_density")) b2Shape_SetDensity(f->shapes[i], value, true);
        if (IS("physics_set_friction")) b2Shape_SetFriction(f->shapes[i], value);
        if (IS("physics_set_restitution")) b2Shape_SetRestitution(f->shapes[i], value);
    }
    if (IS("physics_set_density")) ensureMass(a->id);
    return 0;
#undef ARG
#undef IS
#undef VEC
#undef POINT
}
