// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bounds and draw order: a world emitter's box of its particles grown by
// their size, empty with none and after a restart; a local emitter's box
// in the world under a moved, turned, scaled instance; fixed bounds
// through the instance's transform, reported with no particles and
// checked when made; an instance's box around its emitters'; the draw
// order by priority, distance, instance and emitter, a buffer too small
// and a frustum's skip; and the refusals.

#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>

static mvfxContext* NewContext(void)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    return context;
}

static mvfxEmitterDef Held(float size)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.hostSpawnCapacity = 8;
    emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                      .min = {size, size, size, 0},
                                      .max = {size, size, size, 0},
                                      .uniform = true};
    return emitter;
}

static mvfxEffectId NewEffect(mvfxContext* context, const mvfxEmitterDef* emitters, int32_t count)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = count;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "effect");
    return effect;
}

static mvfxInstanceId NewInstance(mvfxContext* context, mvfxEffectId effect)
{
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_success, "instance");
    return instance;
}

static void Place(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, double x,
                  double y, double z)
{
    const double place[3] = {x, y, z};
    mvfxSpawnRequest request = {.count = 1, .positions = place};
    CHECK(mvfxInstance_Spawn(context, instance, emitter, &request) == mvfx_success, "placed");
}

static bool Near(double a, double b)
{
    return fabs(a - b) <= 1e-4;
}

static bool IsBox(const mvfxBounds* b, double x0, double y0, double z0, double x1, double y1,
                  double z1)
{
    return !b->empty && Near(b->min[0], x0) && Near(b->min[1], y0) && Near(b->min[2], z0) &&
           Near(b->max[0], x1) && Near(b->max[1], y1) && Near(b->max[2], z1);
}

static void TestMeasured(void)
{
    mvfxContext* context = NewContext();
    mvfxEmitterDef emitters[2] = {Held(0.5f), Held(0.25f)};
    mvfxInstanceId instance = NewInstance(context, NewEffect(context, emitters, 2));
    mvfxBounds bounds = {0};
    CHECK(mvfxInstance_GetBounds(context, instance, 0, &bounds) == mvfx_success && bounds.empty &&
              bounds.min[0] == 0.0 && bounds.max[0] == 0.0,
          "empty before a step");
    Place(context, instance, 0, 1, 2, 3);
    Place(context, instance, 0, -1, 0, 5);
    Place(context, instance, 1, 4, 0, 0);
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success, "step");
    CHECK(mvfxInstance_GetBounds(context, instance, 0, &bounds) == mvfx_success &&
              IsBox(&bounds, -1.5, -0.5, 2.5, 1.5, 2.5, 5.5),
          "the particles' box grown by their size");
    CHECK(mvfxInstance_GetBounds(context, instance, 1, &bounds) == mvfx_success &&
              IsBox(&bounds, 3.75, -0.25, -0.25, 4.25, 0.25, 0.25),
          "the second emitter's");
    CHECK(mvfxInstance_GetBounds(context, instance, -1, &bounds) == mvfx_success &&
              IsBox(&bounds, -1.5, -0.5, -0.25, 4.25, 2.5, 5.5),
          "the instance's around both");
    CHECK(mvfxInstance_Restart(context, instance) == mvfx_success &&
              mvfxInstance_GetBounds(context, instance, -1, &bounds) == mvfx_success &&
              bounds.empty,
          "empty after a restart");
    mvfxDestroyContext(context);
}

static void TestLocal(void)
{
    // A local particle at (1, 0, 0) of size 1 under an instance at (100,
    // 0, 0) turned a quarter about Z at scale 2: its box [0, 2] x [-1, 1]
    // x [-1, 1] is the world's [98, 102] x [0, 4] x [-2, 2].
    mvfxContext* context = NewContext();
    mvfxEmitterDef emitter = Held(1.0f);
    emitter.space = mvfx_spaceLocal;
    mvfxInstanceId instance = NewInstance(context, NewEffect(context, &emitter, 1));
    static const double origin[3] = {100.0, 0.0, 0.0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(context, instance, origin, quarter, 2.0f, true) == mvfx_success,
          "placed");
    Place(context, instance, 0, 1, 0, 0);
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success, "step");
    mvfxBounds bounds = {0};
    CHECK(mvfxInstance_GetBounds(context, instance, 0, &bounds) == mvfx_success &&
              IsBox(&bounds, 98, 0, -2, 102, 4, 2),
          "in the world");
    mvfxDestroyContext(context);
}

static void TestFixed(void)
{
    // A fixed box [0, 1] x [0, 2] x [0, 3] under an instance at (10, 0, 0)
    // turned a quarter about Z at scale 2: [6, 10] x [0, 2] x [0, 6].
    mvfxContext* context = NewContext();
    mvfxEmitterDef emitter = Held(1.0f);
    emitter.fixedBounds = true;
    emitter.boundsMax[0] = 1.0f;
    emitter.boundsMax[1] = 2.0f;
    emitter.boundsMax[2] = 3.0f;
    mvfxInstanceId instance = NewInstance(context, NewEffect(context, &emitter, 1));
    static const double origin[3] = {10.0, 0.0, 0.0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    CHECK(mvfxInstance_SetTransform(context, instance, origin, quarter, 2.0f, true) == mvfx_success,
          "placed");
    mvfxBounds bounds = {0};
    CHECK(mvfxInstance_GetBounds(context, instance, 0, &bounds) == mvfx_success &&
              IsBox(&bounds, 6, 0, 0, 10, 2, 6),
          "the fixed box, with no particles");
    static const double eye[3] = {0, 0, 0};
    int64_t count = -1;
    CHECK(mvfxContext_OrderEmitters(context, eye, NULL, NULL, 0, &count) == mvfx_success &&
              count == 0,
          "not drawn with no particles");
    Place(context, instance, 0, 50, 50, 50);
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success, "step");
    CHECK(mvfxInstance_GetBounds(context, instance, 0, &bounds) == mvfx_success &&
              IsBox(&bounds, 6, 0, 0, 10, 2, 6) &&
              mvfxContext_OrderEmitters(context, eye, NULL, NULL, 0, &count) == mvfx_success &&
              count == 1,
          "the fixed box whatever its particles; drawn with them");
    // Checked when made: min above max, NaN, too large.
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId refused = {0};
    int32_t refusals = 0;
    for (int32_t rule = 0; rule < 4; ++rule)
    {
        emitter.boundsMin[1] = 0.0f;
        emitter.boundsMax[2] = 3.0f;
        switch (rule)
        {
        case 0:
            emitter.boundsMin[1] = 2.5f;
            break;
        case 1:
            emitter.boundsMin[1] = (float)NAN;
            break;
        case 2:
            emitter.boundsMax[2] = (float)NAN;
            break;
        default:
            emitter.boundsMax[2] = 2e6f;
            break;
        }
        refusals += mvfxCreateEffect(context, &def, &refused) == mvfx_errorInvalid ? 1 : 0;
    }
    CHECK(refusals == 4, "fixed bounds checked");
    emitter.fixedBounds = false;
    CHECK(mvfxCreateEffect(context, &def, &refused) == mvfx_success, "ignored when not fixed");
    mvfxDestroyContext(context);
}

static bool IsItem(const mvfxDrawItem* item, mvfxInstanceId instance, int32_t emitter,
                   int32_t priority, double distance)
{
    return item->instance.index1 == instance.index1 &&
           item->instance.generation == instance.generation && item->emitter == emitter &&
           item->priority == priority && item->distance == distance;
}

static void TestOrder(void)
{
    // Emitter 0 draws at priority 1, emitter 1 at 0. Instance a has both at
    // x 10 and 5, b at 20 and 30, c emitter 0 alone at 10 (as far as a's).
    mvfxContext* context = NewContext();
    mvfxEmitterDef emitters[2] = {Held(0.5f), Held(0.5f)};
    emitters[0].drawPriority = 1;
    mvfxEffectId effect = NewEffect(context, emitters, 2);
    mvfxInstanceId a = NewInstance(context, effect);
    mvfxInstanceId b = NewInstance(context, effect);
    mvfxInstanceId c = NewInstance(context, effect);
    Place(context, a, 0, 10, 0, 0);
    Place(context, a, 1, 5, 0, 0);
    Place(context, b, 0, 20, 0, 0);
    Place(context, b, 1, 30, 0, 0);
    Place(context, c, 0, 0, 10, 0);
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success, "step");
    static const double eye[3] = {0, 0, 0};
    mvfxDrawItem items[6] = {0};
    int64_t count = 0;
    CHECK(mvfxContext_OrderEmitters(context, eye, NULL, items, 6, &count) == mvfx_success &&
              count == 5,
          "five to draw");
    CHECK(IsItem(&items[0], b, 1, 0, 30.0) && IsItem(&items[1], a, 1, 0, 5.0) &&
              IsItem(&items[2], b, 0, 1, 20.0) && IsItem(&items[3], a, 0, 1, 10.0) &&
              IsItem(&items[4], c, 0, 1, 10.0),
          "by priority, farther first, then by instance");
    mvfxDrawItem few[3] = {0};
    CHECK(mvfxContext_OrderEmitters(context, eye, NULL, few, 3, &count) == mvfx_success &&
              count == 5 && IsItem(&few[0], b, 1, 0, 30.0) && IsItem(&few[1], a, 1, 0, 5.0) &&
              IsItem(&few[2], b, 0, 1, 20.0),
          "a buffer too small takes the first");
    // x at most 25 drops b's emitter 1; x at least 20.25 keeps b's alone
    // (its emitter 0 reaching 20.5).
    mvfxFrustum frustum = {.planes = {{-1, 0, 0, 25}}, .planeCount = 1};
    CHECK(mvfxContext_OrderEmitters(context, eye, &frustum, items, 6, &count) == mvfx_success &&
              count == 4 && IsItem(&items[0], a, 1, 0, 5.0) && IsItem(&items[3], c, 0, 1, 10.0),
          "outside a plane, skipped");
    frustum.planes[0][0] = 1;
    frustum.planes[0][3] = -20.25;
    CHECK(mvfxContext_OrderEmitters(context, eye, &frustum, items, 6, &count) == mvfx_success &&
              count == 2 && IsItem(&items[0], b, 1, 0, 30.0) && IsItem(&items[1], b, 0, 1, 20.0),
          "a box across a plane kept");
    // x at most 20.25 keeps b's emitter 0, its far side past it; x at
    // least 20.75 drops it, a quarter short.
    frustum.planes[0][0] = -1;
    frustum.planes[0][3] = 20.25;
    CHECK(mvfxContext_OrderEmitters(context, eye, &frustum, items, 6, &count) == mvfx_success &&
              count == 4 && IsItem(&items[1], b, 0, 1, 20.0),
          "a box across a plane facing back kept");
    frustum.planes[0][0] = 1;
    frustum.planes[0][3] = -20.75;
    CHECK(mvfxContext_OrderEmitters(context, eye, &frustum, items, 6, &count) == mvfx_success &&
              count == 1 && IsItem(&items[0], b, 1, 0, 30.0),
          "a box just outside skipped");
    frustum.planeCount = 0;
    static const double there[3] = {10, 0, 0};
    CHECK(mvfxContext_OrderEmitters(context, there, &frustum, items, 6, &count) == mvfx_success &&
              count == 5 && IsItem(&items[0], b, 1, 0, 20.0) && IsItem(&items[1], a, 1, 0, 5.0) &&
              IsItem(&items[2], c, 0, 1, sqrt(200.0)),
          "no planes keep all; distance from the eye");
    mvfxDestroyContext(context);
}

static void TestTies(void)
{
    // Two emitters of one priority as far from the eye: by emitter index.
    // The second's sizes differ by axis: its box grows by the largest.
    mvfxContext* context = NewContext();
    mvfxEmitterDef emitters[2] = {Held(0.5f), Held(0.5f)};
    emitters[1].size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.5f, 2.0f, 1.0f, 0}, .max = {0.5f, 2.0f, 1.0f, 0}};
    mvfxInstanceId instance = NewInstance(context, NewEffect(context, emitters, 2));
    Place(context, instance, 1, 10, 0, 0);
    Place(context, instance, 0, 0, 10, 0);
    CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success, "step");
    static const double eye[3] = {0, 0, 0};
    mvfxDrawItem items[2] = {0};
    int64_t count = 0;
    CHECK(mvfxContext_OrderEmitters(context, eye, NULL, items, 2, &count) == mvfx_success &&
              count == 2 && IsItem(&items[0], instance, 0, 0, 10.0) &&
              IsItem(&items[1], instance, 1, 0, 10.0),
          "by emitter index");
    mvfxBounds bounds = {0};
    CHECK(mvfxInstance_GetBounds(context, instance, 1, &bounds) == mvfx_success &&
              IsBox(&bounds, 8, -2, -2, 12, 2, 2),
          "grown by the largest size component");
    mvfxDestroyContext(context);
}

static void TestRefusals(void)
{
    mvfxContext* context = NewContext();
    mvfxEmitterDef emitter = Held(1.0f);
    mvfxInstanceId instance = NewInstance(context, NewEffect(context, &emitter, 1));
    uint64_t misuse = mvfxGetContextMisuse(context);
    mvfxBounds bounds = {0};
    CHECK(mvfxInstance_GetBounds(context, instance, 1, &bounds) == mvfx_errorInvalid &&
              mvfxInstance_GetBounds(context, instance, -2, &bounds) == mvfx_errorInvalid &&
              mvfxInstance_GetBounds(context, instance, 0, NULL) == mvfx_errorInvalid &&
              mvfxInstance_GetBounds(NULL, instance, 0, &bounds) == mvfx_errorInvalid,
          "bounds refused");
    mvfxInstanceId stale = instance;
    stale.generation += 1;
    CHECK(mvfxInstance_GetBounds(context, stale, 0, &bounds) == mvfx_errorStale, "stale");
    CHECK(mvfxGetContextMisuse(context) == misuse + 3, "each counted");
    static const double eye[3] = {0, 0, 0};
    static const double far[3] = {0, 0, 0x1p53};
    static const double farX[3] = {-0x1p53, 0, 0};
    static const double farY[3] = {0, 0x1p53, 0};
    mvfxFrustum many = {.planeCount = MVFX_MAX_FRUSTUM_PLANES + 1};
    mvfxFrustum negative = {.planeCount = -1};
    mvfxFrustum nan = {.planes = {{0, 0, 1, 0}, {0, 0, 0, (double)NAN}}, .planeCount = 2};
    mvfxFrustum infinite = {.planes = {{(double)INFINITY, 0, 0, 0}}, .planeCount = 1};
    mvfxDrawItem items[1];
    int64_t count = 0;
    CHECK(
        mvfxContext_OrderEmitters(context, NULL, NULL, items, 1, &count) == mvfx_errorInvalid &&
            mvfxContext_OrderEmitters(context, eye, NULL, items, 1, NULL) == mvfx_errorInvalid &&
            mvfxContext_OrderEmitters(context, eye, NULL, items, -1, &count) == mvfx_errorInvalid &&
            mvfxContext_OrderEmitters(context, eye, NULL, NULL, 1, &count) == mvfx_errorInvalid &&
            mvfxContext_OrderEmitters(context, far, NULL, items, 1, &count) == mvfx_errorInvalid &&
            mvfxContext_OrderEmitters(context, farX, NULL, items, 1, &count) == mvfx_errorInvalid &&
            mvfxContext_OrderEmitters(context, farY, NULL, items, 1, &count) == mvfx_errorInvalid &&
            mvfxContext_OrderEmitters(context, eye, &many, items, 1, &count) == mvfx_errorInvalid &&
            mvfxContext_OrderEmitters(context, eye, &negative, items, 1, &count) ==
                mvfx_errorInvalid &&
            mvfxContext_OrderEmitters(context, eye, &nan, items, 1, &count) == mvfx_errorInvalid &&
            mvfxContext_OrderEmitters(context, eye, &infinite, items, 1, &count) ==
                mvfx_errorInvalid &&
            mvfxContext_OrderEmitters(NULL, eye, NULL, items, 1, &count) == mvfx_errorInvalid,
        "order refused");
    CHECK(mvfxGetContextMisuse(context) == misuse + 14, "each counted");
    mvfxDestroyContext(context);
}

// Sizes over life: the box grows by the most a particle's size can be
// over its life (its stored size times the curve's largest value in size),
// so it holds the particles through the step without measuring each.
static void TestSizeOverLife(void)
{
    static const mvfxCurveKey peak[3] = {{.time = 0.0f, .value = 1.0f},
                                         {.time = 0.5f, .value = 3.0f},
                                         {.time = 1.0f, .value = 0.5f}};
    static const mvfxCurveKey below[2] = {{.time = 0.0f, .value = -2.0f},
                                          {.time = 1.0f, .value = 1.0f}};
    // The peak, a curve below 0, and the peak under a value below 0.
    const mvfxCurveKey* curves[3] = {peak, below, peak};
    const int32_t keys[3] = {3, 2, 3};
    const float values[3] = {1.0f, 1.0f, -1.0f};
    const double grown[3] = {1.5, 1.0, 1.5};
    for (int32_t c = 0; c < 3; ++c)
    {
        mvfxContext* context = NewContext();
        mvfxEmitterDef emitter = Held(0.5f);
        emitter.overLife.size = (mvfxSizeOverLife){
            .enabled = true,
            .size = {{.value = values[c], .curve = {.keys = curves[c], .keyCount = keys[c]}}}};
        mvfxInstanceId instance = NewInstance(context, NewEffect(context, &emitter, 1));
        Place(context, instance, 0, 1, 2, 3);
        CHECK(mvfxContext_StepTicks(context, 1) == mvfx_success, "step");
        mvfxBounds bounds = {0};
        double g = grown[c];
        CHECK(mvfxInstance_GetBounds(context, instance, 0, &bounds) == mvfx_success &&
                  IsBox(&bounds, 1 - g, 2 - g, 3 - g, 1 + g, 2 + g, 3 + g),
              "grown by the largest size over life");
        mvfxDestroyContext(context);
    }
}

int main(void)
{
    TestSizeOverLife();
    TestMeasured();
    TestLocal();
    TestFixed();
    TestOrder();
    TestTies();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
