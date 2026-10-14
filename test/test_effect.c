// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating and destroying effects: every refusal, the limit, stale ids,
// and the allocator.

#include "test_harness.h"

#include "maul-vfx/effect.h"

#include <math.h>
#include <stdlib.h>

// An allocator that counts what it holds and fails after a number of
// allocations.
typedef struct CountingAllocator
{
    int64_t blocks;
    int64_t allocations;
    int64_t failAfter;
} CountingAllocator;

static void* CountingAlloc(size_t size, size_t alignment, void* context)
{
    CountingAllocator* counter = context;
    if (counter->failAfter >= 0 && counter->allocations >= counter->failAfter)
    {
        return NULL;
    }
    counter->allocations += 1;
    counter->blocks += 1;
    // Nothing here asks for more than max_align_t.
    (void)alignment;
    return malloc(size);
}

static void CountingFree(void* memory, size_t size, size_t alignment, void* context)
{
    CountingAllocator* counter = context;
    (void)size;
    (void)alignment;
    counter->blocks -= 1;
    free(memory);
}

static mvfxContext* NewContext(CountingAllocator* counter, int32_t effects)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    if (counter != NULL)
    {
        def.allocator = (mvfxAllocator){CountingAlloc, CountingFree, counter};
    }
    def.limits.effects = effects;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context created");
    return context;
}

static mvfxEffectDef OneEmitter(const mvfxEmitterDef* emitter)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    return def;
}

static void TestCreateAndDestroy(void)
{
    mvfxContext* context = NewContext(NULL, 8);
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxEffectDef def = OneEmitter(&emitter);
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "created");
    CHECK(effect.index1 != 0, "a non-null id");
    CHECK(mvfxEffect_IsValid(context, effect), "valid while alive");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "destroyed");
    CHECK(!mvfxEffect_IsValid(context, effect), "stale after");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_errorStale, "destroying twice is stale");
    mvfxEffectId none = {0};
    CHECK(mvfxDestroyEffect(context, none) == mvfx_errorStale, "the null id is stale");
    CHECK(!mvfxEffect_IsValid(context, none), "the null id is not valid");
    mvfxEffectId beyond = {9999, 1};
    CHECK(!mvfxEffect_IsValid(context, beyond), "an id past the table is not valid");
    CHECK(mvfxContext_GetMisuse(context) == 0, "stale ids are not misuse");

    mvfxEffectId again = {0};
    CHECK(mvfxCreateEffect(context, &def, &again) == mvfx_success, "a slot is reused");
    CHECK(again.index1 == effect.index1 && again.generation != effect.generation,
          "with a new generation");
    CHECK(!mvfxEffect_IsValid(context, effect), "the old id stays stale");
    CHECK(mvfxDestroyEffect(context, again) == mvfx_success, "destroyed again");
    CHECK(mvfxDestroyEffect(NULL, again) == mvfx_errorInvalid, "no context");
    CHECK(!mvfxEffect_IsValid(NULL, again), "no context, not valid");
    mvfxDestroyContext(context);
}

static void TestLimit(void)
{
    mvfxContext* context = NewContext(NULL, 2);
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxEffectDef def = OneEmitter(&emitter);
    mvfxEffectId a = {0};
    mvfxEffectId b = {0};
    mvfxEffectId c = {1, 1};
    CHECK(mvfxCreateEffect(context, &def, &a) == mvfx_success, "first");
    CHECK(mvfxCreateEffect(context, &def, &b) == mvfx_success, "second");
    CHECK(mvfxCreateEffect(context, &def, &c) == mvfx_errorLimit, "the third is past the limit");
    CHECK(c.index1 == 0 && c.generation == 0, "the null id on failure");
    CHECK(mvfxContext_GetMisuse(context) == 0, "a limit is not misuse");
    CHECK(mvfxDestroyEffect(context, a) == mvfx_success, "one freed");
    CHECK(mvfxCreateEffect(context, &def, &c) == mvfx_success, "room again");
    mvfxDestroyContext(context);
}

static void TestInvalidDefsAreMisuse(void)
{
    mvfxContext* context = NewContext(NULL, 8);
    mvfxEffectId effect = {0};
    uint64_t misuse = 0;
    CHECK(mvfxCreateEffect(context, NULL, &effect) == mvfx_errorInvalid, "no def");
    CHECK(mvfxContext_GetMisuse(context) == ++misuse, "counted");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxEffectDef def = OneEmitter(&emitter);
    CHECK(mvfxCreateEffect(context, &def, NULL) == mvfx_errorInvalid, "no out-parameter");
    CHECK(mvfxContext_GetMisuse(context) == ++misuse, "counted");
    mvfxEffectDef def2 = OneEmitter(&emitter);
    CHECK(mvfxCreateEffect(NULL, &def2, &effect) == mvfx_errorInvalid, "no context");

    for (int32_t field = 0; field < 14; ++field)
    {
        mvfxEmitterDef bad = mvfxDefaultEmitterDef();
        mvfxEffectDef broken = OneEmitter(&bad);
        switch (field)
        {
        case 0:
            bad.cookie = 0;
            break;
        case 1:
            bad.capacity = 0;
            break;
        case 2:
            bad.capacity = MVFX_MAX_EMITTER_CAPACITY + 1;
            break;
        case 3:
            bad.rate = -1.0f;
            break;
        case 4:
            bad.rate = NAN;
            break;
        case 5:
            bad.rate = MVFX_MAX_SPAWN_RATE * 2.0f;
            break;
        case 6:
            bad.lifetime = (mvfxRange){0.0f, 1.0f};
            break;
        case 7:
            bad.lifetime = (mvfxRange){2.0f, 1.0f};
            break;
        case 8:
            bad.lifetime = (mvfxRange){1.0f, INFINITY};
            break;
        case 9:
            bad.lifetime = (mvfxRange){NAN, 1.0f};
            break;
        case 10:
            broken.cookie = 0;
            break;
        case 11:
            broken.emitterCount = 0;
            break;
        case 12:
            broken.emitterCount = MVFX_MAX_EMITTERS + 1;
            break;
        default:
            broken.emitters = NULL;
            break;
        }
        effect = (mvfxEffectId){7, 7};
        CHECK(mvfxCreateEffect(context, &broken, &effect) == mvfx_errorInvalid, "refused");
        CHECK(effect.index1 == 0, "the null id");
        CHECK(mvfxContext_GetMisuse(context) == ++misuse, "each refusal counted");
    }
    mvfxEmitterDef edges = mvfxDefaultEmitterDef();
    edges.capacity = MVFX_MAX_EMITTER_CAPACITY;
    edges.rate = 0.0f;
    edges.lifetime = (mvfxRange){MVFX_MAX_LIFETIME, MVFX_MAX_LIFETIME};
    mvfxEffectDef atEdges = OneEmitter(&edges);
    CHECK(mvfxCreateEffect(context, &atEdges, &effect) == mvfx_success, "every edge accepted");
    mvfxDestroyContext(context);
}

static void TestLifeFieldsRefused(void)
{
    mvfxContext* context = NewContext(NULL, 8);
    static const mvfxCurveKey ramp[2] = {{0.0f, 0.0f, 0.0f, 0.0f, mvfx_interpolationLinear},
                                         {1.0f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    static const mvfxCurveKey late[1] = {{1.5f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    static const mvfxCurveKey negative[1] = {{0.5f, -1.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    // A cubic overshooting below 0 between keys of 0 and 1.
    static const mvfxCurveKey dips[2] = {{0.0f, 0.0f, 0.0f, -20.0f, mvfx_interpolationCubic},
                                         {1.0f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    static const mvfxCurveKey high[1] = {{0.5f, 2.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    uint64_t misuse = mvfxContext_GetMisuse(context);
    for (int32_t field = 0; field < 22; ++field)
    {
        mvfxBurst burst = {.time = 0.0f,
                           .minCount = 1,
                           .maxCount = 4,
                           .cycles = 2,
                           .interval = 0.5f,
                           .probability = 1.0f};
        mvfxEmitterDef bad = mvfxDefaultEmitterDef();
        bad.duration = 1.0f;
        bad.bursts = &burst;
        bad.burstCount = 1;
        bad.rateCurve = (mvfxCurve){ramp, 2, mvfx_tangentsGiven};
        switch (field)
        {
        case 0:
            bad.startDelay = -1.0f;
            break;
        case 1:
            bad.startDelay = NAN;
            break;
        case 2:
            bad.duration = MVFX_MIN_DURATION * 0.5f;
            break;
        case 3:
            bad.duration = MVFX_MAX_LIFETIME * 2.0f;
            break;
        case 4:
            bad.loops = -1;
            break;
        case 5:
            bad.burstCount = MVFX_MAX_BURSTS + 1;
            break;
        case 6:
            bad.bursts = NULL;
            break;
        case 7:
            burst.time = -0.5f;
            break;
        case 8:
            burst.minCount = -1;
            break;
        case 9:
            burst.minCount = 5;
            break;
        case 10:
            burst.maxCount = MVFX_MAX_EMITTER_CAPACITY + 1;
            break;
        case 11:
            burst.cycles = -1;
            break;
        case 12:
            burst.interval = MVFX_MIN_DURATION * 0.5f;
            break;
        case 13:
            burst.probability = 1.5f;
            break;
        case 14:
            burst.probability = NAN;
            break;
        case 15:
            // Past the spawn rate: three million a firing, four firings a
            // second and one more each loop.
            burst.maxCount = 3000000;
            burst.cycles = 0;
            burst.interval = 0.25f;
            bad.capacity = 16;
            break;
        case 16:
            bad.duration = 0.0f;
            break;
        case 17:
            bad.rateCurve = (mvfxCurve){late, 1, mvfx_tangentsGiven};
            break;
        case 18:
            bad.rateCurve = (mvfxCurve){negative, 1, mvfx_tangentsGiven};
            break;
        case 19:
            bad.rateCurve = (mvfxCurve){dips, 2, mvfx_tangentsGiven};
            break;
        case 20:
            bad.rate = MVFX_MAX_SPAWN_RATE * 0.75f;
            bad.rateCurve = (mvfxCurve){high, 1, mvfx_tangentsGiven};
            break;
        default:
            bad.rateCurve.keyCount = -1;
            break;
        }
        mvfxEffectDef def = OneEmitter(&bad);
        mvfxEffectId effect = {7, 7};
        CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid, "refused");
        CHECK(effect.index1 == 0, "the null id");
        CHECK(mvfxContext_GetMisuse(context) == ++misuse, "each refusal counted");
    }
    // Every field at its edge, and a single burst's interval ignored.
    mvfxBurst edges[MVFX_MAX_BURSTS];
    for (int32_t b = 0; b < MVFX_MAX_BURSTS; ++b)
    {
        edges[b] = (mvfxBurst){.time = MVFX_MAX_LIFETIME,
                               .minCount = 0,
                               .maxCount = 0,
                               .cycles = 1,
                               .interval = -5.0f,
                               .probability = 0.0f};
    }
    edges[0] = (mvfxBurst){.time = 0.0f,
                           .minCount = MVFX_MAX_EMITTER_CAPACITY,
                           .maxCount = MVFX_MAX_EMITTER_CAPACITY,
                           .cycles = 1,
                           .probability = 1.0f};
    mvfxEmitterDef edge = mvfxDefaultEmitterDef();
    edge.startDelay = MVFX_MAX_LIFETIME;
    edge.duration = MVFX_MIN_DURATION;
    edge.loops = 1;
    edge.bursts = edges;
    edge.burstCount = MVFX_MAX_BURSTS;
    edge.rate = MVFX_MAX_SPAWN_RATE;
    edge.rateCurve = (mvfxCurve){ramp, 2, mvfx_tangentsGiven};
    mvfxEffectDef def = OneEmitter(&edge);
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "every edge accepted");
    edge.duration = MVFX_MAX_LIFETIME;
    edge.loops = 0;
    def = OneEmitter(&edge);
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "the longest loop");
    mvfxDestroyContext(context);
}

static void TestAllocator(void)
{
    CountingAllocator counter = {.failAfter = -1};
    mvfxContext* context = NewContext(&counter, 8);
    int64_t base = counter.blocks;
    mvfxEmitterDef emitters[3] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                  mvfxDefaultEmitterDef()};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 3;
    mvfxEffectId a = {0};
    mvfxEffectId b = {0};
    CHECK(mvfxCreateEffect(context, &def, &a) == mvfx_success, "first");
    CHECK(counter.blocks == base + 1, "one block per effect");
    CHECK(mvfxCreateEffect(context, &def, &b) == mvfx_success, "second");
    CHECK(mvfxDestroyEffect(context, a) == mvfx_success, "one destroyed");
    CHECK(counter.blocks == base + 1, "its block returned");
    counter.failAfter = counter.allocations;
    mvfxEffectId c = {0};
    CHECK(mvfxCreateEffect(context, &def, &c) == mvfx_errorCapacity, "allocator failure");
    CHECK(c.index1 == 0, "the null id");
    mvfxDestroyContext(context);
    CHECK(counter.blocks == 0, "destroying the context frees its live effects");

    CountingAllocator failing = {.failAfter = 1};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.allocator = (mvfxAllocator){CountingAlloc, CountingFree, &failing};
    mvfxContext* none = NULL;
    CHECK(mvfxCreateContext(&contextDef, &none) == mvfx_errorCapacity, "slot table fails");
    CHECK(none == NULL && failing.blocks == 0, "nothing kept");
    contextDef.limits.effects = 0;
    CHECK(mvfxCreateContext(&contextDef, &none) == mvfx_errorInvalid, "no effects allowed");
    contextDef.limits.effects = 65537;
    CHECK(mvfxCreateContext(&contextDef, &none) == mvfx_errorInvalid, "too many effects");
}

int main(void)
{
    TestCreateAndDestroy();
    TestLimit();
    TestInvalidDefsAreMisuse();
    TestLifeFieldsRefused();
    TestAllocator();
    return s_failures == 0 ? 0 : 1;
}
