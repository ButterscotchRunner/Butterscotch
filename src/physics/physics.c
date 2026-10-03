#include "physics.h"
#include "runner.h"
#include "vm.h"
#include "utils.h"
#include "string_compat.h"
#include "math_compat.h"
#include <stdlib.h>
#include <string.h>

static const char* const variables[] = {
    "phy_rotation", "phy_position_x", "phy_position_y", "phy_angular_velocity",
    "phy_linear_velocity_x", "phy_linear_velocity_y", "phy_speed", "phy_speed_x", "phy_speed_y",
    "phy_angular_damping", "phy_linear_damping", "phy_bullet", "phy_fixed_rotation",
    "phy_active", "phy_mass", "phy_inertia", "phy_com_x", "phy_com_y", "phy_dynamic",
    "phy_kinematic", "phy_sleeping", "phy_position_xprevious", "phy_position_yprevious",
    "phy_collision_points", "phy_collision_x", "phy_collision_y", "phy_col_normal_x", "phy_col_normal_y"
};
static const struct { const char* name; int value; } constants[] = {
    {"phy_debug_render_shapes", 1}, {"phy_debug_render_joints", 2}, {"phy_debug_render_aabb", 8},
    {"phy_debug_render_pairs", 64}, {"phy_debug_render_coms", 4}, {"phy_debug_render_obb", 16}, {"phy_debug_render_core_shapes", 32},
    {"phy_joint_anchor_1_x", 0}, {"phy_joint_anchor_1_y", 1}, {"phy_joint_anchor_2_x", 2}, {"phy_joint_anchor_2_y", 3},
    {"phy_joint_reaction_force_x", 4}, {"phy_joint_reaction_force_y", 5}, {"phy_joint_reaction_torque", 6},
    {"phy_joint_motor_speed", 7}, {"phy_joint_angle", 8}, {"phy_joint_motor_torque", 9}, {"phy_joint_max_motor_torque", 10},
    {"phy_joint_translation", 11}, {"phy_joint_speed", 12}, {"phy_joint_motor_force", 13}, {"phy_joint_max_motor_force", 14},
    {"phy_joint_length_1", 15}, {"phy_joint_length_2", 16}, {"phy_joint_damping_ratio", 17}, {"phy_joint_frequency", 18},
    {"phy_joint_lower_angle_limit", 19}, {"phy_joint_upper_angle_limit", 20}, {"phy_joint_angle_limits", 21},
    {"phy_joint_max_length", 22}, {"phy_joint_max_torque", 23}, {"phy_joint_max_force", 24},
    {"phy_particle_water", 0}, {"phy_particle_zombie", 2}, {"phy_particle_wall", 4}, {"phy_particle_spring", 8},
    {"phy_particle_elastic", 16}, {"phy_particle_viscous", 32}, {"phy_particle_powder", 64}, {"phy_particle_tensile", 128},
    {"phy_particle_colourmixing", 256}, {"phy_particle_colormixing", 256},
    {"phy_particle_group_solid", 1}, {"phy_particle_group_rigid", 2},
    {"phy_particle_data_flags", 1}, {"phy_particle_data_position", 2}, {"phy_particle_data_velocity", 4},
    {"phy_particle_data_colour", 8}, {"phy_particle_data_color", 8}, {"phy_particle_data_category", 16}
};
int16_t Physics_resolveVariable(const char* name) {
    if (strncmp(name, "phy_", 4)) return -1;
    for (int i = 0; i < PHY_VARIABLE_COUNT; ++i) if (!strcmp(name, variables[i])) return PHYSICS_VARIABLE_BASE + i;
    for (size_t i = 0; i < sizeof(constants) / sizeof(constants[0]); ++i) if (!strcmp(name, constants[i].name)) return PHYSICS_CONSTANT_BASE + (int16_t)i;
    return -1;
}
RValue Physics_getVariable(Runner* runner, Instance* inst, int16_t id) {
    if (id >= PHYSICS_CONSTANT_BASE) {
        int index = id - PHYSICS_CONSTANT_BASE;
        if ((size_t)index < sizeof(constants) / sizeof(constants[0])) return RValue_makeReal(constants[index].value);
        return RValue_makeUndefined();
    }
    int field = id - PHYSICS_VARIABLE_BASE;
    if (!inst || !inst->physicsBody) return RValue_makeUndefined();
    if (field >= PHY_COLLISION_POINTS) {
        if (field != PHY_COLLISION_POINTS && inst->physicsContact[0] == 0) return RValue_makeUndefined();
        return RValue_makeReal(inst->physicsContact[field - PHY_COLLISION_POINTS]);
    }
    return RValue_makeReal(PhysicsEngine_variable(inst->physicsBody, field, 0, 0, (float)Runner_getEffectiveGameSpeed(runner)));
}
void Physics_setVariable(Runner* runner, Instance* inst, int16_t id, RValue value) {
    if (id >= PHYSICS_CONSTANT_BASE || !inst || !inst->physicsBody) return;
    PhysicsEngine_variable(inst->physicsBody, id - PHYSICS_VARIABLE_BASE, RValue_toReal(value), 1, (float)Runner_getEffectiveGameSpeed(runner));
}
static void physicsSync(void* owner, void* instance, float x, float y, float angle) {
    Runner* runner = owner;
    Instance* inst = instance;
    if (inst->destroyed) return;
    inst->x = x; inst->y = y; inst->imageAngle = angle;
    SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
}
static bool matchesObject(Runner* r, Instance* inst, int target) {
    int object = inst->objectIndex;
    for (uint32_t n = 0; object >= 0 && (uint32_t)object < r->dataWin->objt.count && n < r->dataWin->objt.count; ++n) {
        if (object == target) return true;
        object = r->dataWin->objt.objects[object].parentId;
    }
    return false;
}
static bool hasCollision(Runner* r, Instance* a, Instance* b) {
    if (a->objectIndex < 0 || !r->flattenedCollisionEvents) return false;
    FlattenedCollisionEventList* list = &r->flattenedCollisionEvents[a->objectIndex];
    for (uint32_t i = 0; i < list->eventCount; ++i) if (matchesObject(r, b, list->events[i].targetObjectIndex)) return true;
    return false;
}
static int physicsFilter(void* owner, void* first, void* second) {
    Runner* r = owner; Instance* a = first; Instance* b = second;
    return !a->destroyed && !b->destroyed && a->active && b->active && (hasCollision(r, a, b) || hasCollision(r, b, a));
}
static void dispatchContact(Runner* r, Instance* a, Instance* b) {
    // collision target inheritance is independent of handler inheritance
    int target = b->objectIndex;
    for (uint32_t n = 0; target >= 0 && (uint32_t)target < r->dataWin->objt.count && n < r->dataWin->objt.count; ++n) {
        int slot = EventSlotMap_lookup(&r->eventSlotMap, EVENT_COLLISION, target);
        if (slot >= 0 && ResolvedEventTable_lookup(&r->eventTable, a->objectIndex, slot, nullptr) >= 0) {
            Instance* previousOther = r->vmContext->otherInstance;
            r->vmContext->otherInstance = b;
            Runner_executeEvent(r, a, EVENT_COLLISION, target);
            r->vmContext->otherInstance = previousOther;
            return;
        }
        target = r->dataWin->objt.objects[target].parentId;
    }
}
static void physicsContact(void* owner, void* first, void* second, int points, float x, float y, float nx, float ny) {
    Runner* r = owner; Instance* a = first; Instance* b = second;
    if (a->destroyed || b->destroyed || !a->active || !b->active) return;
    float data[] = {(float)points, x, y, nx, ny};
    memcpy(a->physicsContact, data, sizeof(data)); memcpy(b->physicsContact, data, sizeof(data));
    dispatchContact(r, a, b);
    if (!a->destroyed && !b->destroyed) dispatchContact(r, b, a);
    memset(a->physicsContact, 0, sizeof(data)); memset(b->physicsContact, 0, sizeof(data));
}
static void physicsLine(void* owner, float x1, float y1, float x2, float y2) {
    Renderer* renderer = ((Runner*)owner)->renderer;
    if (renderer) renderer->vtable->drawLine(renderer, x1, y1, x2, y2, 1, renderer->drawColor, renderer->drawAlpha);
}
PhysicsEngine* Physics_ensureResources(Runner* r, float scale) {
    PhysicsCallbacks cb = {physicsSync, physicsFilter, physicsContact, physicsLine};
    if (!r->physicsResources) r->physicsResources = PhysicsEngine_create(r, cb, scale, (float)Runner_getEffectiveGameSpeed(r), nullptr);
    return r->physicsResources;
}
PhysicsEngine* Physics_createWorld(Runner* r, float scale) {
    PhysicsCallbacks cb = {physicsSync, physicsFilter, physicsContact, physicsLine};
    if (!Physics_ensureResources(r, scale)) return nullptr;
    return PhysicsEngine_create(r, cb, scale, (float)Runner_getEffectiveGameSpeed(r), r->physicsResources);
}
void Physics_initRoom(Runner* r) {
    if (!r->physicsRooms) r->physicsRooms = safeCalloc(r->dataWin->room.count, sizeof(*r->physicsRooms));
    PhysicsEngine* existing = r->physicsRooms[r->currentRoomIndex];
    if (!r->currentRoom->persistent || !existing) {
        PhysicsEngine* fresh = r->currentRoom->world ? Physics_createWorld(r, r->currentRoom->metersPerPixel > 0 ? r->currentRoom->metersPerPixel : 0.1f) : nullptr;
        PhysicsEngine_free(existing);
        r->physicsRooms[r->currentRoomIndex] = fresh;
        r->physics = fresh;
        double gravity[] = {r->currentRoom->world ? r->currentRoom->gravityX : 0, r->currentRoom->world ? r->currentRoom->gravityY : 10};
        PhysicsEngine_call(fresh, "physics_world_gravity", nullptr, nullptr, gravity, 2);
    } else r->physics = existing;
}
void Physics_releaseRoom(Runner* r, int32_t index) {
    if (index < 0 || index == r->currentRoomIndex || !r->physicsRooms || r->dataWin->room.rooms[index].persistent) return;
    PhysicsEngine_free(r->physicsRooms[index]);
    r->physicsRooms[index] = nullptr;
}
void Physics_free(Runner* r) {
    if (r->physicsRooms) {
        for (uint32_t i = 0; i < r->dataWin->room.count; ++i) PhysicsEngine_free(r->physicsRooms[i]);
        free(r->physicsRooms); r->physicsRooms = nullptr;
    }
    PhysicsEngine_free(r->physicsResources); r->physicsResources = nullptr;
    r->physics = nullptr;
}
static double call(Runner* r, const char* name, const double* args, int count) { return PhysicsEngine_call(r->physics, name, nullptr, nullptr, args, count); }
static void fixtureSetting(Runner* r, const char* name, int fixture, double value) { double args[] = {fixture, value}; call(r, name, args, 2); }
void Physics_initInstance(Runner* r, Instance* inst) {
    if (inst->physicsBody || !r->physics || inst->objectIndex < 0) return;
    GameObject* o = &r->dataWin->objt.objects[inst->objectIndex];
    if (!o->usesPhysics || inst->spriteIndex < 0 || (uint32_t)inst->spriteIndex >= r->dataWin->sprt.count || o->physicsVertexCount <= 0) return;
    int fixture = (int)call(r, "physics_fixture_create", nullptr, 0);
    float xo = 0, yo = 0;
    if (o->collisionShape == 0) {
        // OBJT stores a circle as centre (vertex 0) and radius (vertex 1.x).
        if (o->physicsVertexCount < 2) { fixtureSetting(r, "physics_fixture_delete", fixture, 0); return; }
        float scale = GMLReal_fabs(inst->imageXscale - inst->imageYscale) < 0.0001 ? inst->imageXscale : 1;
        xo = -o->physicsVertices[0].x * scale; yo = -o->physicsVertices[0].y * scale;
        fixtureSetting(r, "physics_fixture_set_circle_shape", fixture, GMLReal_fabs(o->physicsVertices[1].x * scale));
    } else {
        fixtureSetting(r, "physics_fixture_set_polygon_shape", fixture, 0);
        for (int i = 0; i < o->physicsVertexCount; ++i) {
            int index = inst->imageXscale * inst->imageYscale < 0 ? o->physicsVertexCount - i - 1 : i;
            double args[] = {fixture, o->physicsVertices[index].x * inst->imageXscale, o->physicsVertices[index].y * inst->imageYscale};
            call(r, "physics_fixture_add_point", args, 3);
        }
    }
    fixtureSetting(r, "physics_fixture_set_density", fixture, o->density);
    fixtureSetting(r, "physics_fixture_set_friction", fixture, o->friction);
    fixtureSetting(r, "physics_fixture_set_restitution", fixture, o->restitution);
    fixtureSetting(r, "physics_fixture_set_collision_group", fixture, (int32_t)o->group);
    fixtureSetting(r, "physics_fixture_set_sensor", fixture, o->isSensor);
    fixtureSetting(r, "physics_fixture_set_linear_damping", fixture, o->linearDamping);
    fixtureSetting(r, "physics_fixture_set_angular_damping", fixture, o->angularDamping);
    fixtureSetting(r, "physics_fixture_set_awake", fixture, o->awake);
    if (o->kinematic) fixtureSetting(r, "physics_fixture_set_kinematic", fixture, 0);
    inst->physicsBody = PhysicsEngine_body(r->physics, inst, fixture, inst->x, inst->y, inst->imageAngle, xo, yo, 1);
    fixtureSetting(r, "physics_fixture_delete", fixture, 0);
}
void Physics_step(Runner* r) {
    if (!r->physics) return;
    for (int i = 0; i < arrlen(r->instances); ++i) {
        Instance* inst = r->instances[i];
        if (inst->destroyed) { PhysicsEngine_destroyBody(inst->physicsBody); inst->physicsBody = nullptr; continue; }
        PhysicsEngine_transform(inst->physicsBody, inst->x, inst->y, inst->imageAngle, inst->active);
    }
    PhysicsEngine_step(r->physics, (float)Runner_getEffectiveGameSpeed(r));
}
