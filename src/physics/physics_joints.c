#include "physics_internal.h"
#include <float.h>

/* legacy joints removed from Box2D 3 are solved as Jacobiban impulse constraints.
 * they operate on the same C bodies, including their actual centres of mass. */
typedef struct { PhysicsBody* body; b2Vec2 linear; float angular; } Gradient;
typedef struct { Gradient terms[4]; int count; } Jacobian;
static float inverseMass(PhysicsBody* p) {
    if (b2Body_GetType(p->id) != b2_dynamicBody) return 0;
    float m = b2Body_GetMass(p->id); return m > 0 ? 1 / m : 0;
}
static float inverseInertia(PhysicsBody* p) {
    if (b2Body_GetType(p->id) != b2_dynamicBody || b2Body_IsFixedRotation(p->id)) return 0;
    float inertia = b2Body_GetRotationalInertia(p->id); return inertia > 0 ? 1 / inertia : 0;
}
static void addGradient(Jacobian* c, PhysicsBody* p, b2Vec2 linear, float angular) {
    for (int i = 0; i < c->count; ++i) if (c->terms[i].body == p) {
        c->terms[i].linear = b2Add(c->terms[i].linear, linear); c->terms[i].angular += angular; return;
    }
    require(c->count < 4);
    c->terms[c->count++] = (Gradient){p, linear, angular};
}
static void applyGradient(const Jacobian* c, float impulse, bool position) {
    if (fabsf(impulse) < 1e-9f) return;
    for (int i = 0; i < c->count; ++i) {
        const Gradient* g = &c->terms[i]; PhysicsBody* p = g->body;
        float im = inverseMass(p), ii = inverseInertia(p);
        if (position) {
            if (!im && !ii) continue;
            b2Vec2 center = b2Body_GetWorldCenterOfMass(p->id);
            b2Rot q = b2MakeRot(peAngle(p->id) + ii * impulse * g->angular);
            center = b2MulAdd(center, im * impulse, g->linear);
            b2Vec2 origin = b2Sub(center, b2RotateVector(q, b2Body_GetLocalCenterOfMass(p->id)));
            b2Body_SetTransform(p->id, origin, q);
        } else {
            if (im) b2Body_ApplyLinearImpulseToCenter(p->id, b2MulSV(impulse, g->linear), true);
            if (ii) b2Body_ApplyAngularImpulse(p->id, impulse * g->angular, true);
        }
    }
}
static float constraintMass(const Jacobian* c, float* speed) {
    float mass = 0; *speed = 0;
    for (int i = 0; i < c->count; ++i) {
        const Gradient* g = &c->terms[i];
        mass += inverseMass(g->body) * b2LengthSquared(g->linear) + inverseInertia(g->body) * g->angular * g->angular;
        *speed += b2Dot(g->linear, b2Body_GetLinearVelocity(g->body->id)) + g->angular * b2Body_GetAngularVelocity(g->body->id);
    }
    return mass;
}
static float coordinate(PhysicsJoint* j, Jacobian* c, float ratio) {
    if (j->type == PE_REVOLUTE) {
        addGradient(c, j->a, b2Vec2_zero, -ratio); addGradient(c, j->b, b2Vec2_zero, ratio);
        float angleA = j->a->angle + peWrap(peAngle(j->a->id) - peWrap(j->a->angle));
        float angleB = j->b->angle + peWrap(peAngle(j->b->id) - peWrap(j->b->angle));
        return angleB - angleA - j->reference;
    }
    b2Vec2 pa = b2Body_GetWorldPoint(j->a->id, j->anchorA), pb = b2Body_GetWorldPoint(j->b->id, j->anchorB);
    b2Vec2 axis = b2Body_GetWorldVector(j->a->id, j->axis), delta = b2Sub(pb, pa);
    b2Vec2 ra = b2Sub(pa, b2Body_GetWorldCenterOfMass(j->a->id)), rb = b2Sub(pb, b2Body_GetWorldCenterOfMass(j->b->id));
    addGradient(c, j->a, b2MulSV(-ratio, axis), -ratio * b2Cross(b2Add(ra, delta), axis));
    addGradient(c, j->b, b2MulSV(ratio, axis), ratio * b2Cross(rb, axis));
    return b2Dot(axis, delta);
}
static float buildConstraint(PhysicsJoint* j, Jacobian* c) {
    if (j->type == PE_GEAR) {
        PhysicsJoint* a = peJoint(j->engine->resources, j->source1), *b = peJoint(j->engine->resources, j->source2);
        if (!a || !b) return 0;
        return coordinate(a, c, 1) + j->ratio * coordinate(b, c, j->ratio) - j->constant;
    }
    b2Vec2 pa = b2Body_GetWorldPoint(j->a->id, j->anchorA), pb = b2Body_GetWorldPoint(j->b->id, j->anchorB);
    b2Vec2 ra = b2Sub(pa, b2Body_GetWorldCenterOfMass(j->a->id)), rb = b2Sub(pb, b2Body_GetWorldCenterOfMass(j->b->id));
    if (j->type == PE_ROPE) {
        b2Vec2 delta = b2Sub(pb, pa); float length = b2Length(delta); b2Vec2 n = b2Normalize(delta);
        addGradient(c, j->a, b2Neg(n), -b2Cross(ra, n)); addGradient(c, j->b, n, b2Cross(rb, n));
        return length - j->length;
    }
    b2Vec2 da = b2Sub(pa, j->groundA), db = b2Sub(pb, j->groundB), na = b2Normalize(da), nb = b2Normalize(db);
    addGradient(c, j->a, na, b2Cross(ra, na)); addGradient(c, j->b, b2MulSV(j->ratio, nb), j->ratio * b2Cross(rb, nb));
    return b2Length(da) + j->ratio * b2Length(db) - j->constant;
}
static void solveFriction(PhysicsJoint* j, float dt) {
    b2Vec2 pa = b2Body_GetWorldPoint(j->a->id, j->anchorA), pb = b2Body_GetWorldPoint(j->b->id, j->anchorB);
    b2Vec2 ra = b2Sub(pa, b2Body_GetWorldCenterOfMass(j->a->id)), rb = b2Sub(pb, b2Body_GetWorldCenterOfMass(j->b->id));
    float ma = inverseMass(j->a), mb = inverseMass(j->b), ia = inverseInertia(j->a), ib = inverseInertia(j->b);
    float angularMass = ia + ib;
    if (angularMass > 0) {
        float impulse = -(b2Body_GetAngularVelocity(j->b->id) - b2Body_GetAngularVelocity(j->a->id)) / angularMass;
        float old = j->angularImpulse; j->angularImpulse = peClamp(old + impulse, -dt * j->maxTorque, dt * j->maxTorque);
        if (fabsf(j->angularImpulse - old) > 1e-9f) {
            b2Body_ApplyAngularImpulse(j->a->id, old - j->angularImpulse, true);
            b2Body_ApplyAngularImpulse(j->b->id, j->angularImpulse - old, true);
        }
    }
    b2Vec2 velocity = b2Sub(b2Body_GetWorldPointVelocity(j->b->id, pb), b2Body_GetWorldPointVelocity(j->a->id, pa));
    float k11 = ma + mb + ia * ra.y * ra.y + ib * rb.y * rb.y;
    float k12 = -ia * ra.x * ra.y - ib * rb.x * rb.y;
    float k22 = ma + mb + ia * ra.x * ra.x + ib * rb.x * rb.x;
    float determinant = k11 * k22 - k12 * k12;
    if (determinant <= 1e-12f) return;
    b2Vec2 impulse = peVec((-k22 * velocity.x + k12 * velocity.y) / determinant, (k12 * velocity.x - k11 * velocity.y) / determinant);
    b2Vec2 old = j->linearImpulse; j->linearImpulse = b2Add(old, impulse);
    float length = b2Length(j->linearImpulse), maximum = dt * j->maxForce;
    if (length > maximum && length > 0) j->linearImpulse = b2MulSV(maximum / length, j->linearImpulse);
    impulse = b2Sub(j->linearImpulse, old);
    if (b2LengthSquared(impulse) > 1e-18f) {
        b2Body_ApplyLinearImpulse(j->a->id, b2Neg(impulse), pa, true); b2Body_ApplyLinearImpulse(j->b->id, impulse, pb, true);
    }
    j->reaction = b2MulSV(1 / dt, j->linearImpulse);
}
void peSolveJoints(PhysicsEngine* e, float dt, bool reset) {
    if (reset) for (int i = 0; i < arrlen(e->resources->joints); ++i) {
        PhysicsJoint* j = e->resources->joints[i];
        if (j && j->engine == e) { j->impulse = j->angularImpulse = 0; j->linearImpulse = j->reaction = b2Vec2_zero; }
    }
    for (int iteration = 0; iteration < e->iterations; ++iteration) for (int i = 0; i < arrlen(e->resources->joints); ++i) {
        PhysicsJoint* j = e->resources->joints[i];
        if (!j || j->engine != e || j->type < PE_ROPE || !peBodyValid(j->a) || !peBodyValid(j->b)) continue;
        if (!b2Body_IsEnabled(j->a->id) || !b2Body_IsEnabled(j->b->id)) continue;
        if (j->type == PE_FRICTION) { solveFriction(j, dt); continue; }
        Jacobian c = {0}; float error = buildConstraint(j, &c), speed;
        float mass = constraintMass(&c, &speed); if (mass <= 1e-12f) continue;
        float bias = j->type == PE_ROPE && error < 0 ? error / dt : 0.2f * peClamp(error, -0.2f, 0.2f) / dt;
        float impulse = -(speed + bias) / mass;
        if (j->type == PE_ROPE) { float old = j->impulse; j->impulse = peMin(0, old + impulse); impulse = j->impulse - old; }
        else j->impulse += impulse;
        applyGradient(&c, impulse, false);
        if (!reset && fabsf(error) > 0.005f && (j->type != PE_ROPE || error > 0)) applyGradient(&c, -0.2f * peClamp(error, -0.2f, 0.2f) / mass, true);
        for (int k = 0; k < c.count; ++k) if (c.terms[k].body == j->b) j->reaction = b2MulSV(j->impulse / dt, c.terms[k].linear);
    }
}
void peDestroyJoint(PhysicsResources* r, int id) {
    PhysicsJoint* j = peJoint(r, id); if (!j) return;
    // Clear first so recursive destruction cannot revisit this slot.
    r->joints[id] = NULL;
    for (int i = 0; i < arrlen(r->joints); ++i) {
        PhysicsJoint* gear = r->joints[i];
        if (gear && gear->type == PE_GEAR && (gear->source1 == id || gear->source2 == id)) peDestroyJoint(r, i);
    }
    if (b2Joint_IsValid(j->id)) b2DestroyJoint(j->id);
    free(j);
}
static double jointValue(PhysicsJoint* j, int field, float value, bool write) {
    if (!j || !peBodyValid(j->a) || !peBodyValid(j->b)) return 0;
    b2JointId id = j->id;
    if (write) { b2Body_SetAwake(j->a->id, true); b2Body_SetAwake(j->b->id, true); }
    else {
        b2Vec2 a = b2Body_GetWorldPoint(j->a->id, j->anchorA), b = b2Body_GetWorldPoint(j->b->id, j->anchorB);
        b2Vec2 force = j->type < PE_ROPE ? b2Joint_GetConstraintForce(id) : j->reaction;
        switch (field) {
        case 0: return a.x; case 1: return a.y; case 2: return b.x; case 3: return b.y;
        case 4: return force.x; case 5: return force.y;
        case 6: return j->type < PE_ROPE ? b2Joint_GetConstraintTorque(id) : j->angularImpulse / j->engine->timeStep;
        }
    }
#define FIELD(n, get, set) case n: if (write) { set; return 0; } return get
    switch (j->type) {
    case PE_REVOLUTE:
        switch (field) {
        FIELD(7, b2RevoluteJoint_GetMotorSpeed(id), b2RevoluteJoint_SetMotorSpeed(id, value));
        case 8: return j->b->angle - j->a->angle - j->reference;
        case 9: return b2RevoluteJoint_GetMotorTorque(id);
        FIELD(10, b2RevoluteJoint_GetMaxMotorTorque(id), b2RevoluteJoint_SetMaxMotorTorque(id, peMax(0, value)));
        FIELD(19, b2RevoluteJoint_GetLowerLimit(id) / PE_RAD, b2RevoluteJoint_SetLimits(id, peMin(value * PE_RAD, b2RevoluteJoint_GetUpperLimit(id)), b2RevoluteJoint_GetUpperLimit(id)));
        FIELD(20, b2RevoluteJoint_GetUpperLimit(id) / PE_RAD, b2RevoluteJoint_SetLimits(id, b2RevoluteJoint_GetLowerLimit(id), peMax(value * PE_RAD, b2RevoluteJoint_GetLowerLimit(id))));
        FIELD(21, b2RevoluteJoint_IsLimitEnabled(id), b2RevoluteJoint_EnableLimit(id, value != 0));
        case 25: b2RevoluteJoint_EnableMotor(id, value != 0); return 0;
        } break;
    case PE_PRISMATIC:
        switch (field) {
        FIELD(7, b2PrismaticJoint_GetMotorSpeed(id), b2PrismaticJoint_SetMotorSpeed(id, value));
        case 11: return b2PrismaticJoint_GetTranslation(id); case 12: return b2PrismaticJoint_GetSpeed(id);
        case 13: return b2PrismaticJoint_GetMotorForce(id);
        FIELD(14, b2PrismaticJoint_GetMaxMotorForce(id), b2PrismaticJoint_SetMaxMotorForce(id, peMax(0, value)));
        case 25: b2PrismaticJoint_EnableMotor(id, value != 0); return 0;
        } break;
    case PE_WHEEL:
        switch (field) {
        FIELD(7, b2WheelJoint_GetMotorSpeed(id), b2WheelJoint_SetMotorSpeed(id, value));
        case 9: return b2WheelJoint_GetMotorTorque(id);
        FIELD(10, b2WheelJoint_GetMaxMotorTorque(id), b2WheelJoint_SetMaxMotorTorque(id, peMax(0, value)));
        case 11: { Jacobian c = {0}; return coordinate(j, &c, 1); }
        case 12: return b2Body_GetAngularVelocity(j->b->id) - b2Body_GetAngularVelocity(j->a->id);
        FIELD(17, b2WheelJoint_GetSpringDampingRatio(id), b2WheelJoint_SetSpringDampingRatio(id, peMax(0, value)));
        FIELD(18, b2WheelJoint_GetSpringHertz(id), b2WheelJoint_SetSpringHertz(id, peMax(0, value)));
        case 25: b2WheelJoint_EnableMotor(id, value != 0); return 0;
        } break;
    case PE_DISTANCE:
        switch (field) {
        FIELD(15, b2DistanceJoint_GetLength(id), b2DistanceJoint_SetLength(id, peMax(0.005f, value)));
        FIELD(17, b2DistanceJoint_GetSpringDampingRatio(id), b2DistanceJoint_SetSpringDampingRatio(id, peMax(0, value)));
        FIELD(18, b2DistanceJoint_GetSpringHertz(id), b2DistanceJoint_SetSpringHertz(id, peMax(0, value)); b2DistanceJoint_EnableSpring(id, value > 0));
        } break;
    case PE_WELD:
        switch (field) {
        case 8: return j->reference;
        FIELD(17, b2WeldJoint_GetAngularDampingRatio(id), b2WeldJoint_SetAngularDampingRatio(id, peMax(0, value)));
        FIELD(18, b2WeldJoint_GetAngularHertz(id), b2WeldJoint_SetAngularHertz(id, peMax(0, value)));
        } break;
    case PE_ROPE:
        switch (field) { FIELD(22, j->length, j->length = peMax(0, value)); } break;
    case PE_PULLEY:
        if (field == 15) return b2Distance(b2Body_GetWorldPoint(j->a->id, j->anchorA), j->groundA);
        if (field == 16) return b2Distance(b2Body_GetWorldPoint(j->b->id, j->anchorB), j->groundB);
        break;
    case PE_FRICTION:
        switch (field) { FIELD(23, j->maxTorque, j->maxTorque = peMax(0, value)); FIELD(24, j->maxForce, j->maxForce = peMax(0, value)); } break;
    default: break;
    }
#undef FIELD
    return 0;
}
double peJointCall(PhysicsEngine* e, const char* name, PhysicsBody* a, PhysicsBody* b, const double* args, int count) {
#define ARG(i) peArg(args, count, i)
#define IS(s) (!strcmp(name, s))
#define POINT(i) b2MulSV(e->scale, peVec(ARG(i), ARG((i) + 1)))
    if (IS("physics_joint_delete")) { peDestroyJoint(e->resources, (int)ARG(0)); return 0; }
    if (IS("physics_joint_get_value") || IS("physics_joint_set_value") || IS("physics_joint_enable_motor"))
        return jointValue(peJoint(e->resources, (int)ARG(0)), IS("physics_joint_enable_motor") ? 25 : (int)ARG(1), IS("physics_joint_enable_motor") ? ARG(1) : ARG(2), !IS("physics_joint_get_value"));
    if (!peBodyValid(a) || !peBodyValid(b) || a == b || a->engine != e || b->engine != e) return -1;
    PhysicsJoint* j = safeCalloc(1, sizeof(*j)); j->a = a; j->b = b; j->engine = e;
    j->source1 = j->source2 = -1; j->reference = b->angle - a->angle;
    j->anchorA = b2Body_GetLocalPoint(a->id, POINT(2));
    j->anchorB = b2Body_GetLocalPoint(b->id, POINT(2));
    if (IS("physics_joint_distance_create") || IS("physics_joint_rope_create")) {
        j->anchorB = b2Body_GetLocalPoint(b->id, POINT(4)); j->collide = ARG(IS("physics_joint_distance_create") ? 6 : 7) != 0;
        j->length = IS("physics_joint_distance_create") ? b2Distance(POINT(2), POINT(4)) : peMax(0, ARG(6) * e->scale);
        if (IS("physics_joint_rope_create")) j->type = PE_ROPE;
        else {
            j->type = PE_DISTANCE; b2DistanceJointDef d = b2DefaultDistanceJointDef();
            d.bodyIdA = a->id; d.bodyIdB = b->id; d.localAnchorA = j->anchorA; d.localAnchorB = j->anchorB;
            d.length = peMax(0.005f, j->length); d.collideConnected = j->collide; j->id = b2CreateDistanceJoint(e->world, &d);
        }
    } else if (IS("physics_joint_revolute_create")) {
        if (ARG(4) > ARG(5)) { free(j); return -1; }
        j->type = PE_REVOLUTE; j->collide = ARG(10) != 0;
        b2RevoluteJointDef d = b2DefaultRevoluteJointDef(); d.bodyIdA = a->id; d.bodyIdB = b->id;
        d.localAnchorA = j->anchorA; d.localAnchorB = j->anchorB; d.referenceAngle = peWrap(j->reference);
        d.lowerAngle = peClamp(ARG(4) * PE_RAD, -0.99f * PE_PI, 0.99f * PE_PI);
        d.upperAngle = peClamp(ARG(5) * PE_RAD, -0.99f * PE_PI, 0.99f * PE_PI);
        d.enableLimit = ARG(6) != 0; d.maxMotorTorque = peMax(0, ARG(7)); d.motorSpeed = ARG(8); d.enableMotor = ARG(9) != 0;
        d.collideConnected = j->collide; j->id = b2CreateRevoluteJoint(e->world, &d);
    } else if (IS("physics_joint_prismatic_create") || IS("physics_joint_wheel_create")) {
        b2Vec2 axis = b2Normalize(peVec(ARG(4), ARG(5))); if (!b2LengthSquared(axis)) { free(j); return -1; }
        j->axis = b2Body_GetLocalVector(a->id, axis);
        if (IS("physics_joint_prismatic_create")) {
            if (ARG(6) > ARG(7)) { free(j); return -1; }
            j->type = PE_PRISMATIC; j->collide = ARG(12) != 0;
            b2PrismaticJointDef d = b2DefaultPrismaticJointDef(); d.bodyIdA = a->id; d.bodyIdB = b->id;
            d.localAnchorA = j->anchorA; d.localAnchorB = j->anchorB; d.localAxisA = j->axis; d.referenceAngle = peWrap(j->reference);
            d.lowerTranslation = ARG(6) * e->scale; d.upperTranslation = ARG(7) * e->scale; d.enableLimit = ARG(8) != 0;
            d.maxMotorForce = peMax(0, ARG(9)); d.motorSpeed = ARG(10); d.enableMotor = ARG(11) != 0; d.collideConnected = j->collide;
            j->id = b2CreatePrismaticJoint(e->world, &d);
        } else {
            j->type = PE_WHEEL; j->collide = ARG(11) != 0;
            b2WheelJointDef d = b2DefaultWheelJointDef(); d.bodyIdA = a->id; d.bodyIdB = b->id;
            d.localAnchorA = j->anchorA; d.localAnchorB = j->anchorB; d.localAxisA = j->axis;
            d.enableMotor = ARG(6) != 0; d.maxMotorTorque = peMax(0, ARG(7)); d.motorSpeed = ARG(8);
            d.hertz = peMax(0, ARG(9)); d.dampingRatio = peMax(0, ARG(10)); d.enableSpring = d.hertz > 0; d.collideConnected = j->collide;
            j->id = b2CreateWheelJoint(e->world, &d);
        }
    } else if (IS("physics_joint_weld_create")) {
        j->type = PE_WELD; j->collide = ARG(7) != 0; j->reference = -ARG(4) * PE_RAD;
        b2WeldJointDef d = b2DefaultWeldJointDef(); d.bodyIdA = a->id; d.bodyIdB = b->id;
        d.localAnchorA = j->anchorA; d.localAnchorB = j->anchorB; d.referenceAngle = peWrap(j->reference);
        d.angularHertz = peMax(0, ARG(5)); d.angularDampingRatio = peMax(0, ARG(6)); d.linearHertz = 0; d.collideConnected = j->collide;
        j->id = b2CreateWeldJoint(e->world, &d);
    } else if (IS("physics_joint_pulley_create")) {
        if (ARG(10) <= 0) { free(j); return -1; }
        j->type = PE_PULLEY; j->groundA = POINT(2); j->groundB = POINT(4); j->anchorA = POINT(6); j->anchorB = POINT(8);
        j->ratio = ARG(10); j->collide = ARG(11) != 0;
        j->constant = b2Distance(b2Body_GetWorldPoint(a->id, j->anchorA), j->groundA) + j->ratio * b2Distance(b2Body_GetWorldPoint(b->id, j->anchorB), j->groundB);
    } else if (IS("physics_joint_gear_create")) {
        PhysicsJoint* s1 = peJoint(e->resources, (int)ARG(2)), *s2 = peJoint(e->resources, (int)ARG(3));
        if (!s1 || !s2 || s1 == s2 || s1->engine != e || s2->engine != e ||
            (s1->type != PE_REVOLUTE && s1->type != PE_PRISMATIC) || (s2->type != PE_REVOLUTE && s2->type != PE_PRISMATIC)) { free(j); return -1; }
        j->type = PE_GEAR; j->source1 = (int)ARG(2); j->source2 = (int)ARG(3); j->ratio = ARG(4);
        Jacobian c = {0}; j->constant = coordinate(s1, &c, 1) + j->ratio * coordinate(s2, &c, j->ratio);
    } else if (IS("physics_joint_friction_create")) {
        j->type = PE_FRICTION; j->maxForce = peMax(0, ARG(4)); j->maxTorque = peMax(0, ARG(5)); j->collide = ARG(6) != 0;
    } else { free(j); return -1; }
    if (j->type >= PE_ROPE && !j->collide) {
        b2FilterJointDef d = b2DefaultFilterJointDef(); d.bodyIdA = a->id; d.bodyIdB = b->id; j->id = b2CreateFilterJoint(e->world, &d);
    }
    b2Body_SetAwake(a->id, true); b2Body_SetAwake(b->id, true);
    int index = 0; while (index < arrlen(e->resources->joints) && e->resources->joints[index]) ++index;
    if (index == arrlen(e->resources->joints)) arrput(e->resources->joints, j); else e->resources->joints[index] = j;
    return index;
#undef ARG
#undef IS
#undef POINT
}
