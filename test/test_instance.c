// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating, destroying and reading instances, and the step's refusals;
// the bytes an instance and its effect hold, sized by what the effect has;
// a host's allocator asked for powers of two, its memory aligned exactly
// as asked enough, each block given back as it was asked; and memory past
// what a size_t counts refused, never served short.

#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static mvfxContext* NewContext(int32_t instances)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    def.limits.instances = instances;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context created");
    return context;
}

static mvfxEffectId NewEffect(mvfxContext* context)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect) == mvfx_success, "effect created");
    return effect;
}

static void TestLifecycle(void)
{
    mvfxContext* context = NewContext(4);
    mvfxEffectId effect = NewEffect(context);
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    def.origin[0] = 1e9;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_success, "created");
    CHECK(mvfxInstance_IsValid(context, instance), "valid");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_errorBusy, "its effect is busy");
    mvfxEmitterStats stats = {.status = 9, .live = 1, .owed = 1, .refused = 1};
    CHECK(mvfxInstance_GetEmitterStats(context, instance, 0, &stats) == mvfx_success, "stats");
    CHECK(stats.live == 0 && stats.owed == 0 && stats.refused == 0, "nothing before a step");
    CHECK(stats.status == mvfx_emitterActive, "active from the start");
    CHECK(mvfxDestroyInstance(context, instance) == mvfx_success, "destroyed");
    CHECK(!mvfxInstance_IsValid(context, instance), "stale after");
    CHECK(mvfxDestroyInstance(context, instance) == mvfx_errorStale, "twice is stale");
    CHECK(mvfxInstance_GetEmitterStats(context, instance, 0, &stats) == mvfx_errorStale,
          "stats of a stale instance");
    CHECK(stats.live == 0 && stats.owed == 0 && stats.status == 0, "zeroed on failure");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "free once its instance is gone");
    CHECK(mvfxGetContextMisuse(context) == 0, "no misuse so far");
    mvfxDestroyContext(context);
}

static void TestRefusals(void)
{
    mvfxContext* context = NewContext(2);
    mvfxEffectId effect = NewEffect(context);
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    mvfxInstanceId instance = {5, 5};
    uint64_t misuse = 0;
    CHECK(mvfxCreateInstance(context, NULL, &instance) == mvfx_errorInvalid, "no def");
    CHECK(instance.index1 == 0 && mvfxGetContextMisuse(context) == ++misuse, "counted");
    CHECK(mvfxCreateInstance(context, &def, NULL) == mvfx_errorInvalid, "no out-parameter");
    CHECK(mvfxGetContextMisuse(context) == ++misuse, "counted");
    CHECK(mvfxCreateInstance(NULL, &def, &instance) == mvfx_errorInvalid, "no context");
    mvfxInstanceDef bad = def;
    bad.cookie = 0;
    CHECK(mvfxCreateInstance(context, &bad, &instance) == mvfx_errorInvalid, "no cookie");
    CHECK(mvfxGetContextMisuse(context) == ++misuse, "counted");
    static const double origins[] = {(double)NAN, (double)INFINITY, 0x1p53};
    for (int32_t i = 0; i < 3; ++i)
    {
        bad = def;
        bad.origin[i] = origins[i];
        CHECK(mvfxCreateInstance(context, &bad, &instance) == mvfx_errorInvalid, "bad origin");
        CHECK(mvfxGetContextMisuse(context) == ++misuse, "counted");
    }
    bad = def;
    bad.effect = (mvfxEffectId){effect.index1, effect.generation + 1};
    CHECK(mvfxCreateInstance(context, &bad, &instance) == mvfx_errorStale, "a stale effect");
    mvfxInstanceId a = {0};
    mvfxInstanceId b = {0};
    CHECK(mvfxCreateInstance(context, &def, &a) == mvfx_success, "first");
    CHECK(mvfxCreateInstance(context, &def, &b) == mvfx_success, "second");
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_errorCapacity, "past the table");
    mvfxEmitterStats stats;
    CHECK(mvfxInstance_GetEmitterStats(context, a, 1, &stats) == mvfx_errorInvalid, "no emitter 1");
    CHECK(mvfxInstance_GetEmitterStats(context, a, -1, &stats) == mvfx_errorInvalid,
          "no emitter -1");
    CHECK(mvfxInstance_GetEmitterStats(context, a, 0, NULL) == mvfx_errorInvalid, "no out");
    misuse += 3;
    CHECK(mvfxGetContextMisuse(context) == misuse, "each counted");
    CHECK(mvfxGetContextMisuse(context) == misuse && misuse == 9, "nine refusals");

    CHECK(mvfxContext_Step(context, -0.001) == mvfx_errorInvalid, "a negative step");
    CHECK(mvfxContext_Step(context, (double)NAN) == mvfx_errorInvalid, "a NaN step");
    CHECK(mvfxContext_Step(context, MVFX_MAX_STEP_SECONDS * 1.01) == mvfx_errorInvalid,
          "too long a step");
    CHECK(mvfxContext_StepTicks(context, -1) == mvfx_errorInvalid, "negative ticks");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND + 1) == mvfx_errorInvalid,
          "too many ticks");
    CHECK(mvfxGetContextMisuse(context) == misuse + 5, "each counted");
    CHECK(mvfxContext_Step(NULL, 0.1) == mvfx_errorInvalid, "no context");
    CHECK(mvfxContext_StepTicks(NULL, 1) == mvfx_errorInvalid, "no context, ticks");
    CHECK(mvfxContext_Step(context, 0.0) == mvfx_success, "a zero step");
    CHECK(mvfxContext_Step(context, MVFX_MAX_STEP_SECONDS) == mvfx_success, "the longest step");
    mvfxDestroyContext(context);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.limits.instances = 0;
    mvfxContext* none = NULL;
    CHECK(mvfxCreateContext(&contextDef, &none) == mvfx_errorInvalid, "no instances allowed");
    contextDef.limits.instances = 65537;
    CHECK(mvfxCreateContext(&contextDef, &none) == mvfx_errorInvalid, "too many instances");
}

// The bytes a counting allocator holds, each block's start kept before it.
static int64_t s_held;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    (void)context;
    size_t front = alignment > 16 ? alignment : 16;
    unsigned char* block = malloc(size + front + alignment);
    if (block == NULL)
    {
        return NULL;
    }
    uintptr_t start = ((uintptr_t)block + front + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void**)start)[-1] = block;
    s_held += (int64_t)size;
    return (void*)start;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    if (memory != NULL)
    {
        free(((void**)memory)[-1]);
        s_held -= (int64_t)size;
    }
}

// An effect of one plain emitter holds no gas or water room: its compiled
// form and an instance of it are a few KiB, not the tens they were with
// every gas's state inline.
static void TestMemory(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.allocator = (mvfxAllocator){.alloc = Allocate, .free = Free};
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context created");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 16;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    int64_t before = s_held;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect created");
    int64_t effectBytes = s_held - before;
    CHECK(effectBytes > 0 && effectBytes <= 16384, "a plain effect within 16 KiB");
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    mvfxInstanceId instance = {0};
    before = s_held;
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_success, "created");
    int64_t instanceBytes = s_held - before;
    CHECK(instanceBytes > 0 && instanceBytes <= 6144, "a plain instance of 16 within 6 KiB");
    mvfxDestroyContext(context);
    CHECK(s_held == 0, "everything given back");
}

// The blocks an exact allocator serves: each one's address, size and
// alignment, and its start.
#define MAX_BLOCKS 256

typedef struct ExactBlock
{
    void* memory;
    void* start;
    size_t size;
    size_t alignment;
} ExactBlock;

typedef struct ExactAllocator
{
    ExactBlock blocks[MAX_BLOCKS];
    int32_t held;
    int32_t served;
    int32_t odd;
    int32_t mismatched;
} ExactAllocator;

// Memory aligned to exactly what was asked and never to twice it, so
// asking for less than the library's data needs puts it out of line.
static void* AllocateExactly(size_t size, size_t alignment, void* context)
{
    ExactAllocator* exact = context;
    exact->odd += alignment == 0 || (alignment & (alignment - 1)) != 0 ? 1 : 0;
    if (exact->odd > 0 || exact->held == MAX_BLOCKS)
    {
        return NULL;
    }
    unsigned char* start = malloc(size + 3 * alignment);
    if (start == NULL)
    {
        return NULL;
    }
    uintptr_t twice = 2 * (uintptr_t)alignment;
    uintptr_t at = (((uintptr_t)start + twice - 1) & ~(twice - 1)) + alignment;
    exact->blocks[exact->held++] = (ExactBlock){(void*)at, start, size, alignment};
    exact->served += 1;
    return (void*)at;
}

static void FreeExactly(void* memory, size_t size, size_t alignment, void* context)
{
    ExactAllocator* exact = context;
    for (int32_t b = 0; b < exact->held; ++b)
    {
        if (exact->blocks[b].memory == memory)
        {
            exact->mismatched +=
                exact->blocks[b].size != size || exact->blocks[b].alignment != alignment ? 1 : 0;
            free(exact->blocks[b].start);
            exact->blocks[b] = exact->blocks[--exact->held];
            return;
        }
    }
    exact->mismatched += 1;
}

// A host's allocator is asked for powers of two, and memory aligned to
// exactly what it was asked is enough: an effect of every kind of
// particle value, a gas and a water surface, stepped and read, out of
// line nowhere (the sanitizers trap on a misaligned access). Each block is
// given back with the size and alignment it was asked for.
static void TestExactAlignmentIsEnough(void)
{
    static ExactAllocator exact;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.allocator = (mvfxAllocator){AllocateExactly, FreeExactly, &exact};
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context created");
    mvfxVectorSource set = {
        .kind = mvfx_sourceRange, .min = {0.5f, 0.5f, 0.5f, 0.5f}, .max = {1, 1, 1, 1}};
    mvfxCustomAttribute custom[2] = {{.components = 3, .initial = set}, {.components = 1}};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 37;
    emitter.rate = 200.0f;
    emitter.size = set;
    emitter.color = set;
    emitter.angle = mvfxRangeSource(0.0f, 1.0f);
    emitter.orientationMode = mvfx_orientationRandom;
    emitter.frame = (mvfxFrameStart){mvfx_frameRandom, 2, 9};
    emitter.motion.dragScale = mvfxRangeSource(0.5f, 2.0f);
    emitter.collision.enabled = true;
    emitter.custom = custom;
    emitter.customCount = 2;
    mvfxGasDef gas = mvfxDefaultGasDef();
    mvfxWaterDef water = mvfxDefaultWaterDef();
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    effectDef.gases = &gas;
    effectDef.gasCount = 1;
    effectDef.waters = &water;
    effectDef.waterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect created");
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_success, "instance created");
    for (int32_t s = 0; s < 8; ++s)
    {
        CHECK(mvfxContext_Step(context, 1.0 / 60.0) == mvfx_success, "stepped");
    }
    mvfxEmitterStats stats = {0};
    CHECK(mvfxInstance_GetEmitterStats(context, instance, 0, &stats) == mvfx_success &&
              stats.live > 0,
          "particles alive");
    CHECK(mvfxDestroyInstance(context, instance) == mvfx_success &&
              mvfxDestroyEffect(context, effect) == mvfx_success,
          "destroyed");
    mvfxDestroyContext(context);
    CHECK(exact.served > 0 && exact.odd == 0, "powers of two asked");
    CHECK(exact.held == 0 && exact.mismatched == 0, "every block given back as it was asked");
}

// An allocator serving blocks below 64 MiB, which remembers whether it was
// asked for a block of at least that but less than `need` bytes.
typedef struct BoundedAllocator
{
    uint64_t need;
    int32_t shortAsks;
} BoundedAllocator;

static void* AllocateBounded(size_t size, size_t alignment, void* context)
{
    BoundedAllocator* bounded = context;
    if (size < ((size_t)1 << 26))
    {
        return Allocate(size, alignment, NULL);
    }
    bounded->shortAsks += (uint64_t)size < bounded->need ? 1 : 0;
    return NULL;
}

// Instances whose memory passes what a size_t counts on a 32-bit target
// are refused with mvfx_errorCapacity, never served short: one emitter of
// 67 particle arrays of 2^24 floats, whose arrays alone pass 2^32 bytes,
// and two of 40 such arrays, each below it and together past it. Where a
// size_t counts them, the allocator is asked for at least that much.
static void TestMemoryPastSizeIsRefused(void)
{
    static const int32_t arrays[2] = {67, 40};
    static const int32_t emitterCounts[2] = {1, 2};
    for (int32_t c = 0; c < 2; ++c)
    {
        BoundedAllocator bounded = {.need = (uint64_t)emitterCounts[c] * (uint64_t)arrays[c] *
                                            MVFX_MAX_EMITTER_CAPACITY * 4};
        mvfxContextDef contextDef = mvfxDefaultContextDef();
        contextDef.allocator = (mvfxAllocator){AllocateBounded, Free, &bounded};
        mvfxContext* context = NULL;
        CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context created");
        mvfxVectorSource set = {.kind = mvfx_sourceRange};
        mvfxCustomAttribute custom[MVFX_MAX_CUSTOM_ATTRIBUTES];
        for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
        {
            custom[i] = (mvfxCustomAttribute){.components = 4};
        }
        // Always 10 arrays; with 7 custom attributes of 4 and a sprite's
        // angle and spin, 40; with 8 custom attributes, size, color, the
        // sprite, a turn, a frame, a drag multiplier and scene queries, 67.
        mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
        emitter.capacity = MVFX_MAX_EMITTER_CAPACITY;
        emitter.angle = mvfxRangeSource(0.0f, 1.0f);
        emitter.custom = custom;
        emitter.customCount = 7;
        if (arrays[c] == 67)
        {
            emitter.customCount = 8;
            emitter.size = set;
            emitter.color = set;
            emitter.orientationMode = mvfx_orientationRandom;
            emitter.frame = (mvfxFrameStart){mvfx_frameRandom, 2, 9};
            emitter.motion.dragScale = mvfxRangeSource(0.5f, 2.0f);
            emitter.collision.enabled = true;
            emitter.collision.sceneQueries = true;
            emitter.collision.queryCapacity = 1;
            emitter.collision.lookahead = 0.5f;
        }
        mvfxEmitterDef emitters[2] = {emitter, emitter};
        mvfxEffectDef effectDef = mvfxDefaultEffectDef();
        effectDef.emitters = emitters;
        effectDef.emitterCount = emitterCounts[c];
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect created");
        mvfxInstanceDef def = mvfxDefaultInstanceDef();
        def.effect = effect;
        mvfxInstanceId instance = {0};
        CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_errorCapacity,
              "refused with capacity");
        CHECK(bounded.shortAsks == 0, "never asked for less than the arrays need");
        mvfxDestroyContext(context);
    }
    CHECK(s_held == 0, "everything given back");
}

// The case below runs only where a size_t counts 32 bits; the web build
// is where CI has one, so a web build of another width must not skip it
// unseen.
#if defined(__EMSCRIPTEN__) && SIZE_MAX != UINT32_MAX
#error "a web build without a 32-bit size_t skips TestBlockNearSizeIsRefused"
#endif

#if SIZE_MAX == UINT32_MAX
// The largest block a probing allocator was asked for and refused: it
// serves blocks below 64 MiB and refuses the rest.
static size_t s_largestAsked;

static void* AllocateProbing(size_t size, size_t alignment, void* context)
{
    (void)context;
    if (size < ((size_t)1 << 26))
    {
        return Allocate(size, alignment, NULL);
    }
    s_largestAsked = size > s_largestAsked ? size : s_largestAsked;
    return NULL;
}

// The block an instance of one emitter of 60 particle arrays asks for, at
// this capacity and with this many host spawns reserved; SIZE_MAX where it
// asks for none (its memory past what a size_t counts).
static size_t BlockFor(mvfxContext* context, int32_t capacity, int32_t hostSpawns)
{
    mvfxVectorSource set = {.kind = mvfx_sourceRange};
    mvfxCustomAttribute custom[MVFX_MAX_CUSTOM_ATTRIBUTES];
    for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
    {
        custom[i] = (mvfxCustomAttribute){.components = 4};
    }
    // Always 10 arrays; 8 custom attributes of 4, size, color, the
    // sprite, a turn, a frame and a drag multiplier: 60.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = capacity;
    emitter.hostSpawnCapacity = hostSpawns;
    emitter.custom = custom;
    emitter.customCount = MVFX_MAX_CUSTOM_ATTRIBUTES;
    emitter.size = set;
    emitter.color = set;
    emitter.angle = mvfxRangeSource(0.0f, 1.0f);
    emitter.orientationMode = mvfx_orientationRandom;
    emitter.frame = (mvfxFrameStart){mvfx_frameRandom, 2, 9};
    emitter.motion.dragScale = mvfxRangeSource(0.5f, 2.0f);
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect created");
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    mvfxInstanceId instance = {0};
    s_largestAsked = 0;
    CHECK(mvfxCreateInstance(context, &def, &instance) == mvfx_errorCapacity,
          "refused with capacity");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "effect destroyed");
    return s_largestAsked == 0 ? SIZE_MAX : s_largestAsked;
}
#endif

// On a 32-bit target, an instance whose block lies within 64 bytes of
// SIZE_MAX, which the C library cannot be asked for with its alignment,
// is refused with mvfx_errorCapacity under the zeroed allocator, not
// served short. A probing allocator finds such a block: the most capacity
// whose block is below the mark, then the fewest host spawns reserved
// that take it there.
static void TestBlockNearSizeIsRefused(void)
{
#if SIZE_MAX == UINT32_MAX
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.allocator = (mvfxAllocator){.alloc = AllocateProbing, .free = Free};
    mvfxContext* probe = NULL;
    CHECK(mvfxCreateContext(&contextDef, &probe) == mvfx_success, "context created");
    const size_t mark = SIZE_MAX - 63;
    int32_t low = 1;
    int32_t high = MVFX_MAX_EMITTER_CAPACITY;
    while (high - low > 1)
    {
        int32_t middle = low + (high - low) / 2;
        if (BlockFor(probe, middle, 0) < mark)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    // At a few capacities from it down, in case a reserve's step passes
    // the mark: the fewest host spawns whose block is at least the mark
    // (reserving the capacity's worth passes it).
    int32_t capacity = low;
    int32_t hostSpawns = -1;
    while (hostSpawns < 0 && capacity > low - 8)
    {
        int32_t fewest = 0;
        int32_t most = capacity;
        while (most - fewest > 1)
        {
            int32_t middle = fewest + (most - fewest) / 2;
            if (BlockFor(probe, capacity, middle) < mark)
            {
                fewest = middle;
            }
            else
            {
                most = middle;
            }
        }
        size_t block = BlockFor(probe, capacity, most);
        if (block >= mark && block < SIZE_MAX)
        {
            hostSpawns = most;
        }
        else
        {
            capacity -= 1;
        }
    }
    mvfxDestroyContext(probe);
    CHECK(hostSpawns >= 0, "a block within 64 bytes of SIZE_MAX found");
    // The same instance under the zeroed allocator, which must refuse it.
    contextDef.allocator = (mvfxAllocator){0};
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context created");
    (void)BlockFor(context, capacity, hostSpawns);
    mvfxDestroyContext(context);
#endif
    CHECK(s_held == 0, "everything given back");
}

int main(void)
{
    TestLifecycle();
    TestRefusals();
    TestMemory();
    TestExactAlignmentIsEnough();
    TestMemoryPastSizeIsRefused();
    TestBlockNearSizeIsRefused();
    return s_failures == 0 ? 0 : 1;
}
