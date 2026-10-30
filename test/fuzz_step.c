// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the step with the effects fuzzed files make: a file that reads
// into a def the validator accepts is created as an effect on a context
// holding the golden scene's grids, meshes and host module (the scene's
// own instances destroyed, so the step is the fuzzed effect's alone), an
// instance of it is created and stepped through short, long and empty
// steps, and both are destroyed. Every call must succeed or refuse as
// documented, and nothing may read, write or overflow out of bounds. After
// each step every value its emitters' records, sprites, ribbons, lights,
// events and bounds show must be finite, for an effect without a solver of
// its own (rigid bodies, units, contacts, a liquid, a gas or a water
// surface), whose products are bounded apart. Defs
// too costly to step within a fuzzing run (many particles, large gases or
// water surfaces) are passed over. Inputs are sealed (size and hash set),
// as the file's own target does for most. No call may return with the
// context still in a call of host code.

#include "context.h"
#include "slots.h"
#include "vfx_scene.h"

#include "maul-vfx/file.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

// The most a def may cost to be stepped here.
#define MOST_PARTICLES   (1 << 12)
#define MOST_GAS_CELLS   (16 * 16 * 16)
#define MOST_WATER_CELLS (64 * 64)

static mvfxContext* s_fuzzContext;

// A record of every field a particle shows, as binary32.
#define RECORD_FLOATS 23
static mvfxLayoutId s_layout;
static float s_records[4 * MOST_PARTICLES * 4][RECORD_FLOATS];
static uint32_t s_indices[6 * MOST_PARTICLES * 4];
static uint32_t s_scratch[4 * MOST_PARTICLES];
static mvfxLight s_lights[MOST_PARTICLES];

// Aborts on a condition that does not hold, or on a context left in a
// call of host code after one of its calls returned.
static void Expect(bool condition)
{
    if (!condition || (s_fuzzContext != NULL && s_fuzzContext->hostCalls != 0))
    {
        abort();
    }
}

static void PutLittle(uint8_t* at, uint64_t value, int32_t bytes)
{
    for (int32_t k = 0; k < bytes; ++k)
    {
        at[k] = (uint8_t)(value >> (8 * k));
    }
}

// The scene's grids, meshes and module on a context with no instances.
static void MakeContext(void)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    Expect(mvfxCreateContext(&def, &s_fuzzContext) == mvfx_success);
    BuildScene(s_fuzzContext);
    Expect(s_failures == 0);
    static const struct
    {
        mvfxField field;
        uint8_t components;
    } fields[] = {{mvfx_recordPosition, 3},    {mvfx_recordSize, 3},     {mvfx_recordColor, 4},
                  {mvfx_recordAngle, 1},       {mvfx_recordVelocity, 3}, {mvfx_recordAge, 1},
                  {mvfx_recordOrientation, 4}, {mvfx_recordCustom, 4}};
    mvfxLayoutField layout[8];
    uint32_t offset = 0;
    for (int32_t f = 0; f < 8; ++f)
    {
        layout[f] = (mvfxLayoutField){.field = fields[f].field,
                                      .format = mvfx_formatFloat32,
                                      .components = fields[f].components,
                                      .offset = offset};
        offset += 4u * fields[f].components;
    }
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = layout;
    layoutDef.fieldCount = 8;
    layoutDef.stride = offset;
    Expect(offset == 4 * RECORD_FLOATS &&
           mvfxCreateLayout(s_fuzzContext, &layoutDef, &s_layout) == mvfx_success);
    const mvfxSlots* slots = &s_fuzzContext->instances;
    for (uint32_t i = 0; i < slots->capacity; ++i)
    {
        if (slots->slots[i].object != NULL)
        {
            mvfxInstanceId id = {i + 1, slots->slots[i].generation};
            Expect(mvfxDestroyInstance(s_fuzzContext, id) == mvfx_success);
        }
    }
}

// Whether a def is cheap enough to step here.
static bool IsCheap(const mvfxEffectDef* def)
{
    int64_t particles = 0;
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        particles += def->emitters[i].capacity;
    }
    for (int32_t i = 0; i < def->gasCount; ++i)
    {
        const int32_t* n = def->gases[i].size;
        if ((int64_t)n[0] * n[1] * n[2] > MOST_GAS_CELLS)
        {
            return false;
        }
    }
    for (int32_t i = 0; i < def->waterCount; ++i)
    {
        const int32_t* n = def->waters[i].size;
        if ((int64_t)n[0] * n[1] > MOST_WATER_CELLS)
        {
            return false;
        }
    }
    return particles <= MOST_PARTICLES;
}

// Whether the first count records are finite in every value.
static bool AreFinite(int64_t count)
{
    bool finite = true;
    for (int64_t i = 0; i < count; ++i)
    {
        for (int32_t k = 0; k < RECORD_FLOATS; ++k)
        {
            finite = finite && isfinite(s_records[i][k]);
        }
    }
    return finite;
}

// Expects every value an instance's emitters show to be finite: their
// records, sprites and ribbons expanded, lights, events and bounds.
static void ExpectFinite(mvfxInstanceId instance, int32_t emitters)
{
    static const mvfxView view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};
    const int64_t room = (int64_t)(sizeof s_records / sizeof s_records[0]);
    const mvfxExpandBuffers buffers = {.vertices = s_records,
                                       .vertexCapacity = room,
                                       .indices = s_indices,
                                       .indexCapacity = (int64_t)(sizeof s_indices / 4),
                                       .scratch = s_scratch,
                                       .scratchBytes = (int64_t)sizeof s_scratch};
    for (int32_t e = 0; e < emitters; ++e)
    {
        int64_t count = 0;
        int64_t indices = 0;
        Expect(mvfxInstance_WriteParticles(s_fuzzContext, instance, e, s_layout, &view, s_scratch,
                                           (int64_t)sizeof s_scratch, s_records, room,
                                           &count) == mvfx_success);
        Expect(AreFinite(count < room ? count : room));
        Expect(mvfxInstance_ExpandSprites(s_fuzzContext, instance, e, s_layout, &view, &buffers,
                                          &count) == mvfx_success);
        Expect(AreFinite(count < room ? count : room));
        if (mvfxInstance_ExpandRibbons(s_fuzzContext, instance, e, s_layout, &view, &buffers,
                                       &count, &indices) == mvfx_success)
        {
            Expect(AreFinite(count < room ? count : room));
        }
        Expect(mvfxInstance_WriteLights(s_fuzzContext, instance, e, &view, s_lights, MOST_PARTICLES,
                                        &count) == mvfx_success);
        for (int64_t i = 0; i < count && i < MOST_PARTICLES; ++i)
        {
            const mvfxLight* l = &s_lights[i];
            Expect(isfinite(l->radius) && isfinite(l->position[0]) && isfinite(l->position[1]) &&
                   isfinite(l->position[2]) && isfinite(l->color[0]) && isfinite(l->color[1]) &&
                   isfinite(l->color[2]));
        }
        const mvfxEvent* events = NULL;
        int32_t eventCount = 0;
        Expect(mvfxInstance_GetEvents(s_fuzzContext, instance, e, &events, &eventCount) ==
               mvfx_success);
        for (int32_t i = 0; i < eventCount; ++i)
        {
            const mvfxEvent* v = &events[i];
            for (int32_t k = 0; k < 4; ++k)
            {
                Expect(isfinite(v->color[k]) &&
                       (k == 3 || (isfinite(v->place[k]) && isfinite(v->velocity[k]) &&
                                   isfinite(v->normal[k]) && isfinite(v->size[k]))));
            }
        }
        mvfxBounds bounds;
        Expect(mvfxInstance_GetBounds(s_fuzzContext, instance, e, &bounds) == mvfx_success);
        for (int32_t k = 0; k < 3; ++k)
        {
            // An empty box is the empty one, its ends past each other.
            Expect(!(bounds.min[k] <= bounds.max[k]) ||
                   (isfinite(bounds.min[k]) && isfinite(bounds.max[k])));
        }
    }
}

// Whether an effect has a solver whose own products are not yet bounded:
// its outputs are not held to be finite here.
static bool HasSolver(const mvfxEffectDef* def)
{
    bool solver = def->gasCount > 0 || def->waterCount > 0;
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        const mvfxEmitterDef* e = &def->emitters[i];
        solver = solver || e->rigid.enable || e->unit.kind != mvfx_unitParticle ||
                 e->contacts.enable || e->liquid.enable;
    }
    return solver;
}

// Plays an effect: an instance stepped through steps of several lengths.
static void Play(mvfxEffectId effect, const mvfxEffectDef* effectDef, uint32_t seed)
{
    bool finite = !HasSolver(effectDef);
    mvfxInstanceDef def = mvfxDefaultInstanceDef();
    def.effect = effect;
    def.seed = seed;
    mvfxInstanceId instance = {0};
    mvfxResult created = mvfxCreateInstance(s_fuzzContext, &def, &instance);
    Expect(created == mvfx_success || created == mvfx_errorCapacity);
    if (created != mvfx_success)
    {
        return;
    }
    static const int64_t steps[] = {MVFX_TICKS_PER_SECOND / 60, 0, 1, MVFX_TICKS_PER_SECOND / 30};
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; ++i)
    {
        Expect(mvfxContext_StepTicks(s_fuzzContext, steps[i]) == mvfx_success);
        if (finite)
        {
            ExpectFinite(instance, effectDef->emitterCount);
        }
    }
    Expect(mvfxDestroyInstance(s_fuzzContext, instance) == mvfx_success);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 24 || size > ((size_t)1 << 22))
    {
        return 0;
    }
    if (s_fuzzContext == NULL)
    {
        MakeContext();
    }
    uint8_t* bytes = malloc(size);
    Expect(bytes != NULL);
    memcpy(bytes, data, size);
    // The seed comes from the hash's bytes, which sealing writes over.
    uint32_t seed = (uint32_t)bytes[16] | (uint32_t)bytes[17] << 8;
    PutLittle(bytes + 8, size, 4);
    PutLittle(bytes + 16, mvfxHash64(MVFX_HASH_INIT, bytes + 24, (int32_t)(size - 24)), 8);
    int64_t needed = 0;
    mvfxEffectDef def;
    mvfxFileResult read = mvfxReadEffect(bytes, (int64_t)size, s_sceneAssets, s_sceneAssetCount,
                                         NULL, 0, &needed, &def);
    if (read.result == mvfx_errorCapacity)
    {
        void* block = malloc((size_t)needed + 16);
        Expect(block != NULL);
        void* memory = (void*)(((uintptr_t)block + 15) & ~(uintptr_t)15);
        read = mvfxReadEffect(bytes, (int64_t)size, s_sceneAssets, s_sceneAssetCount, memory,
                              needed, &needed, &def);
        mvfxEffectId effect = {0};
        mvfxResult created = read.result == mvfx_success && IsCheap(&def)
                                 ? mvfxCreateEffect(s_fuzzContext, &def, &effect)
                                 : mvfx_errorCapacity;
        Expect(created <= 0);
        if (created == mvfx_success)
        {
            Play(effect, &def, seed);
            Expect(mvfxDestroyEffect(s_fuzzContext, effect) == mvfx_success);
        }
        free(block);
    }
    free(bytes);
    return 0;
}
