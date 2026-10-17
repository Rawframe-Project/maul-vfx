// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the step with the effects fuzzed files make: a file that reads
// into a def the validator accepts is created as an effect on a context
// holding the golden scene's grids, meshes and host module (the scene's
// own instances destroyed, so the step is the fuzzed effect's alone), an
// instance of it is created and stepped through short, long and empty
// steps, and both are destroyed. Every call must succeed or refuse as
// documented, and nothing may read, write or overflow out of bounds. Defs
// too costly to step within a fuzzing run (many particles, large gases or
// water surfaces) are passed over. Inputs are sealed (size and hash set),
// as the file's own target does for most.

#include "context.h"
#include "slots.h"
#include "vfx_scene.h"

#include "maul-vfx/file.h"

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

static void Expect(bool condition)
{
    if (!condition)
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

// Plays an effect: an instance stepped through steps of several lengths.
static void Play(mvfxEffectId effect, uint32_t seed)
{
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
                                 : mvfx_errorLimit;
        Expect(created <= 0);
        if (created == mvfx_success)
        {
            Play(effect, seed);
            Expect(mvfxDestroyEffect(s_fuzzContext, effect) == mvfx_success);
        }
        free(block);
    }
    free(bytes);
    return 0;
}
