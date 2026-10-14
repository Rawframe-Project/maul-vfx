// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An instance's block: the header, the emitters' states, then each
// emitter's particle arrays, aligned for whole-width vector loops.

#include "instance.h"

#include "allocator.h"

#include <string.h>

mvfxInstance* mvfxCreateInstanceBlock(mvfxEffect* effect, const double origin[3], uint32_t seed,
                                      const mvfxAllocator* allocator)
{
    mvfxLayout layout = {0};
    (void)mvfxLayoutAdd(&layout, 1, sizeof(mvfxInstance), MVFX_ATTRIBUTE_ALIGNMENT);
    size_t statesAt = mvfxLayoutAdd(&layout, (size_t)effect->emitterCount, sizeof(mvfxEmitterState),
                                    alignof(mvfxEmitterState));
    size_t particlesAt[MVFX_MAX_EMITTERS] = {0};
    int32_t largest = 0;
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        largest = effect->emitters[e].capacity > largest ? effect->emitters[e].capacity : largest;
    }
    size_t deathsAt = mvfxLayoutAdd(&layout, (size_t)largest, sizeof(int64_t), alignof(int64_t));
    size_t acceptedAt =
        mvfxLayoutAdd(&layout, (size_t)largest, sizeof(uint64_t), alignof(uint64_t));
    size_t acceptedBirthsAt =
        mvfxLayoutAdd(&layout, (size_t)largest, sizeof(int64_t), alignof(int64_t));
    size_t burstsAt = mvfxLayoutAdd(&layout, (size_t)effect->burstCount, sizeof(mvfxBurstCursor),
                                    alignof(mvfxBurstCursor));
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        particlesAt[e] = mvfxLayoutAdd(&layout, effect->emitters[e].plan.blockBytes, 1,
                                       MVFX_ATTRIBUTE_ALIGNMENT);
    }
    if (layout.overflow)
    {
        return nullptr;
    }
    unsigned char* block = mvfxAllocate(allocator, layout.size, MVFX_ATTRIBUTE_ALIGNMENT);
    if (block == nullptr)
    {
        return nullptr;
    }
    // The particle arrays are written before they are read, but a zeroed
    // block keeps the padding lanes' bits fixed for every platform's hash.
    memset(block, 0, layout.size);
    mvfxInstance* instance = (mvfxInstance*)block;
    *instance = (mvfxInstance){
        .effect = effect,
        .origin = {origin[0], origin[1], origin[2]},
        .seed = seed,
        .emitters = (mvfxEmitterState*)(block + statesAt),
        .deathScratch = (int64_t*)(block + deathsAt),
        .acceptedScratch = (uint64_t*)(block + acceptedAt),
        .acceptedBirthScratch = (int64_t*)(block + acceptedBirthsAt),
        .blockBytes = layout.size,
    };
    mvfxBurstCursor* bursts = (mvfxBurstCursor*)(block + burstsAt);
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        instance->emitters[e] = (mvfxEmitterState){
            .particles = block + particlesAt[e],
            .bursts = &bursts[effect->emitters[e].firstBurst],
        };
    }
    mvfxResetInstance(instance);
    return instance;
}

void mvfxResetInstance(mvfxInstance* instance)
{
    instance->clock = 0;
    instance->stopTick = INT64_MAX;
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        mvfxEmitterState* state = &instance->emitters[e];
        state->count = 0;
        state->owed = 0;
        state->refused = 0;
        state->rate = (mvfxRateCursor){0};
        for (int32_t b = 0; b < instance->effect->emitters[e].burstCount; ++b)
        {
            state->bursts[b] = (mvfxBurstCursor){0};
        }
    }
}

void mvfxReleaseInstance(mvfxInstance* instance, const mvfxAllocator* allocator)
{
    mvfxRelease(allocator, instance, instance->blockBytes, MVFX_ATTRIBUTE_ALIGNMENT);
}

uint64_t mvfxHashInstance(const mvfxInstance* instance, uint64_t seed)
{
    uint64_t hash = mvfxHash64(seed, &instance->clock, (int32_t)sizeof instance->clock);
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        const mvfxEmitterState* state = &instance->emitters[e];
        hash = mvfxHash64(hash, &state->count, (int32_t)sizeof state->count);
        hash = mvfxHash64(hash, &state->owed, (int32_t)sizeof state->owed);
        hash = mvfxHash64(hash, &state->refused, (int32_t)sizeof state->refused);
        uint32_t present = instance->effect->emitters[e].plan.present;
        for (mvfxAttribute a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
        {
            if ((present & (1u << a)) != 0)
            {
                hash = mvfxHash64(hash, mvfxAttributeArray(instance, e, a),
                                  state->count * (int32_t)sizeof(float));
            }
        }
    }
    return hash;
}
