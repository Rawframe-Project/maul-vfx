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
    CHECK(mvfxGetContextMisuse(context) == 0, "stale ids are not misuse");

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
    CHECK(mvfxGetContextMisuse(context) == 0, "a limit is not misuse");
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
    CHECK(mvfxGetContextMisuse(context) == ++misuse, "counted");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxEffectDef def = OneEmitter(&emitter);
    CHECK(mvfxCreateEffect(context, &def, NULL) == mvfx_errorInvalid, "no out-parameter");
    CHECK(mvfxGetContextMisuse(context) == ++misuse, "counted");
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
            bad.lifetime = mvfxRangeSource(0.0f, 1.0f);
            break;
        case 7:
            bad.lifetime = mvfxRangeSource(2.0f, 1.0f);
            break;
        case 8:
            bad.lifetime = mvfxRangeSource(1.0f, INFINITY);
            break;
        case 9:
            bad.lifetime = mvfxRangeSource(NAN, 1.0f);
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
        CHECK(mvfxGetContextMisuse(context) == ++misuse, "each refusal counted");
    }
    mvfxEmitterDef edges = mvfxDefaultEmitterDef();
    edges.capacity = MVFX_MAX_EMITTER_CAPACITY;
    edges.rate = 0.0f;
    edges.lifetime = mvfxRangeSource(MVFX_MAX_LIFETIME, MVFX_MAX_LIFETIME);
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
    uint64_t misuse = mvfxGetContextMisuse(context);
    for (int32_t field = 0; field < 24; ++field)
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
        case 22:
            // Past the spawn rate in a few cycles: three million a firing,
            // three firings a loop at a quarter second.
            burst.maxCount = 3000000;
            burst.cycles = 3;
            burst.interval = 0.25f;
            bad.capacity = 16;
            break;
        case 23:
            // A burst firing once a loop of a looping emitter: 16,777,216 a
            // second.
            burst.maxCount = MVFX_MAX_EMITTER_CAPACITY;
            burst.cycles = 1;
            bad.duration = 1.0f;
            bad.loops = 0;
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
        CHECK(mvfxGetContextMisuse(context) == ++misuse, "each refusal counted");
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

static void TestShapeAndVelocityRefused(void)
{
    mvfxContext* context = NewContext(NULL, 8);
    uint64_t misuse = mvfxGetContextMisuse(context);
    for (int32_t field = 0; field < 28; ++field)
    {
        mvfxEmitterDef bad = mvfxDefaultEmitterDef();
        mvfxShape* shape = &bad.shape;
        mvfxVelocity* velocity = &bad.velocity;
        shape->radius = 1.0f;
        shape->length = 1.0f;
        shape->angle = 0.5f;
        shape->halfExtents[0] = 1.0f;
        switch (field)
        {
        case 0:
            shape->kind = 8;
            break;
        case 1:
            shape->rotation[3] = 0.99f;
            break;
        case 2:
            shape->rotation[0] = NAN;
            break;
        case 3:
            shape->offset[1] = MVFX_MAX_EXTENT * 2.0f;
            break;
        case 4:
            shape->arcMode = 4;
            break;
        case 5:
            shape->arcSpeed = -1.0f;
            break;
        case 6:
            shape->thickness = 1.5f;
            break;
        case 7:
            shape->kind = mvfx_shapeLine;
            shape->length = -1.0f;
            break;
        case 8:
            shape->kind = mvfx_shapeCircle;
            shape->arc = 0.0f;
            break;
        case 9:
            shape->kind = mvfx_shapeCircle;
            shape->arc = 7.0f;
            break;
        case 10:
            shape->kind = mvfx_shapeCircle;
            shape->radius = NAN;
            break;
        case 11:
            shape->kind = mvfx_shapeSphere;
            shape->radius = -2.0f;
            break;
        case 12:
            shape->kind = mvfx_shapeHemisphere;
            shape->radius = INFINITY;
            break;
        case 13:
            shape->kind = mvfx_shapeBox;
            shape->halfExtents[2] = -1.0f;
            break;
        case 14:
            shape->kind = mvfx_shapeCone;
            shape->angle = MVFX_MAX_CONE_ANGLE + 0.01f;
            break;
        case 15:
            shape->kind = mvfx_shapeCone;
            shape->angle = -0.1f;
            break;
        case 16:
            // A top radius past the largest extent.
            shape->kind = mvfx_shapeCone;
            shape->angle = MVFX_MAX_CONE_ANGLE;
            shape->length = MVFX_MAX_EXTENT;
            break;
        case 17:
            shape->kind = mvfx_shapeCone;
            shape->arc = -1.0f;
            break;
        case 18:
            velocity->mode = 4;
            break;
        case 27:
            // The base's radius alone takes the top past the largest extent.
            shape->kind = mvfx_shapeCone;
            shape->radius = MVFX_MAX_EXTENT;
            shape->length = 1.0f;
            break;
        case 19:
            velocity->mode = mvfx_directionFixed;
            velocity->direction[0] = 0.0f;
            velocity->direction[1] = 0.0f;
            velocity->direction[2] = 0.0f;
            break;
        case 20:
            velocity->mode = mvfx_directionCone;
            velocity->direction[2] = NAN;
            break;
        case 21:
            velocity->coneAngle = 3.2f;
            break;
        case 22:
            velocity->randomize = -0.5f;
            break;
        case 23:
            velocity->speed = (mvfxRange){-1.0f, 1.0f};
            break;
        case 24:
            velocity->speed = (mvfxRange){2.0f, 1.0f};
            break;
        case 25:
            velocity->speed = (mvfxRange){0.0f, MVFX_MAX_EXTENT * 2.0f};
            break;
        case 26:
            velocity->direction[0] = INFINITY;
            break;
        default:
            break;
        }
        mvfxEffectDef def = OneEmitter(&bad);
        mvfxEffectId effect = {7, 7};
        CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid, "refused");
        CHECK(mvfxGetContextMisuse(context) == ++misuse, "each refusal counted");
    }
    // A kind's unread fields are not checked, and every edge is accepted.
    static const mvfxShapeKind kinds[7] = {mvfx_shapePoint,  mvfx_shapeLine,       mvfx_shapeCircle,
                                           mvfx_shapeSphere, mvfx_shapeHemisphere, mvfx_shapeBox,
                                           mvfx_shapeCone};
    for (int32_t k = 0; k < 7; ++k)
    {
        mvfxEmitterDef edge = mvfxDefaultEmitterDef();
        edge.shape.kind = kinds[k];
        edge.shape.radius = kinds[k] == mvfx_shapeCone ? 0.0f : MVFX_MAX_EXTENT;
        edge.shape.length = kinds[k] == mvfx_shapeCone ? 0.0f : MVFX_MAX_EXTENT;
        edge.shape.angle = kinds[k] == mvfx_shapeCone ? MVFX_MAX_CONE_ANGLE : -5.0f;
        edge.shape.halfExtents[0] = kinds[k] == mvfx_shapeBox ? MVFX_MAX_EXTENT : -1.0f;
        edge.shape.offset[0] = -MVFX_MAX_EXTENT;
        edge.shape.thickness = 0.0f;
        edge.shape.arcSpeed = 1000.0f;
        edge.shape.arcMode = mvfx_arcBurstSpread;
        edge.shape.rotation[3] = 1.0009f;
        edge.velocity.mode = mvfx_directionCone;
        edge.velocity.direction[0] = 1e-6f;
        edge.velocity.coneAngle = 3.14159265f;
        edge.velocity.randomize = 1.0f;
        edge.velocity.speed = (mvfxRange){MVFX_MAX_EXTENT, MVFX_MAX_EXTENT};
        mvfxEffectDef def = OneEmitter(&edge);
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "every edge accepted");
        CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "destroyed");
    }
    mvfxDestroyContext(context);
}

static void TestValuesRefused(void)
{
    mvfxContext* context = NewContext(NULL, 8);
    uint64_t misuse = mvfxGetContextMisuse(context);
    static const float parameters[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const float notFinite[4] = {0, 0, NAN, 0};
    static const mvfxCurveKey late[1] = {{2.0f, 1.0f, 0.0f, 0.0f, mvfx_interpolationLinear}};
    static const mvfxColorKey colors[1] = {{0.0f, 1.0f, 1.0f, 1.0f}};
    static const mvfxAlphaKey alphas[1] = {{0.0f, 1.0f}};
    for (int32_t field = 0; field < 24; ++field)
    {
        mvfxCustomAttribute custom = {.components = 2};
        mvfxEmitterDef bad = mvfxDefaultEmitterDef();
        mvfxEffectDef def = OneEmitter(&bad);
        def.parameters = parameters;
        def.parameterCount = 2;
        switch (field)
        {
        case 0:
            bad.lifetime.kind = mvfx_sourceNone;
            break;
        case 1:
            bad.lifetime = mvfxRangeSource(-1.0f, 1.0f);
            break;
        case 2:
            bad.lifetime =
                (mvfxScalarSource){.kind = mvfx_sourceParameter, .range = {1, 1}, .parameter = 2};
            break;
        case 3:
            bad.lifetime = (mvfxScalarSource){
                .kind = mvfx_sourceCurve, .range = {1, 1}, .curve = {late, 1, mvfx_tangentsGiven}};
            bad.duration = 1.0f;
            break;
        case 4:
            bad.size = (mvfxVectorSource){.kind = 9};
            break;
        case 5:
            bad.size = (mvfxVectorSource){.kind = mvfx_sourceRange, .max = {1, (float)INFINITY, 1}};
            break;
        case 6:
            bad.size =
                (mvfxVectorSource){.kind = mvfx_sourceGradient, .gradient = {colors, 1, alphas, 1}};
            break;
        case 7:
            bad.color = (mvfxVectorSource){.kind = mvfx_sourceGradientOverAge,
                                           .gradient = {colors, 1, alphas, 1}};
            break;
        case 8:
            bad.color =
                (mvfxVectorSource){.kind = mvfx_sourceGradient, .gradient = {colors, 0, alphas, 1}};
            break;
        case 9:
            bad.color = (mvfxVectorSource){.kind = mvfx_sourceParameter, .parameter = -1};
            break;
        case 10:
            bad.angle = mvfxRangeSource(1.0f, 0.0f);
            break;
        case 11:
            bad.spin = (mvfxScalarSource){.kind = 7};
            break;
        case 12:
            bad.orientationMode = 3;
            break;
        case 13:
            bad.orientationMode = mvfx_orientationEuler;
            break;
        case 14:
            bad.angularVelocity = (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {2e30f}};
            break;
        case 15:
            bad.frame = (mvfxFrameStart){3, 0, 0};
            break;
        case 16:
            bad.frame = (mvfxFrameStart){mvfx_frameRandom, 5, 4};
            break;
        case 17:
            bad.frame = (mvfxFrameStart){mvfx_frameRandom, 0, MVFX_MAX_FRAMES};
            break;
        case 18:
            bad.customCount = MVFX_MAX_CUSTOM_ATTRIBUTES + 1;
            bad.custom = &custom;
            break;
        case 19:
            bad.customCount = 1;
            break;
        case 20:
            custom.components = 5;
            bad.custom = &custom;
            bad.customCount = 1;
            break;
        case 21:
            def.parameterCount = MVFX_MAX_PARAMETERS + 1;
            break;
        case 22:
            def.parameters = notFinite;
            def.parameterCount = 1;
            break;
        default:
            def.parameters = NULL;
            break;
        }
        mvfxEffectId effect = {7, 7};
        CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid, "refused");
        CHECK(mvfxGetContextMisuse(context) == ++misuse, "each refusal counted");
    }
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
    TestShapeAndVelocityRefused();
    TestValuesRefused();
    TestAllocator();
    return s_failures == 0 ? 0 : 1;
}
