// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sorting: ordered keys across every kind of float; the radix sort
// against a stable insertion sort on many sizes and key shapes (bytes
// that never vary, so passes are skipped, and an odd number of passes);
// records back to front and front to back by distance and by depth with
// ties in the pool's order, youngest first, none; a local emitter sorted
// in the world; a buffer too small; the scratch query, scratch too small
// or misaligned; the view's checks and the def's.

#include "sort.h"
#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

static void TestKeys(void)
{
    const float values[11] = {-INFINITY, -2.0f, -1.0f, -0x1p-149f, -0.0f,   0.0f,
                              0x1p-149f, 1.0f,  2.0f,  3e38f,      INFINITY};
    bool increasing = true;
    for (int32_t i = 1; i < 11; ++i)
    {
        uint32_t before = mvfxOrderedKey(values[i - 1]);
        uint32_t after = mvfxOrderedKey(values[i]);
        increasing = increasing && (i == 5 ? before == after : before < after);
    }
    CHECK(increasing, "keys order as their values; -0 as +0");
}

static uint32_t s_state = 0x12345678u;

static uint32_t Next(void)
{
    s_state ^= s_state << 13u;
    s_state ^= s_state >> 17u;
    s_state ^= s_state << 5u;
    return s_state;
}

// Sorts by insertion, stable, as the reference.
static void Reference(uint32_t* keys, uint32_t* indices, int32_t count)
{
    for (int32_t i = 1; i < count; ++i)
    {
        uint32_t key = keys[i];
        uint32_t index = indices[i];
        int32_t j = i - 1;
        while (j >= 0 && keys[j] > key)
        {
            keys[j + 1] = keys[j];
            indices[j + 1] = indices[j];
            --j;
        }
        keys[j + 1] = key;
        indices[j + 1] = index;
    }
}

static void TestRadix(void)
{
    enum
    {
        MOST = 1000
    };
    static uint32_t keys[MOST];
    static uint32_t indices[MOST];
    static uint32_t wantKeys[MOST];
    static uint32_t wantIndices[MOST];
    static uint32_t temporaryKeys[MOST];
    static uint32_t temporaryIndices[MOST];
    // Key shapes: any bits; few values (ties); one byte varying (three
    // passes skipped, one done: an odd count); two high bytes; all equal.
    const uint32_t masks[5] = {0xFFFFFFFFu, 0x00000003u, 0x0000FF00u, 0xFFFF0000u, 0};
    const int32_t sizes[5] = {0, 1, 2, 37, MOST};
    int32_t agreed = 0;
    for (int32_t m = 0; m < 5; ++m)
    {
        for (int32_t z = 0; z < 5; ++z)
        {
            int32_t count = sizes[z];
            for (int32_t i = 0; i < count; ++i)
            {
                keys[i] = wantKeys[i] = (Next() & masks[m]) | 0x10u;
                indices[i] = wantIndices[i] = (uint32_t)i;
            }
            Reference(wantKeys, wantIndices, count);
            mvfxRadixSort(keys, indices, temporaryKeys, temporaryIndices, count);
            bool same = memcmp(keys, wantKeys, (size_t)count * 4) == 0 &&
                        memcmp(indices, wantIndices, (size_t)count * 4) == 0;
            agreed += same ? 1 : 0;
        }
    }
    CHECK(agreed == 25, "as the stable reference, every shape and size");
}

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
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
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
    emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
    emitter.sortMode = mode;
    emitter.sortMeasure = measure;
    return emitter;
}

// Places particles at the given points (serials 0, 1, ...) and steps.
static void Place(Scene* scene, const double* points, int32_t count)
{
    mvfxSpawnRequest request = {.count = count, .positions = points};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "placed");
    CHECK(mvfxContext_StepTicks(scene->context, 1) == mvfx_success, "step");
}

// The serials the records come in, as digits: 3102 for serials 3, 1, 0, 2.
static int32_t Order(Scene* scene, const mvfxView* view, int32_t capacity)
{
    static uint32_t scratch[8 * 4];
    uint32_t serials[8] = {0};
    int64_t count = 0;
    CHECK(mvfxInstance_WriteParticles(scene->context, scene->instance, 0, scene->layout, view,
                                      scratch, sizeof scratch, serials, capacity,
                                      &count) == mvfx_success,
          "written");
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
              count == 0 && serials[0] == 9,
          "too small for three: nothing written");
    CHECK(mvfxInstance_WriteParticles(c, i, 0, scene.layout, &view, scratch, 48, serials, 3,
                                      &count) == mvfx_success &&
              count == 3 && serials[0] == 1 && serials[1] == 2 && serials[2] == 0,
          "enough for the live ones");
    uint64_t misuse = mvfxGetContextMisuse(c);
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
    CHECK(mvfxGetContextMisuse(c) == misuse + 10, "each counted");
    mvfxInstanceId stale = i;
    stale.generation += 1;
    CHECK(mvfxInstance_GetSortScratchBytes(c, stale, 0, &bytes) == mvfx_errorStale, "stale");
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
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid, "a mode past the last");
    emitter = Sorted(mvfx_sortFrontToBack, 2);
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid, "a measure past the last");
    emitter = Sorted(mvfx_sortFrontToBack, mvfx_sortByDepth);
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "the last of each");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestKeys();
    TestRadix();
    TestModes();
    TestLocalAndCapacity();
    TestScratch();
    TestChecks();
    return s_failures == 0 ? 0 : 1;
}
