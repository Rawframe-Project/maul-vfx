// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sorting: records back to front and front to back by distance and by
// depth with ties in the pool's order, youngest first, none; depths of
// every sign and size, subnormal ones too, in their order; many counts
// of particles against a stable sort of their depths, in five key
// shapes (any, a few values varying in the two high bytes, one byte,
// either sign, none); a local emitter sorted in the world; a buffer too
// small; the scratch query, scratch too small or misaligned; the view's
// checks and the def's.

#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

// The most particles a scene places.
enum
{
    MOST = 1000
};

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
} Scene;

static Scene NewScene(const mvfxEmitterDef* emitter)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    Scene scene = {0};
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    static const mvfxLayoutField serial = {
        .field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &serial;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 4;
    CHECK(mvfxCreateLayout(scene.context, &layoutDef, &scene.layout) == mvfx_success, "layout");
    return scene;
}

static mvfxEmitterDef Sorted(mvfxSortMode mode, mvfxSortMeasure measure)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.hostSpawnCapacity = 8;
    emitter.lifetime = mvfxMakeRangeSource(2.0f, 2.0f);
    emitter.sortMode = mode;
    emitter.sortMeasure = measure;
    return emitter;
}

// Places particles at the given points (serials 0, 1, ...), if any, and
// steps.
static void Place(Scene* scene, const double* points, int32_t count)
{
    mvfxSpawnRequest request = {.count = count, .positions = points};
    CHECK(count == 0 ||
              mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "placed");
    CHECK(mvfxContext_StepTicks(scene->context, 1) == mvfx_success, "step");
}

// Writes the records' serials, count of them, in the view's order.
static int64_t Serials(Scene* scene, const mvfxView* view, uint32_t* serials, int32_t capacity)
{
    static uint32_t scratch[MOST * 4];
    int64_t count = 0;
    CHECK(mvfxInstance_WriteParticles(scene->context, scene->instance, 0, scene->layout, view,
                                      scratch, sizeof scratch, serials, capacity,
                                      &count) == mvfx_success,
          "written");
    return count;
}

// The serials the records come in, as digits: 3102 for serials 3, 1, 0, 2.
static int32_t Order(Scene* scene, const mvfxView* view, int32_t capacity)
{
    uint32_t serials[8] = {0};
    int64_t count = Serials(scene, view, serials, capacity);
    int32_t digits = 0;
    for (int64_t i = 0; i < count && i < capacity; ++i)
    {
        digits = digits * 10 + (int32_t)serials[i];
    }
    return digits;
}

static int32_t OrderOf(mvfxSortMode mode, mvfxSortMeasure measure, const mvfxView* view)
{
    // x of 1, 5, 3 and 3 again; z of -1, -5, 2 and 2.
    static const double points[12] = {1, 0, -1, 5, 0, -5, 3, 0, 2, 3, 0, 2};
    mvfxEmitterDef emitter = Sorted(mode, measure);
    Scene scene = NewScene(&emitter);
    Place(&scene, points, 4);
    int32_t order = Order(&scene, view, 8);
    mvfxDestroyContext(scene.context);
    return order;
}

static void TestModes(void)
{
    // Distances squared 2, 50, 13, 13; depths along -z 1, 5, -2, -2.
    static const mvfxView view = {.forward = {0, 0, -1}};
    CHECK(OrderOf(mvfx_sortNone, mvfx_sortByDistance, &view) == 123, "none: the pool's");
    CHECK(OrderOf(mvfx_sortYoungestFirst, mvfx_sortByDistance, &view) == 3210, "youngest first");
    CHECK(OrderOf(mvfx_sortBackToFront, mvfx_sortByDistance, &view) == 1230,
          "back to front by distance, ties oldest first");
    CHECK(OrderOf(mvfx_sortFrontToBack, mvfx_sortByDistance, &view) == 231,
          "front to back by distance");
    CHECK(OrderOf(mvfx_sortBackToFront, mvfx_sortByDepth, &view) == 1023, "back to front by depth");
    CHECK(OrderOf(mvfx_sortFrontToBack, mvfx_sortByDepth, &view) == 2301, "front to back by depth");
    // From another eye: (6, 0, 0) sees distances squared 26, 26, 13, 13.
    static const mvfxView there = {.eye = {6, 0, 0}, .origin = {100, 0, 0}};
    CHECK(OrderOf(mvfx_sortBackToFront, mvfx_sortByDistance, &there) == 123,
          "from the view's eye, not its origin");
}

// Depths of every sign and size, subnormal ones among them, placed out of
// order along Z: front to back by depth along +Z gives them in their order.
static void TestDepths(void)
{
    static const float depths[10] = {-1e5f, -2.0f,     -1.0f,     -0x1p-126f, -0x1p-149f,
                                     0.0f,  0x1p-149f, 0x1p-126f, 1.0f,       1e5f};
    // The depths' indices in the order the particles are placed.
    static const int32_t placed[10] = {7, 2, 9, 0, 5, 4, 8, 1, 6, 3};
    double points[30] = {0};
    for (int32_t i = 0; i < 10; ++i)
    {
        points[3 * i + 2] = (double)depths[placed[i]];
    }
    mvfxEmitterDef emitter = Sorted(mvfx_sortFrontToBack, mvfx_sortByDepth);
    emitter.capacity = 10;
    emitter.hostSpawnCapacity = 10;
    Scene scene = NewScene(&emitter);
    Place(&scene, points, 10);
    static const mvfxView view = {.forward = {0, 0, 1}};
    uint32_t serials[10] = {0};
    bool ordered = Serials(&scene, &view, serials, 10) == 10;
    for (int32_t i = 0; i < 10 && ordered; ++i)
    {
        ordered = serials[i] < 10 && placed[serials[i]] == i;
    }
    CHECK(ordered, "depths of every sign and size in their order");
    mvfxDestroyContext(scene.context);
}

static uint32_t s_state = 0x12345678u;

static uint32_t Next(void)
{
    s_state ^= s_state << 13u;
    s_state ^= s_state >> 17u;
    s_state ^= s_state << 5u;
    return s_state;
}

// A depth of a shape: any float of some size and either sign; one of four
// values, whose keys vary in their two high bytes only; one varying in its
// key's second byte only; one of either sign varying in its exponent and
// high mantissa, whose keys vary in every byte, as a negative's key flips
// them all; always the same.
static float Depth(int32_t shape)
{
    uint32_t bits = 0;
    switch (shape)
    {
    case 0:
        return (float)(int32_t)Next() * 0x1p-20f;
    case 1:
        return (float)(Next() & 3u);
    case 2:
        bits = 0x3F800000u | ((Next() & 0xFFu) << 8u);
        break;
    case 3:
        bits = 0x3F800000u + ((Next() & 0x3FFu) << 16u) + (Next() & 0x80000000u);
        break;
    default:
        return 5.0f;
    }
    float depth = 0.0f;
    memcpy(&depth, &bits, sizeof depth);
    return depth;
}

// The pool's indices sorted by depth, front to back or back to front,
// equal depths in the pool's order: by insertion, as the reference.
static void Reference(const float* depths, uint32_t* order, int32_t count, bool backToFront)
{
    for (int32_t i = 0; i < count; ++i)
    {
        uint32_t index = (uint32_t)i;
        int32_t j = i - 1;
        while (j >= 0 &&
               (backToFront ? depths[order[j]] < depths[index] : depths[order[j]] > depths[index]))
        {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = index;
    }
}

// Each count of particles in each shape, both ways: the records in the
// reference's order.
static void TestStable(void)
{
    static double points[MOST * 3];
    static float depths[MOST];
    static uint32_t want[MOST];
    static uint32_t serials[MOST];
    static const mvfxView view = {.forward = {0, 0, 1}};
    const int32_t counts[5] = {0, 1, 2, 37, MOST};
    int32_t agreed = 0;
    for (int32_t way = 0; way < 2; ++way)
    {
        for (int32_t shape = 0; shape < 5; ++shape)
        {
            for (int32_t c = 0; c < 5; ++c)
            {
                int32_t count = counts[c];
                for (int32_t i = 0; i < count; ++i)
                {
                    depths[i] = Depth(shape);
                    points[3 * i + 2] = (double)depths[i];
                }
                Reference(depths, want, count, way == 1);
                mvfxEmitterDef emitter = Sorted(
                    way == 1 ? mvfx_sortBackToFront : mvfx_sortFrontToBack, mvfx_sortByDepth);
                emitter.capacity = MOST;
                emitter.hostSpawnCapacity = MOST;
                Scene scene = NewScene(&emitter);
                Place(&scene, points, count);
                bool same = Serials(&scene, &view, serials, MOST) == count &&
                            memcmp(serials, want, (size_t)count * sizeof *want) == 0;
                agreed += same ? 1 : 0;
                mvfxDestroyContext(scene.context);
            }
        }
    }
    CHECK(agreed == 50, "as the stable reference, every shape and count, both ways");
}

static void TestLocalAndCapacity(void)
{
    // A local emitter turned a half about Z: local x of 1 and 2 are the
    // world's -1 and -2, so from an eye at (10, 0, 0) the second is
    // farther.
    mvfxEmitterDef emitter = Sorted(mvfx_sortBackToFront, mvfx_sortByDistance);
    emitter.space = mvfx_spaceLocal;
    Scene scene = NewScene(&emitter);
    static const double origin[3] = {0, 0, 0};
    static const float half[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, half, 1.0f, true) ==
              mvfx_success,
          "turned");
    static const double points[9] = {1, 0, 0, 2, 0, 0, 0.5, 0, 0};
    Place(&scene, points, 3);
    static const mvfxView view = {.eye = {10, 0, 0}};
    CHECK(Order(&scene, &view, 8) == 102, "in the world");
    CHECK(Order(&scene, &view, 2) == 10, "a buffer too small takes the first");
    mvfxDestroyContext(scene.context);
}

static void TestScratch(void)
{
    mvfxEmitterDef emitter = Sorted(mvfx_sortFrontToBack, mvfx_sortByDistance);
    Scene scene = NewScene(&emitter);
    static const double points[9] = {3, 0, 0, 1, 0, 0, 2, 0, 0};
    Place(&scene, points, 3);
    mvfxContext* c = scene.context;
    mvfxInstanceId i = scene.instance;
    int64_t bytes = 0;
    CHECK(mvfxInstance_GetSortScratchBytes(c, i, 0, &bytes) == mvfx_success && bytes == 8 * 16,
          "16 bytes a particle of its capacity");
    static const mvfxView view = {0};
    static uint32_t scratch[13];
    uint32_t serials[3] = {9, 9, 9};
    int64_t count = -1;
    CHECK(mvfxInstance_WriteParticles(c, i, 0, scene.layout, &view, scratch, 47, serials, 3,
                                      &count) == mvfx_errorCapacity &&
              count == 0 && serials[0] == 0 && serials[1] == 0 && serials[2] == 0,
          "too small for three: the records zeroed");
    CHECK(mvfxInstance_WriteParticles(c, i, 0, scene.layout, &view, scratch, 48, serials, 3,
                                      &count) == mvfx_success &&
              count == 3 && serials[0] == 1 && serials[1] == 2 && serials[2] == 0,
          "enough for the live ones");
    uint64_t misuse = mvfxContext_GetMisuse(c);
    unsigned char* odd = (unsigned char*)scratch + 2;
    mvfxView far = {.eye = {0, 0x1p53, 0}};
    mvfxView farOrigin = {.origin = {0, 0, -0x1p53}};
    mvfxView nan = {.forward = {0, (float)NAN, 0}};
    mvfxView infinite = {.forward = {INFINITY, 0, 0}};
    mvfxLayoutId l = scene.layout;
    CHECK(mvfxInstance_WriteParticles(c, i, 0, l, &view, odd, 48, serials, 3, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, l, &view, scratch, -1, serials, 3, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, l, &view, NULL, 48, serials, 3, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, l, &far, scratch, 48, serials, 3, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, l, &farOrigin, scratch, 48, serials, 3,
                                          &count) == mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, l, &nan, scratch, 48, serials, 3, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, l, &infinite, scratch, 48, serials, 3, &count) ==
                  mvfx_errorInvalid &&
              mvfxInstance_WriteParticles(c, i, 0, l, NULL, scratch, 48, serials, 3, &count) ==
                  mvfx_errorInvalid,
          "refused");
    CHECK(mvfxInstance_GetSortScratchBytes(c, i, 1, &bytes) == mvfx_errorInvalid &&
              mvfxInstance_GetSortScratchBytes(c, i, 0, NULL) == mvfx_errorInvalid &&
              mvfxInstance_GetSortScratchBytes(NULL, i, 0, &bytes) == mvfx_errorInvalid,
          "the query refused");
    CHECK(mvfxContext_GetMisuse(c) == misuse + 10, "each counted");
    mvfxInstanceId stale = i;
    stale.generation += 1;
    CHECK(mvfxInstance_GetSortScratchBytes(c, stale, 0, &bytes) == mvfx_errorStale, "stale");
    bytes = 7;
    CHECK(mvfxInstance_GetSortScratchBytes(NULL, i, 0, &bytes) == mvfx_errorInvalid && bytes == 0,
          "no context: 0 bytes");
    mvfxDestroyContext(c);
    // No scratch for none and youngest first; none needed to write them.
    for (mvfxSortMode mode = mvfx_sortNone; mode <= mvfx_sortYoungestFirst; ++mode)
    {
        emitter = Sorted(mode, mvfx_sortByDepth);
        scene = NewScene(&emitter);
        Place(&scene, points, 3);
        CHECK(mvfxInstance_GetSortScratchBytes(scene.context, scene.instance, 0, &bytes) ==
                      mvfx_success &&
                  bytes == 0 &&
                  mvfxInstance_WriteParticles(scene.context, scene.instance, 0, scene.layout, &view,
                                              NULL, 0, serials, 3, &count) == mvfx_success &&
                  count == 3,
              "no scratch");
        mvfxDestroyContext(scene.context);
    }
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Sorted(4, mvfx_sortByDistance);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_errorInvalid,
          "a mode past the last");
    emitter = Sorted(mvfx_sortFrontToBack, 2);
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_errorInvalid,
          "a measure past the last");
    emitter = Sorted(mvfx_sortFrontToBack, mvfx_sortByDepth);
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "the last of each");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestModes();
    TestDepths();
    TestStable();
    TestLocalAndCapacity();
    TestScratch();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
