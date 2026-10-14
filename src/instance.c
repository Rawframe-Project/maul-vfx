// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An instance's block: the header, the emitters' states, then each
// emitter's particle arrays, aligned for whole-width vector loops.

#include "instance.h"

#include "allocator.h"
#include "force.h"
#include "kill.h"

#include <math.h>
#include <string.h>

mvfxInstance* mvfxCreateInstanceBlock(mvfxEffect* effect, const mvfxTransform* transform,
                                      uint32_t seed, const mvfxAllocator* allocator)
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
        mvfxLayoutAdd(&layout, (size_t)largest, sizeof(mvfxBirth), alignof(mvfxBirth));
    size_t killsAt = mvfxLayoutAdd(&layout, (size_t)largest, sizeof(uint8_t), alignof(uint8_t));
    size_t burstsAt = mvfxLayoutAdd(&layout, (size_t)effect->burstCount, sizeof(mvfxBurstCursor),
                                    alignof(mvfxBurstCursor));
    size_t queuesAt[MVFX_MAX_EMITTERS] = {0};
    size_t forcesAt[MVFX_MAX_EMITTERS] = {0};
    size_t rulesAt[MVFX_MAX_EMITTERS] = {0};
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        queuesAt[e] = mvfxLayoutAdd(&layout, (size_t)effect->emitters[e].hostSpawnCapacity,
                                    sizeof(mvfxHostBirth), alignof(mvfxHostBirth));
        forcesAt[e] = mvfxLayoutAdd(&layout, (size_t)effect->emitters[e].forceCount,
                                    sizeof(mvfxResolvedForce), alignof(mvfxResolvedForce));
        rulesAt[e] = mvfxLayoutAdd(&layout, (size_t)effect->emitters[e].killRuleCount,
                                   sizeof(mvfxResolvedKillRule), alignof(mvfxResolvedKillRule));
    }
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
        .from = *transform,
        .to = *transform,
        .seed = seed,
        .emitters = (mvfxEmitterState*)(block + statesAt),
        .deathScratch = (int64_t*)(block + deathsAt),
        .acceptedScratch = (mvfxBirth*)(block + acceptedAt),
        .killScratch = block + killsAt,
        .blockBytes = layout.size,
    };
    for (int32_t p = 0; p < effect->parameterCount; ++p)
    {
        for (int32_t c = 0; c < 4; ++c)
        {
            instance->parameters[p][c] = effect->parameters[p][c];
        }
    }
    mvfxBurstCursor* bursts = (mvfxBurstCursor*)(block + burstsAt);
    for (int32_t e = 0; e < effect->emitterCount; ++e)
    {
        instance->emitters[e] = (mvfxEmitterState){
            .particles = block + particlesAt[e],
            .bursts = &bursts[effect->emitters[e].firstBurst],
            .mesh = effect->emitters[e].shape.mesh,
            .hostQueue = (mvfxHostBirth*)(block + queuesAt[e]),
            .forces = (mvfxResolvedForce*)(block + forcesAt[e]),
            .killRules = (mvfxResolvedKillRule*)(block + rulesAt[e]),
        };
    }
    mvfxResetInstance(instance);
    return instance;
}

void mvfxResetInstance(mvfxInstance* instance)
{
    instance->clock = 0;
    instance->stepStart = 0;
    instance->stopTick = INT64_MAX;
    instance->from = instance->to;
    instance->stepDistance = 0.0;
    instance->velocity[0] = 0.0f;
    instance->velocity[1] = 0.0f;
    instance->velocity[2] = 0.0f;
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        mvfxEmitterState* state = &instance->emitters[e];
        state->count = 0;
        state->owed = 0;
        state->refused = 0;
        state->rate = (mvfxRateCursor){0};
        state->hostCount = 0;
        state->killed = 0;
        state->distanceCarry = 0.0;
        state->distanceDelta = 0.0;
        state->distanceSkip = 0.0;
        state->distanceTake = 0;
        state->distanceNext = 0;
        for (int32_t k = 0; k < 3; ++k)
        {
            state->anchor[k] = instance->to.origin[k];
        }
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
        uint64_t present = instance->effect->emitters[e].plan.present;
        for (int32_t a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
        {
            if ((present & ((uint64_t)1 << a)) != 0)
            {
                hash = mvfxHash64(hash, mvfxAttributeArray(instance, e, a),
                                  state->count * (int32_t)sizeof(float));
            }
        }
    }
    return hash;
}

void mvfxTransformAt(const mvfxInstance* instance, int64_t tick, mvfxTransform* out)
{
    int64_t span = instance->clock - instance->stepStart;
    if (span <= 0 || tick >= instance->clock)
    {
        *out = instance->to;
        return;
    }
    double share = (double)(tick - instance->stepStart) / (double)span;
    const mvfxTransform* a = &instance->from;
    const mvfxTransform* b = &instance->to;
    for (int32_t k = 0; k < 3; ++k)
    {
        out->origin[k] = a->origin[k] + (b->origin[k] - a->origin[k]) * share;
    }
    // Normalized linear interpolation, along the shorter way.
    float t = (float)share;
    float dot = a->rotation[0] * b->rotation[0] + a->rotation[1] * b->rotation[1] +
                a->rotation[2] * b->rotation[2] + a->rotation[3] * b->rotation[3];
    float sign = dot < 0.0f ? -1.0f : 1.0f;
    double square = 0.0;
    for (int32_t k = 0; k < 4; ++k)
    {
        out->rotation[k] = a->rotation[k] + (sign * b->rotation[k] - a->rotation[k]) * t;
        square += (double)out->rotation[k] * (double)out->rotation[k];
    }
    float scale = square > 0.0 ? (float)(1.0 / sqrt(square)) : 1.0f;
    for (int32_t k = 0; k < 4; ++k)
    {
        out->rotation[k] *= scale;
    }
    out->scale = a->scale + (b->scale - a->scale) * t;
}
