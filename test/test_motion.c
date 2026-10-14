// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Moving instances: world and local space, turns and scales, teleports,
// births by distance along the way (alike however the move is split),
// the cap on a jump, inherited velocity, anchors that rebase without
// moving a particle, restart, and the refusals.

#include "context.h"
#include "instance.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxInstance* live;
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
    instanceDef.seed = 11;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    scene.live = scene.context->instances.slots[scene.instance.index1 - 1].object;
    return scene;
}

static const float s_identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};

static void MoveTo(const Scene* scene, double x, double y, double z, bool teleport)
{
    const double origin[3] = {x, y, z};
    CHECK(mvfxInstance_SetTransform(scene->context, scene->instance, origin, s_identity, 1.0f,
                                    teleport) == mvfx_success,
          "moved");
}

static const float* Array(const Scene* scene, mvfxAttribute attribute)
{
    return mvfxAttributeArray(scene->live, 0, attribute);
}

static void TestWorldAndLocal(void)
{
    // 64 a second over a step moving 16 units along X: in world space each
    // birth is where the instance was at its tick (x = 16 times its share of
    // the step), and stays there; in local space each is at the origin.
    for (int32_t space = 0; space < 2; ++space)
    {
        mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
        emitter.space = (mvfxSimulationSpace)space;
        emitter.lifetime = mvfxRangeSource(5.0f, 5.0f);
        Scene scene = NewScene(&emitter);
        MoveTo(&scene, 16.0, 0.0, 0.0, false);
        CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
        int32_t count = scene.live->emitters[0].count;
        const float* x = Array(&scene, mvfx_attributePositionX);
        bool placed = count == 64;
        for (int32_t k = 0; k < count; ++k)
        {
            float expected = space == mvfx_spaceWorld ? 16.0f * (float)(k + 1) / 64.0f : 0.0f;
            placed = placed && fabsf(x[k] - expected) <= 1e-4f;
        }
        CHECK(placed, space == mvfx_spaceWorld ? "born along the way" : "born at the origin");
        float first = x[0];
        MoveTo(&scene, 40.0, 0.0, 0.0, false);
        CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success,
              "step");
        CHECK(Array(&scene, mvfx_attributePositionX)[0] == first, "a particle stays put");
        mvfxDestroyContext(scene.context);
    }
}

static void TestTurnAndScale(void)
{
    // A quarter turn about Z at scale 2: a shape offset of (1, 0, 0) is born
    // at (0, 2, 0) from the instance, a fixed velocity (1, 0, 0) goes (0, 2,
    // 0).
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.shape.offset[0] = 1.0f;
    emitter.velocity =
        (mvfxVelocity){.mode = mvfx_directionFixed, .direction = {1, 0, 0}, .speed = {1.0f, 1.0f}};
    Scene scene = NewScene(&emitter);
    const double origin[3] = {5.0, 0.0, 0.0};
    const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 2.0f, true) ==
              mvfx_success,
          "teleported");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    bool turned = scene.live->emitters[0].count == 16;
    for (int32_t k = 0; k < scene.live->emitters[0].count; ++k)
    {
        turned = turned && fabsf(Array(&scene, mvfx_attributePositionX)[k] - 5.0f) <= 1e-5f &&
                 fabsf(Array(&scene, mvfx_attributePositionY)[k] - 2.0f) <= 1e-5f &&
                 fabsf(Array(&scene, mvfx_attributeVelocityX)[k]) <= 1e-5f &&
                 fabsf(Array(&scene, mvfx_attributeVelocityY)[k] - 2.0f) <= 1e-5f;
    }
    CHECK(turned, "turned and scaled; a teleport moves at once");
    mvfxDestroyContext(scene.context);
}

static void TestDistance(void)
{
    // Two a unit over ten units: twenty births, birth k where the travel
    // reaches (k + 1) / 2, and the same however the move is split.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.ratePerDistance = 2.0f;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    Scene whole = NewScene(&emitter);
    Scene split = NewScene(&emitter);
    MoveTo(&whole, 10.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(whole.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    MoveTo(&split, 3.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(split.context, 314573) == mvfx_success, "step");
    MoveTo(&split, 10.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(split.context, MVFX_TICKS_PER_SECOND - 314573) == mvfx_success,
          "step");
    bool along = whole.live->emitters[0].count == 20 && split.live->emitters[0].count == 20;
    for (int32_t k = 0; k < 20 && along; ++k)
    {
        float a = Array(&whole, mvfx_attributePositionX)[k];
        float b = Array(&split, mvfx_attributePositionX)[k];
        along = fabsf(a - 0.5f * (float)(k + 1)) <= 1e-4f && fabsf(a - b) <= 1e-4f;
    }
    CHECK(along, "born along the way, alike when the move is split");
    MoveTo(&whole, 10.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(whole.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(whole.live->emitters[0].count == 20, "standing still spawns nothing");
    MoveTo(&whole, 100.0, 0.0, 0.0, true);
    CHECK(mvfxContext_StepTicks(whole.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(whole.live->emitters[0].count == 20, "a teleport spawns nothing");
    mvfxDestroyContext(whole.context);
    mvfxDestroyContext(split.context);

    // Moving during a delay spawns nothing; after it, the rest of the way.
    emitter.startDelay = 0.5f;
    Scene delayed = NewScene(&emitter);
    MoveTo(&delayed, 10.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(delayed.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    mvfxEmitterStats stats;
    CHECK(mvfxInstance_GetEmitterStats(delayed.context, delayed.instance, 0, &stats) ==
              mvfx_success,
          "stats");
    CHECK(stats.owed == 10 && Array(&delayed, mvfx_attributePositionX)[0] > 5.0f,
          "only the second half of the way");
    mvfxDestroyContext(delayed.context);

    // A jump past the capacity: the earliest births refused, the last eight
    // born near the end.
    emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.ratePerDistance = 1.0f;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    Scene jump = NewScene(&emitter);
    MoveTo(&jump, 100.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(jump.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "step");
    CHECK(mvfxInstance_GetEmitterStats(jump.context, jump.instance, 0, &stats) == mvfx_success,
          "stats");
    CHECK(stats.owed == 100 && stats.refused == 92 && stats.live == 8, "the excess refused");
    CHECK(fabsf(Array(&jump, mvfx_attributePositionX)[0] - 93.0f) <= 1e-3f, "the last eight born");
    mvfxDestroyContext(jump.context);
}

static void TestInheritedVelocity(void)
{
    // Half the instance's velocity: 4 units in half a second is 8 a second,
    // so each particle sets off at 4.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.inheritVelocity = 0.5f;
    Scene scene = NewScene(&emitter);
    MoveTo(&scene, 0.0, 4.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "step");
    bool inherited = scene.live->emitters[0].count == 32;
    for (int32_t k = 0; k < scene.live->emitters[0].count; ++k)
    {
        inherited = inherited && Array(&scene, mvfx_attributeVelocityY)[k] == 4.0f &&
                    Array(&scene, mvfx_attributeVelocityX)[k] == 0.0f;
    }
    CHECK(inherited, "half the instance's velocity");
    MoveTo(&scene, 0.0, 100.0, 0.0, true);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
    CHECK(Array(&scene, mvfx_attributeVelocityY)[32] == 0.0f, "a teleport has no velocity");
    mvfxDestroyContext(scene.context);
}

static void TestRebase(void)
{
    // Far along X in steps: the anchor follows past the rebase distance,
    // and every particle keeps its place in the world.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitter.capacity = 4096;
    Scene scene = NewScene(&emitter);
    double places[64];
    MoveTo(&scene, 600.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(scene.live->emitters[0].anchor[0] == 0.0, "not yet rebased");
    for (int32_t k = 0; k < 64; ++k)
    {
        places[k] =
            scene.live->emitters[0].anchor[0] + (double)Array(&scene, mvfx_attributePositionX)[k];
    }
    MoveTo(&scene, 1100.0, 0.0, 0.0, false);
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    CHECK(scene.live->emitters[0].anchor[0] == 1100.0, "rebased to the instance");
    bool kept = true;
    for (int32_t k = 0; k < 64; ++k)
    {
        double place =
            scene.live->emitters[0].anchor[0] + (double)Array(&scene, mvfx_attributePositionX)[k];
        kept = kept && fabs(place - places[k]) <= 1e-4;
    }
    CHECK(kept, "every particle keeps its place");
    CHECK(fabsf(Array(&scene, mvfx_attributePositionX)[127]) <= 1e-3f,
          "new births near the anchor");

    CHECK(mvfxInstance_Restart(scene.context, scene.instance) == mvfx_success, "restart");
    CHECK(scene.live->emitters[0].anchor[0] == 1100.0 && scene.live->travelledTo == 0.0,
          "a restart anchors where the instance is, with no travel");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    Scene scene = NewScene(&emitter);
    uint64_t misuse = mvfxContext_GetMisuse(scene.context);
    const double origin[3] = {0.0, 0.0, 0.0};
    const double far[3] = {0.0, 0x1p53, 0.0};
    const float half[4] = {0.0f, 0.0f, 0.0f, 0.5f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, far, s_identity, 1.0f, false) ==
              mvfx_errorInvalid,
          "too far");
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, half, 1.0f, false) ==
              mvfx_errorInvalid,
          "not a unit quaternion");
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, s_identity, 0.0f,
                                    false) == mvfx_errorInvalid,
          "scale 0");
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, NULL, s_identity, 1.0f, false) ==
              mvfx_errorInvalid,
          "no origin");
    CHECK(mvfxContext_GetMisuse(scene.context) == misuse + 4, "each counted");
    mvfxInstanceId stale = scene.instance;
    stale.generation += 1;
    CHECK(mvfxInstance_SetTransform(scene.context, stale, origin, s_identity, 1.0f, false) ==
              mvfx_errorStale,
          "stale");
    CHECK(mvfxInstance_SetTransform(NULL, scene.instance, origin, s_identity, 1.0f, false) ==
              mvfx_errorInvalid,
          "no context");
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = (mvfxEffectId){1, 1};
    def.scale = 2e6f;
    mvfxInstanceId none = {0};
    CHECK(mvfxCreateInstance(scene.context, &def, &none) == mvfx_errorInvalid, "a def's scale");
    def.scale = 1.0f;
    def.rotation[3] = 0.9f;
    CHECK(mvfxCreateInstance(scene.context, &def, &none) == mvfx_errorInvalid, "a def's rotation");
    mvfxDestroyContext(scene.context);

    // Emitter fields: a local emitter cannot inherit; ranges.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    for (int32_t field = 0; field < 5; ++field)
    {
        mvfxEmitterDef bad = mvfxDefaultEmitterDef();
        switch (field)
        {
        case 0:
            bad.space = mvfx_spaceLocal;
            bad.inheritVelocity = 1.0f;
            break;
        case 1:
            bad.space = 2;
            break;
        case 2:
            bad.ratePerDistance = -1.0f;
            break;
        case 3:
            bad.ratePerDistance = MVFX_MAX_DISTANCE_RATE * 2.0f;
            break;
        default:
            bad.inheritVelocity = 11.0f;
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
    TestWorldAndLocal();
    TestTurnAndScale();
    TestDistance();
    TestInheritedVelocity();
    TestRebase();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
