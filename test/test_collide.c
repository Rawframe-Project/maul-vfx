// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Collision with primitives: no particle passes through a plane, a sphere
// or a box, inside or out, however fast; a bounce with restitution and
// friction as computed by hand; a rest speed; a particle's radius; a
// start on the wrong side put back; contacts several a step; kill on
// collision, life lost, the count and its kill rule; the instance's own
// colliders; a collider in the emitter's space; and the refusals. The
// module's own functions are tested in test_collide_internal.c.

#include "particles.h"
#include "peek.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitter)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEmitterDef peeking = Peeking(emitter);
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &peeking;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

// An emitter of what the host spawns, colliding with colliders, living
// ten seconds.
static mvfxEmitterDef Held(const mvfxCollider* colliders, int32_t count, int32_t capacity)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = capacity;
    emitter.hostSpawnCapacity = capacity;
    emitter.lifetime = mvfxMakeRangeSource(10.0f, 10.0f);
    emitter.collision = (mvfxCollision){.enable = true, .restitution = 1.0f};
    emitter.colliders = colliders;
    emitter.colliderCount = count;
    return emitter;
}

static void Place(Scene* scene, double x, double y, double z, float vx, float vy, float vz)
{
    const double place[3] = {x, y, z};
    const float velocity[3] = {vx, vy, vz};
    mvfxSpawnRequest request = {.count = 1, .positions = place, .velocities = velocity};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "placed");
}

static TestParticle s_particles[1000];

// Reads the particles into s_particles; returns how many there are.
static int32_t Read(const Scene* scene)
{
    return ReadParticles(scene->context, scene->instance, 0, s_particles, 1000);
}

// The particle at i.
static TestParticle At(const Scene* scene, int32_t i)
{
    CHECK(i < Read(scene), "the particle read");
    return s_particles[i];
}

static uint32_t s_random = 2026;

// A number in [0, 1).
static float Random(void)
{
    s_random = s_random * 1664525u + 1013904223u;
    return (float)(s_random >> 8u) * 0x1p-24f;
}

// Whether a place keeps to a collider's allowed side, within a hair.
static bool Allowed(const mvfxCollider* c, const float p[3])
{
    const float hair = 1e-3f;
    float d[3] = {p[0] - c->center[0], p[1] - c->center[1], p[2] - c->center[2]};
    if (c->kind == mvfx_colliderPlane)
    {
        return d[0] * c->normal[0] + d[1] * c->normal[1] + d[2] * c->normal[2] >= -hair;
    }
    if (c->kind == mvfx_colliderSphere)
    {
        float r = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        return c->inside ? r <= c->radius + hair : r >= c->radius - hair;
    }
    // Unturned boxes only, here.
    bool in = fabsf(d[0]) < c->halfExtents[0] - hair && fabsf(d[1]) < c->halfExtents[1] - hair &&
              fabsf(d[2]) < c->halfExtents[2] - hair;
    bool within = fabsf(d[0]) <= c->halfExtents[0] + hair &&
                  fabsf(d[1]) <= c->halfExtents[1] + hair &&
                  fabsf(d[2]) <= c->halfExtents[2] + hair;
    return c->inside ? within : !in;
}

static void TestNoTunneling(void)
{
    // A thousand particles a collider, from allowed places at up to 10,000
    // a second, over several steps: none ends on the wrong side.
    static const mvfxCollider colliders[5] = {
        {.kind = mvfx_colliderPlane, .center = {0, -1, 0}, .normal = {0, 1, 0}},
        {.kind = mvfx_colliderSphere, .radius = 2.0f},
        {.kind = mvfx_colliderSphere, .inside = true, .radius = 5.0f},
        {.kind = mvfx_colliderBox, .halfExtents = {1, 2, 3}, .rotation = {0, 0, 0, 1}},
        {.kind = mvfx_colliderBox,
         .inside = true,
         .halfExtents = {4, 3, 2},
         .rotation = {0, 0, 0, 1}},
    };
    for (int32_t c = 0; c < 5; ++c)
    {
        mvfxEmitterDef emitter = Held(&colliders[c], 1, 1000);
        Scene scene = NewScene(&emitter);
        double places[1000][3];
        float velocities[1000][3];
        int32_t placed = 0;
        while (placed < 1000)
        {
            float p[3] = {Random() * 7.0f - 3.5f, Random() * 7.0f - 3.5f, Random() * 3.8f - 1.9f};
            if (!Allowed(&colliders[c], p))
            {
                continue;
            }
            float speed = powf(10.0f, Random() * 4.0f);
            for (int32_t k = 0; k < 3; ++k)
            {
                places[placed][k] = (double)p[k];
                velocities[placed][k] = speed * (Random() * 2.0f - 1.0f);
            }
            ++placed;
        }
        mvfxSpawnRequest request = {
            .count = 1000, .positions = &places[0][0], .velocities = &velocities[0][0]};
        CHECK(mvfxInstance_Spawn(scene.context, scene.instance, 0, &request) == mvfx_success,
              "placed");
        bool kept = true;
        int32_t count = 0;
        for (int32_t step = 0; step < 3 && kept; ++step)
        {
            CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 3) == mvfx_success,
                  "step");
            count = Read(&scene);
            for (int32_t i = 0; i < count; ++i)
            {
                kept = kept && Allowed(&colliders[c], s_particles[i].position);
            }
        }
        CHECK(kept && count == 1000, "none passes through");
        mvfxDestroyContext(scene.context);
    }
}

static void TestBounce(void)
{
    // From y = 1 down at 10 onto the plane y = 0, restitution 0.5 and
    // friction 0.5, along X at 10: the contact at a tenth of the step, then
    // up at 5 and along at 5, the rest of the step's travel bounced alike,
    // (9, -9) to (4.5, 4.5) from (1, 0).
    static const mvfxCollider floor = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};
    mvfxEmitterDef emitter = Held(&floor, 1, 4);
    emitter.collision.restitution = 0.5f;
    emitter.collision.friction = 0.5f;
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 1, 0, 10, -10, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(fabsf(At(&scene, 0).velocity[1] - 5.0f) <= 1e-4f &&
              fabsf(At(&scene, 0).velocity[0] - 5.0f) <= 1e-4f,
          "the velocity bounced");
    CHECK(fabsf(At(&scene, 0).position[1] - 4.5f) <= 1e-4f &&
              fabsf(At(&scene, 0).position[0] - 5.5f) <= 1e-4f,
          "the rest of the step bounced alike");
    CHECK(s_peeked.collisions[0] == 1, "one collision counted");
    mvfxDestroyContext(scene.context);

    // Below the rest speed after the bounce: stopped on the plane.
    emitter.collision.restSpeed = 6.0f;
    scene = NewScene(&emitter);
    Place(&scene, 0, 1, 0, 0, -10, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(At(&scene, 0).velocity[1] == 0.0f && fabsf(At(&scene, 0).position[1]) <= 1e-4f,
          "at rest where it hit");
    mvfxDestroyContext(scene.context);

    // A radius of 0.5 meets the plane half a unit early (no bounce, so it
    // stays there); a start below the plane is put on it.
    emitter.collision.restSpeed = 0.0f;
    emitter.collision.radius = 0.5f;
    emitter.collision.restitution = 0.0f;
    emitter.collision.friction = 0.0f;
    scene = NewScene(&emitter);
    Place(&scene, 0, 1, 0, 0, -1, 0);
    Place(&scene, 0, -3, 0, 0, 0, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(fabsf(At(&scene, 0).position[1] - 0.5f) <= 1e-4f, "the radius keeps it half a unit off");
    CHECK(fabsf(At(&scene, 1).position[1] - 0.5f) <= 1e-4f,
          "a start below put back on the surface");
    mvfxDestroyContext(scene.context);
}

static void TestManyContacts(void)
{
    // Inside a box two units wide, at 4.5 a second along X for a second:
    // off x = 1 back 3.5 to x = -2.5, off x = -1 back 1.5 to x = 0.5, its
    // velocity forward again.
    static const mvfxCollider box = {.kind = mvfx_colliderBox,
                                     .inside = true,
                                     .halfExtents = {1, 1, 1},
                                     .rotation = {0, 0, 0, 1}};
    mvfxEmitterDef emitter = Held(&box, 1, 4);
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 4.5f, 0, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(s_peeked.collisions[0] == 2 && fabsf(At(&scene, 0).position[0] - 0.5f) <= 1e-4f &&
              At(&scene, 0).velocity[0] == 4.5f,
          "two contacts in one step");
    mvfxDestroyContext(scene.context);
}

static void TestLifeAndCount(void)
{
    static const mvfxCollider floor = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};
    // Killed by a collision; a step that collides nowhere keeps it.
    mvfxEmitterDef emitter = Held(&floor, 1, 4);
    emitter.collision.killOnCollision = true;
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 1, 0, 0, -2, 0);
    Place(&scene, 0, 1, 0, 0, 2, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 0, &stats) == mvfx_success,
          "stats");
    CHECK(stats.live == 1 && stats.killCount == 1, "killed by a collision");
    mvfxDestroyContext(scene.context);

    // Half its life left a collision: its inverse life doubles.
    emitter = Held(&floor, 1, 4);
    emitter.collision.lifeLoss = 0.5f;
    scene = NewScene(&emitter);
    Place(&scene, 0, 1, 0, 0, -2, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(fabsf(s_peeked.inverseLife[0] - 0.2f) <= 1e-6f, "half its life");
    mvfxDestroyContext(scene.context);
    emitter.collision.lifeLoss = 1.0f;
    scene = NewScene(&emitter);
    Place(&scene, 0, 1, 0, 0, -2, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(Read(&scene) == 0, "all its life");
    mvfxDestroyContext(scene.context);
    // A life cut below its age: gone (it meets the plane in the second
    // step, its 10 s cut to 0.5 at an age of 2).
    emitter.collision.lifeLoss = 0.95f;
    scene = NewScene(&emitter);
    Place(&scene, 0, 1, 0, 0, -0.6f, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(Read(&scene) == 0, "a life cut below its age");
    mvfxDestroyContext(scene.context);

    // Killed at its second collision, between two walls.
    static const mvfxCollider walls[2] = {
        {.kind = mvfx_colliderPlane, .center = {-1, 0, 0}, .normal = {1, 0, 0}},
        {.kind = mvfx_colliderPlane, .center = {1, 0, 0}, .normal = {-1, 0, 0}},
    };
    mvfxKillRule rule = {.kind = mvfx_killCollisions, .collisions = 2};
    emitter = Held(walls, 2, 4);
    emitter.killRules = &rule;
    emitter.killRuleCount = 1;
    scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 1.5f, 0, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(Read(&scene) == 1, "one collision, alive");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(Read(&scene) == 0, "killed at the second");
    mvfxDestroyContext(scene.context);
}

static void TestCountAtBirth(void)
{
    // A particle collides and ends; one born in its slot after starts at
    // no collisions.
    static const mvfxCollider floor = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};
    mvfxEmitterDef emitter = Held(&floor, 1, 4);
    emitter.lifetime = mvfxMakeRangeSource(1.5f, 1.5f);
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 1, 0, 0, -2, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(s_peeked.collisions[0] == 1, "one collision");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(Read(&scene) == 0, "ended");
    Place(&scene, 0, 50, 0, 0, 0, 0);
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    CHECK(Read(&scene) == 1 && s_peeked.count == 1 && s_peeked.collisions[0] == 0,
          "born at no collisions");
    mvfxDestroyContext(scene.context);
}

static void TestHostAndSpaces(void)
{
    // The instance's colliders, for an emitter that takes them; not for
    // one that does not.
    for (int32_t takes = 0; takes < 2; ++takes)
    {
        mvfxEmitterDef emitter = Held(NULL, 0, 4);
        emitter.collision.hostColliders = takes != 0;
        Scene scene = NewScene(&emitter);
        static const mvfxCollider floor = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};
        CHECK(mvfxInstance_SetColliders(scene.context, scene.instance, &floor, 1) == mvfx_success,
              "set");
        Place(&scene, 0, 1, 0, 0, -2, 0);
        CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
        float y = At(&scene, 0).position[1];
        CHECK(takes != 0 ? fabsf(y - 1.0f) <= 1e-4f : fabsf(y + 1.0f) <= 1e-4f,
              takes != 0 ? "the instance's colliders" : "not for an emitter without them");
        if (takes != 0)
        {
            CHECK(mvfxInstance_SetColliders(scene.context, scene.instance, NULL, 0) == mvfx_success,
                  "cleared");
            Place(&scene, 0, 1, 0, 0, -2, 0);
            CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success,
                  "step");
            CHECK(fabsf(At(&scene, 1).position[1] + 1.0f) <= 1e-4f, "cleared");
            uint64_t misuse = mvfxContext_GetMisuse(scene.context);
            mvfxCollider bad = {.kind = mvfx_colliderSphere};
            CHECK(mvfxInstance_SetColliders(scene.context, scene.instance, &bad, 1) ==
                          mvfx_errorInvalid &&
                      mvfxInstance_SetColliders(scene.context, scene.instance, &floor,
                                                MVFX_MAX_COLLIDERS + 1) == mvfx_errorLimit &&
                      mvfxInstance_SetColliders(scene.context, scene.instance, NULL, 1) ==
                          mvfx_errorInvalid,
                  "refused");
            CHECK(mvfxContext_GetMisuse(scene.context) == misuse + 3, "each counted");
            mvfxInstanceId stale = scene.instance;
            stale.generation += 1;
            CHECK(mvfxInstance_SetColliders(scene.context, stale, &floor, 1) == mvfx_errorStale &&
                      mvfxInstance_SetColliders(NULL, scene.instance, &floor, 1) ==
                          mvfx_errorInvalid,
                  "stale, no context");
        }
        mvfxDestroyContext(scene.context);
    }

    // A sphere of radius 1 at the emitter's (1, 0, 0) under an instance at
    // (100, 0, 0) turned a quarter about Z at scale 2: radius 2 about the
    // world's (100, 2, 0); a particle falling onto it stops at its top.
    static const mvfxCollider ball = {
        .kind = mvfx_colliderSphere, .space = mvfx_spaceLocal, .center = {1, 0, 0}, .radius = 1.0f};
    mvfxEmitterDef emitter = Held(&ball, 1, 4);
    emitter.collision.restSpeed = 100.0f;
    Scene scene = NewScene(&emitter);
    static const double origin[3] = {100.0, 0.0, 0.0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Place(&scene, 100, 10, 0, 0, -20, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(fabsf(At(&scene, 0).position[0] - 100.0f) <= 1e-3f &&
              fabsf(At(&scene, 0).position[1] - 4.0f) <= 1e-3f,
          "on the mapped, scaled sphere's top");
    mvfxDestroyContext(scene.context);
}

// One particle against one collider for a second: its place, velocity and
// collisions after.
typedef struct After
{
    float p[3];
    float v[3];
    uint32_t collisions;
    int32_t live;
} After;

static After Run(const mvfxCollider* collider, float radius, double x, double y, double z, float vx,
                 float vy, float vz)
{
    mvfxEmitterDef emitter = Held(collider, 1, 4);
    emitter.collision.radius = radius;
    Scene scene = NewScene(&emitter);
    Place(&scene, x, y, z, vx, vy, vz);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    After after = {.live = Read(&scene)};
    if (after.live > 0)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            after.p[k] = s_particles[0].position[k];
            after.v[k] = s_particles[0].velocity[k];
        }
        after.collisions = s_peeked.collisions[0];
    }
    mvfxDestroyContext(scene.context);
    return after;
}

static bool Near(float a, float b)
{
    return fabsf(a - b) <= 1e-4f;
}

static void TestShapes(void)
{
    static const mvfxCollider ball = {.kind = mvfx_colliderSphere, .radius = 2.0f};
    // Born inside a sphere it keeps out of: put on its surface, along the
    // way from its center.
    After a = Run(&ball, 0, 0, 0.5, 0, 0, 0, 0);
    CHECK(Near(a.p[1], 2.0f) && a.collisions == 1, "put out of a sphere");
    // At its very center: put on its top.
    a = Run(&ball, 0, 0, 0, 0, 0, 0, 0);
    CHECK(Near(a.p[2], 2.0f) && Near(a.p[0], 0.0f), "from the center, to the top");
    // Moving away from a sphere behind it: no contact.
    a = Run(&ball, 0, 3, 0, 0, 1, 0, 0);
    CHECK(Near(a.p[0], 4.0f) && a.collisions == 0, "a sphere behind");

    // Inside a sphere of radius 5, from its center at 10 along X: off x = 5
    // halfway, back to the center, its velocity reversed.
    static const mvfxCollider room = {.kind = mvfx_colliderSphere, .inside = true, .radius = 5.0f};
    a = Run(&room, 0, 0, 0, 0, 10, 0, 0);
    CHECK(Near(a.p[0], 0.0f) && Near(a.v[0], -10.0f) && a.collisions == 1,
          "off the inside of a sphere");
    // A radius wider than the room leaves it a point: the particle stays at
    // its center.
    a = Run(&room, 6.0f, 1, 0, 0, 0, 0, 0);
    CHECK(Near(a.p[0], 0.0f), "a room narrower than the particle");

    // Ending exactly on a plane is no contact: its velocity holds.
    static const mvfxCollider floor = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};
    a = Run(&floor, 0, 0, 1, 0, 0, -1, 0);
    CHECK(Near(a.p[1], 0.0f) && a.v[1] == -1.0f && a.collisions == 0, "ending on the plane");
    // A normal of length 2 acts as the unit one: half the speed back at
    // restitution 1 is all of it.
    static const mvfxCollider longNormal = {.kind = mvfx_colliderPlane, .normal = {0, 2, 0}};
    a = Run(&longNormal, 0, 0, 1, 0, 0, -2, 0);
    CHECK(Near(a.v[1], 2.0f) && Near(a.p[1], 1.0f), "a normal not of unit length");
    a = Run(&longNormal, 0, 0, -3, 0, 0, 0, 0);
    CHECK(Near(a.p[1], 0.0f), "put back onto a plane with a long normal");

    // Far outside a box it keeps in, off its corner: one push, inside.
    static const mvfxCollider box = {.kind = mvfx_colliderBox,
                                     .inside = true,
                                     .halfExtents = {1, 1, 1},
                                     .rotation = {0, 0, 0, 1}};
    a = Run(&box, 0, 10, 10, 0, 0, 0, 0);
    CHECK(a.collisions == 1 && Near(a.p[0], 1.0f) && Near(a.p[1], 1.0f),
          "pushed into a box at once");
    // A box turned 30 degrees about Z by a rotation 0.0009 longer than unit:
    // a particle at rest just inside its end stays where it is.
    const float s15 = 0.25881905f * 1.0009f;
    const float c15 = 0.96592583f * 1.0009f;
    mvfxCollider turned = {.kind = mvfx_colliderBox,
                           .inside = true,
                           .halfExtents = {2, 0.5f, 0.5f},
                           .rotation = {0, 0, s15, c15}};
    a = Run(&turned, 0, 1.9998 * 0.86602540, 1.9998 * 0.5, 0, 0, 0, 0);
    CHECK(a.collisions == 0, "a near-unit rotation");
}

static void TestRefusals(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    for (int32_t field = 0; field < 15; ++field)
    {
        // Past a named bound: mvfx_errorLimit, data a correct program may
        // pass, so no misuse.
        mvfxResult want = field == 10 || field == 14 ? mvfx_errorLimit : mvfx_errorInvalid;
        mvfxCollider collider = {.kind = mvfx_colliderSphere, .radius = 1.0f};
        mvfxEmitterDef bad = Held(&collider, 1, 4);
        mvfxKillRule rule = {.kind = mvfx_killCollisions, .collisions = 1};
        switch (field)
        {
        case 0:
            collider.kind = 3;
            break;
        case 1:
            collider.space = 2;
            break;
        case 2:
            collider.radius = 0.0f;
            break;
        case 3:
            collider = (mvfxCollider){.kind = mvfx_colliderPlane};
            break;
        case 4:
            collider = (mvfxCollider){
                .kind = mvfx_colliderBox, .halfExtents = {1, 1, 0}, .rotation = {0, 0, 0, 1}};
            break;
        case 5:
            collider = (mvfxCollider){.kind = mvfx_colliderBox, .halfExtents = {1, 1, 1}};
            break;
        case 6:
            bad.collision.restitution = 1.5f;
            break;
        case 7:
            bad.collision.friction = -0.1f;
            break;
        case 8:
            bad.collision.lifeLoss = (float)NAN;
            break;
        case 9:
            bad.collision.radius = -1.0f;
            break;
        case 10:
            bad.colliderCount = MVFX_MAX_COLLIDERS + 1;
            break;
        case 11:
            bad.collision.enable = false;
            bad.killRules = &rule;
            bad.killRuleCount = 1;
            break;
        case 12:
            rule.collisions = 0;
            bad.killRules = &rule;
            bad.killRuleCount = 1;
            break;
        case 13:
            collider.center[1] = (float)INFINITY;
            break;
        default:
            collider.radius = 2.0f * MVFX_MAX_EXTENT;
            break;
        }
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &bad;
        effectDef.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == want, "refused");
    }
    mvfxDestroyContext(context);
}

int main(void)
{
    TestNoTunneling();
    TestBounce();
    TestManyContacts();
    TestLifeAndCount();
    TestCountAtBirth();
    TestHostAndSpaces();
    TestShapes();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
