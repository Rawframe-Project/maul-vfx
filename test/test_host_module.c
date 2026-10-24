// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host modules: before motion, setting velocities that move the particles
// alive at the step's start; after motion, seeing the births and killing;
// the arrays (NULL for what is not stored), the call's step, indices and
// anchor; modules of a stage in order; the random stream fixed by its
// inputs; and the refusals.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitters, int32_t count)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = count;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.seed = 21;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    return scene;
}

// What the modules saw, written by them.
typedef struct Seen
{
    int32_t calls;
    int32_t count;
    bool killNull;
    bool colorNull;
    bool customNull;
    int32_t emitter;
    int32_t module;
    double seconds;
    int64_t startTick;
    int64_t endTick;
    double anchorX;
    bool worldSpace;
    uint32_t seed;
    int32_t order[4];
} Seen;

static void SetVelocity(const mvfxModuleCall* call, void* user)
{
    Seen* seen = user;
    seen->calls += 1;
    seen->count = call->particles.count;
    seen->killNull = call->particles.kill == NULL;
    for (int32_t i = 0; i < call->particles.count; ++i)
    {
        call->particles.velocity[0][i] = 2.0f;
    }
}

static void KillOdd(const mvfxModuleCall* call, void* user)
{
    Seen* seen = user;
    seen->calls += 1;
    seen->count = call->particles.count;
    seen->killNull = call->particles.kill == NULL;
    seen->colorNull = call->particles.color[0] == NULL;
    seen->customNull = call->particles.custom[0][1] == NULL &&
                       call->particles.custom[1][0] == NULL && call->particles.custom[0][0] != NULL;
    seen->emitter = call->emitter;
    seen->module = call->module;
    seen->seconds = call->seconds;
    seen->startTick = call->startTick;
    seen->endTick = call->endTick;
    seen->anchorX = call->anchor[0];
    seen->worldSpace = call->worldSpace;
    seen->seed = call->seed;
    for (int32_t i = 0; i < call->particles.count; ++i)
    {
        if ((call->particles.serial[i] & 1u) != 0)
        {
            call->particles.kill[i] = 1;
        }
    }
}

static void First(const mvfxModuleCall* call, void* user)
{
    (void)call;
    Seen* seen = user;
    seen->order[seen->calls++] = 1;
}

static void Second(const mvfxModuleCall* call, void* user)
{
    (void)call;
    Seen* seen = user;
    seen->order[seen->calls++] = 2;
}

// An emitter of what the host spawns, living ten seconds.
static mvfxEmitterDef Held(const mvfxHostModule* modules, int32_t count)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.hostModules = modules;
    emitter.hostModuleCount = count;
    return emitter;
}

static void Place(Scene* scene, int32_t emitter, double x)
{
    const double place[3] = {x, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = place};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, emitter, &request) == mvfx_success,
          "placed");
}

static void TestBeforeMotion(void)
{
    // A particle alive at the step's start gets velocity 2, so a second
    // moves it 2; the birth of that step is not seen before motion.
    Seen seen = {0};
    mvfxHostModule module = {
        .stage = mvfx_stageBeforeMotion, .function = SetVelocity, .user = &seen};
    mvfxEmitterDef emitter = Held(&module, 1);
    Scene scene = NewScene(&emitter, 1);
    Place(&scene, 0, 0.0);
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    CHECK(seen.calls == 1 && seen.count == 0 && seen.killNull, "the birth not seen before motion");
    Place(&scene, 0, 5.0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    TestParticle p[3];
    CHECK(ReadParticles(scene.context, scene.instance, 0, p, 3) == 2 && seen.count == 1 &&
              fabsf(p[0].position[0] - 2.0f) <= 1e-5f && p[1].position[0] == 5.0f,
          "the first moved by the velocity set, the new one not");
    mvfxDestroyContext(scene.context);
}

static void TestAfterMotion(void)
{
    // A second emitter, with one custom attribute of one component and no
    // colour, kills odd serials after motion, seeing the births.
    Seen seen = {0};
    mvfxHostModule module = {.stage = mvfx_stageAfterMotion, .function = KillOdd, .user = &seen};
    mvfxCustomAttribute custom = {.components = 1};
    mvfxEmitterDef emitters[2] = {Held(NULL, 0), Held(&module, 1)};
    emitters[1].custom = &custom;
    emitters[1].customCount = 1;
    Scene scene = NewScene(emitters, 2);
    static const double far[3] = {3000.0, 0.0, 0.0};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, far,
                                    (const float[4]){0, 0, 0, 1}, 1.0f, true) == mvfx_success,
          "far out");
    for (int32_t i = 0; i < 3; ++i)
    {
        Place(&scene, 1, 3000.0 + i);
    }
    CHECK(mvfxContext_StepTicks(scene.context, 1000) == mvfx_success, "step");
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(scene.context, scene.instance, 1, &stats) == mvfx_success,
          "stats");
    CHECK(seen.calls == 1 && seen.count == 3 && !seen.killNull && stats.live == 2 &&
              stats.killed == 1,
          "the births seen and one killed");
    CHECK(seen.colorNull && seen.customNull, "NULL for what is not stored");
    CHECK(seen.emitter == 1 && seen.module == 0 && seen.startTick == 0 && seen.endTick == 1000 &&
              seen.seconds == 1000.0 / MVFX_TICKS_PER_SECOND && seen.anchorX == 3000.0 &&
              seen.worldSpace,
          "the call's step, indices and anchor");
    mvfxDestroyContext(scene.context);
}

static void TestOrderAndRandom(void)
{
    Seen seen = {0};
    mvfxHostModule modules[3] = {
        {.stage = mvfx_stageAfterMotion, .function = Second, .user = &seen},
        {.stage = mvfx_stageBeforeMotion, .function = First, .user = &seen},
        {.stage = mvfx_stageAfterMotion, .function = First, .user = &seen},
    };
    mvfxEmitterDef emitter = Held(modules, 3);
    Scene scene = NewScene(&emitter, 1);
    // A finished instance steps nothing, its modules neither: one to keep
    // it going.
    Place(&scene, 0, 0.0);
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    CHECK(seen.calls == 3 && seen.order[0] == 1 && seen.order[1] == 2 && seen.order[2] == 1,
          "before motion first, then after motion in order");
    mvfxDestroyContext(scene.context);

    mvfxModuleCall call = {.seed = 7u, .endTick = 99};
    uint32_t a[4];
    uint32_t b[4];
    mvfxModule_Random(&call, 3u, 1u, a);
    mvfxModule_Random(&call, 3u, 1u, b);
    CHECK(a[0] == b[0] && a[3] == b[3], "the same inputs, the same words");
    mvfxModule_Random(&call, 4u, 1u, b);
    CHECK(a[0] != b[0], "another serial, other words");
    mvfxModule_Random(&call, 3u, 2u, b);
    CHECK(a[0] != b[0], "another stream, other words");
    call.endTick = 100;
    mvfxModule_Random(&call, 3u, 1u, b);
    CHECK(a[0] != b[0], "another step, other words");
    call.endTick = 99;
    call.seed = 8u;
    mvfxModule_Random(&call, 3u, 1u, b);
    CHECK(a[0] != b[0], "another seed, other words");
}

static void Record(const mvfxModuleCall* call, void* user)
{
    Seen* seen = user;
    seen->calls += 1;
    seen->count = call->particles.count;
    seen->module = call->module;
    seen->startTick = call->startTick;
    seen->worldSpace = call->worldSpace;
    seen->seed = call->seed;
}

static void TestCallsInFull(void)
{
    // The second module of a local emitter, in a second step: its index,
    // its space and the step's start.
    Seen seen = {0};
    Seen other = {0};
    mvfxHostModule modules[2] = {
        {.stage = mvfx_stageAfterMotion, .function = Record, .user = &other},
        {.stage = mvfx_stageAfterMotion, .function = Record, .user = &seen},
    };
    mvfxEmitterDef emitter = Held(modules, 2);
    emitter.space = mvfx_spaceLocal;
    Scene scene = NewScene(&emitter, 1);
    Place(&scene, 0, 0.0);
    CHECK(mvfxContext_StepTicks(scene.context, 100) == mvfx_success, "step");
    CHECK(mvfxContext_StepTicks(scene.context, 50) == mvfx_success, "step");
    CHECK(seen.module == 1 && !seen.worldSpace && seen.startTick == 100 && other.module == 0,
          "the second module of a local emitter in a second step");
    CHECK(seen.seed != other.seed, "each module its own stream");
    mvfxDestroyContext(scene.context);

    // A full emitter steps its births in tick order: its modules before
    // motion still run on the particles alive at the step's start.
    Seen full = {0};
    mvfxHostModule before = {
        .stage = mvfx_stageBeforeMotion, .function = SetVelocity, .user = &full};
    emitter = Held(&before, 1);
    emitter.capacity = 2;
    emitter.hostSpawnCapacity = 2;
    scene = NewScene(&emitter, 1);
    Place(&scene, 0, 0.0);
    Place(&scene, 0, 1.0);
    CHECK(mvfxContext_StepTicks(scene.context, 1) == mvfx_success, "step");
    Place(&scene, 0, 7.0);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    TestParticle p[3];
    CHECK(ReadParticles(scene.context, scene.instance, 0, p, 3) == 2 && full.count == 2 &&
              fabsf(p[0].position[0] - 2.0f) <= 1e-5f && fabsf(p[1].position[0] - 3.0f) <= 1e-5f,
          "before motion when the emitter is full");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    for (int32_t field = 0; field < 4; ++field)
    {
        mvfxHostModule module = {.stage = mvfx_stageAfterMotion, .function = First};
        mvfxEmitterDef bad = Held(&module, 1);
        switch (field)
        {
        case 0:
            module.function = NULL;
            break;
        case 1:
            module.stage = 2;
            break;
        case 2:
            bad.hostModuleCount = MVFX_MAX_HOST_MODULES + 1;
            break;
        default:
            bad.hostModules = NULL;
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
    TestBeforeMotion();
    TestAfterMotion();
    TestOrderAndRandom();
    TestCallsInFull();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
