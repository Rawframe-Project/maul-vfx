// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Kill rules: a step's segment through a sphere or a turned box however
// fast, ending outside, past a plane, slower than a speed after an age,
// an attractor's kill radius, a rule in the emitter's space, a birth at
// the step's end killed where it is born, the others' order kept, the
// killed counted and restarted, and the refusals.

#include "particles.h"
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
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

// An emitter of what the host spawns under the rules, living ten seconds.
static mvfxEmitterDef Held(const mvfxKillRule* rules, int32_t count)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.killRules = rules;
    emitter.killRuleCount = count;
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

static mvfxEmitterStats Stats(const Scene* scene)
{
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene->context, scene->instance, 0, &stats) == mvfx_success,
          "stats");
    return stats;
}

// Whether a particle placed with a velocity survives one step of a
// second under the rules.
static bool Survives(const mvfxKillRule* rules, int32_t count, double x, double y, double z,
                     float vx, float vy, float vz)
{
    mvfxEmitterDef emitter = Held(rules, count);
    Scene scene = NewScene(&emitter);
    Place(&scene, x, y, z, vx, vy, vz);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    mvfxEmitterStats stats = Stats(&scene);
    bool alive = stats.live == 1;
    CHECK(stats.killed == (alive ? 0u : 1u) && stats.owed == 1, "counted");
    mvfxDestroyContext(scene.context);
    return alive;
}

static void TestRegions(void)
{
    // A sphere of radius 1 at the origin: crossed in one step at 100 a
    // second, killed; passing beside it, not.
    mvfxKillRule rule = {.kind = mvfx_killInsideSphere, .radius = 1.0f};
    CHECK(!Survives(&rule, 1, -50, 0.5, 0, 100, 0, 0), "through a sphere, however fast");
    CHECK(Survives(&rule, 1, -50, 1.5, 0, 100, 0, 0), "beside it");
    rule.kind = mvfx_killOutsideSphere;
    rule.radius = 10.0f;
    CHECK(!Survives(&rule, 1, 0, 0, 0, 11, 0, 0), "ending outside a sphere");
    CHECK(Survives(&rule, 1, 0, 0, 0, 9, 0, 0), "ending inside it");

    // A box of half sizes (1, 4, 1) turned a quarter about Z, so (4, 1, 1)
    // in the world: a path along Y at x = 3 crosses it; at x = 5 it misses.
    rule = (mvfxKillRule){.kind = mvfx_killInsideBox,
                          .halfExtents = {1, 4, 1},
                          .rotation = {0, 0, 0.70710678f, 0.70710678f}};
    CHECK(!Survives(&rule, 1, 3, -50, 0, 0, 100, 0), "through a turned box");
    CHECK(Survives(&rule, 1, 5, -50, 0, 0, 100, 0), "beside it");
    CHECK(Survives(&rule, 1, 3, -50, 1.5, 0, 100, 0), "above it");
    rule.kind = mvfx_killOutsideBox;
    CHECK(!Survives(&rule, 1, 0, 0, 0, 0, 2, 0), "ending outside a turned box");
    CHECK(Survives(&rule, 1, 0, 0, 0, 3, 0, 0), "ending inside it, along its long side");

    // Past the plane through (0, 2, 0) facing +Y.
    rule = (mvfxKillRule){.kind = mvfx_killPastPlane, .center = {0, 2, 0}, .normal = {0, 3, 0}};
    CHECK(!Survives(&rule, 1, 0, 0, 0, 0, 2.5f, 0), "past a plane");
    CHECK(Survives(&rule, 1, 0, 0, 0, 0, 1.5f, 0), "short of it");
}

static void TestSpeedAndAge(void)
{
    // Slower than 2 once older than 0.5 seconds: at rest, killed at the
    // end of a second; at 3, not; a particle half a second old at the end
    // of its step, not yet.
    mvfxKillRule rule = {.kind = mvfx_killSlower, .speed = 2.0f, .minimumAge = 0.5f};
    CHECK(!Survives(&rule, 1, 0, 0, 0, 1, 0, 0), "slower, old enough");
    CHECK(Survives(&rule, 1, 0, 0, 0, 3, 0, 0), "fast enough");
    mvfxEmitterDef emitter = Held(&rule, 1);
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 0, 0, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    CHECK(Stats(&scene).live == 1, "not yet old enough");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    CHECK(Stats(&scene).live == 0, "then killed");
    mvfxDestroyContext(scene.context);
}

static void TestAttractorAndSpaces(void)
{
    // An attractor at (0, 5, 0) with kill radius 1: a path through it
    // killed, one beside it not, whatever its pull.
    mvfxForce force = {.kind = mvfx_forceAttractor,
                       .point = {0, 5, 0},
                       .strength = 0.0f,
                       .softening = 1.0f,
                       .killRadius = 1.0f};
    mvfxEmitterDef emitter = Held(NULL, 0);
    emitter.forces = &force;
    emitter.forceCount = 1;
    Scene scene = NewScene(&emitter);
    Place(&scene, -50, 5.5, 0, 100, 0, 0);
    Place(&scene, -50, 7, 0, 100, 0, 0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    mvfxEmitterStats stats = Stats(&scene);
    CHECK(stats.live == 1 && stats.killed == 1, "through the attractor's kill radius");
    mvfxDestroyContext(scene.context);

    // A sphere of radius 1 at the emitter's (1, 0, 0), under an instance at
    // (100, 0, 0) turned a quarter about Z at scale 2: radius 2 at the
    // world's (100, 2, 0).
    mvfxKillRule rule = {.kind = mvfx_killInsideSphere,
                         .space = mvfx_spaceLocal,
                         .center = {1, 0, 0},
                         .radius = 1.0f};
    emitter = Held(&rule, 1);
    scene = NewScene(&emitter);
    static const double origin[3] = {100.0, 0.0, 0.0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "placed");
    Place(&scene, 101.5, 2, 0, 0, 0, 0);
    Place(&scene, 102.5, 2, 0, 0, 0, 0);
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    stats = Stats(&scene);
    CHECK(stats.live == 1 && stats.killed == 1, "a rule in the emitter's space");
    mvfxDestroyContext(scene.context);
}

static void TestEdgesAndFrames(void)
{
    // A box turned 30 degrees about Z, half sizes (2, 0.5, 0.5): crossed
    // along -X through its center, missed at y = 1.5 (inside its long
    // diagonal's reach only if the turn were the other way).
    // The quaternion's halves: sin and cos of 15 degrees.
    const float s30 = 0.25881905f;
    const float c30 = 0.96592583f;
    mvfxKillRule rule = {
        .kind = mvfx_killInsideBox, .halfExtents = {2, 0.5f, 0.5f}, .rotation = {0, 0, s30, c30}};
    CHECK(!Survives(&rule, 1, 50, 0, 0, -100, 0, 0), "through a box moving along -X");
    // The box's +X end at 30 degrees lies at (1.73, 1, 0): a point there is
    // inside, its mirror (1.73, -1, 0) is not.
    rule.kind = mvfx_killOutsideBox;
    CHECK(Survives(&rule, 1, 1.7, 0.95, 0, 0, 0, 0), "inside the turned box's end");
    CHECK(!Survives(&rule, 1, 1.7, -0.95, 0, 0, 0, 0), "outside its mirror");
    // A rotation 0.0009 longer than unit acts as the unit one: a place just
    // inside the end stays inside.
    rule.rotation[2] = s30 * 1.0009f;
    rule.rotation[3] = c30 * 1.0009f;
    // Along the box's X, at 30 degrees: (cos 30, sin 30).
    CHECK(Survives(&rule, 1, 1.9998 * 0.86602540, 1.9998 * 0.5, 0, 0, 0, 0),
          "a near-unit rotation");

    // Exactly on a plane is not past it.
    rule = (mvfxKillRule){.kind = mvfx_killPastPlane, .center = {0, 2, 0}, .normal = {0, 1, 0}};
    CHECK(Survives(&rule, 1, 0, 2, 0, 0, 0, 0), "on the plane");

    // A vortex's unread kill radius kills nothing.
    mvfxForce force = {.kind = mvfx_forceVortex, .vector = {0, 0, 1}, .killRadius = 5.0f};
    mvfxEmitterDef emitter = Held(NULL, 0);
    emitter.forces = &force;
    emitter.forceCount = 1;
    Scene scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 0, 0, 0);
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    CHECK(Stats(&scene).live == 1, "only an attractor's kill radius");
    mvfxDestroyContext(scene.context);

    // In the emitter's space under an instance turned a quarter about Z at
    // scale 2: a box turned a quarter about X with half sizes (3, 1, 0.5)
    // spans (6, 1, 2) in the world after the instance's turn: X from the
    // emitter's Y... checked at points; and a speed of 1 is 2.
    static const double origin[3] = {0.0, 0.0, 0.0};
    static const float quarterZ[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    rule = (mvfxKillRule){.kind = mvfx_killOutsideBox,
                          .space = mvfx_spaceLocal,
                          .halfExtents = {3, 1, 0.5f},
                          .rotation = {0.70710678f, 0, 0, 0.70710678f}};
    mvfxKillRule slow = {.kind = mvfx_killSlower, .space = mvfx_spaceLocal, .speed = 1.0f};
    for (int32_t c = 0; c < 4; ++c)
    {
        static const double places[4][3] = {{0, 5.5, 0}, {0, 6.5, 0}, {0, 0, 1.5}, {0, 0, 2.5}};
        static const bool alive[4] = {true, false, true, false};
        emitter = Held(&rule, 1);
        scene = NewScene(&emitter);
        CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarterZ, 2.0f,
                                        true) == mvfx_success,
              "placed");
        Place(&scene, places[c][0], places[c][1], places[c][2], 0, 0, 0);
        CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
        CHECK((Stats(&scene).live == 1) == alive[c], "a box turned by the instance's and its own");
        mvfxDestroyContext(scene.context);
    }
    for (int32_t c = 0; c < 2; ++c)
    {
        emitter = Held(&slow, 1);
        scene = NewScene(&emitter);
        CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarterZ, 2.0f,
                                        true) == mvfx_success,
              "placed");
        Place(&scene, 0, 0, 0, c == 0 ? 1.5f : 2.5f, 0, 0);
        CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
        CHECK((Stats(&scene).live == 1) == (c == 1), "a speed scaled with the instance");
        mvfxDestroyContext(scene.context);
    }
}

static void TestBirthsAndOrder(void)
{
    // A rate of one a second: its birth falls on the step's end, inside a
    // sphere at the origin, and is killed where it is born.
    mvfxKillRule rule = {.kind = mvfx_killInsideSphere, .radius = 1.0f};
    mvfxEmitterDef emitter = Held(&rule, 1);
    emitter.rate = 1.0f;
    Scene scene = NewScene(&emitter);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    mvfxEmitterStats stats = Stats(&scene);
    CHECK(stats.owed == 1 && stats.live == 0 && stats.killed == 1, "killed at its birth");
    mvfxDestroyContext(scene.context);

    // Three placed, the middle one past a plane: the others keep their
    // order.
    rule = (mvfxKillRule){.kind = mvfx_killPastPlane, .center = {0, 10, 0}, .normal = {0, 1, 0}};
    emitter = Held(&rule, 1);
    scene = NewScene(&emitter);
    Place(&scene, 0, 0, 0, 0, 0, 0);
    Place(&scene, 0, 20, 0, 0, 0, 0);
    Place(&scene, 0, 1, 0, 0, 0, 0);
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    TestParticle particles[3];
    CHECK(ReadParticles(scene.context, scene.instance, 0, particles, 3) == 2 &&
              particles[0].serial == 0 && particles[1].serial == 2,
          "the others' order kept");
    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success &&
              Stats(&scene).killed == 0,
          "a restart clears the count");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    for (int32_t field = 0; field < 14; ++field)
    {
        mvfxKillRule rule = {.kind = mvfx_killInsideSphere, .radius = 1.0f};
        mvfxForce force = {.kind = mvfx_forceAttractor, .softening = 1.0f};
        mvfxEmitterDef bad = Held(&rule, 1);
        switch (field)
        {
        case 0:
            rule.kind = 6;
            break;
        case 1:
            rule.space = 2;
            break;
        case 2:
            rule.center[0] = (float)NAN;
            break;
        case 3:
            rule.radius = 0.0f;
            break;
        case 4:
            rule = (mvfxKillRule){
                .kind = mvfx_killInsideBox, .halfExtents = {1, 0, 1}, .rotation = {0, 0, 0, 1}};
            break;
        case 5:
            rule = (mvfxKillRule){
                .kind = mvfx_killOutsideBox, .halfExtents = {1, 1, 1}, .rotation = {0, 0, 0, 0.5f}};
            break;
        case 6:
            rule = (mvfxKillRule){.kind = mvfx_killPastPlane};
            break;
        case 7:
            rule = (mvfxKillRule){.kind = mvfx_killSlower, .speed = -1.0f};
            break;
        case 8:
            rule = (mvfxKillRule){.kind = mvfx_killSlower, .minimumAge = 2e6f};
            break;
        case 9:
            bad.killRuleCount = MVFX_MAX_KILL_RULES + 1;
            break;
        case 10:
            bad.killRules = NULL;
            break;
        case 11:
            bad = Held(NULL, 0);
            force.killRadius = -1.0f;
            bad.forces = &force;
            bad.forceCount = 1;
            break;
        case 12:
            rule = (mvfxKillRule){.kind = mvfx_killOutsideSphere, .radius = 2.0f * MVFX_MAX_EXTENT};
            break;
        default:
            bad = Held(NULL, 0);
            force.killRadius = 2.0f * MVFX_MAX_EXTENT;
            bad.forces = &force;
            bad.forceCount = 1;
            break;
        }
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = &bad;
        effectDef.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_errorInvalid, "refused");
    }
    mvfxDestroyContext(context);
}

int main(void)
{
    TestRegions();
    TestSpeedAndAge();
    TestAttractorAndSpaces();
    TestEdgesAndFrames();
    TestBirthsAndOrder();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
