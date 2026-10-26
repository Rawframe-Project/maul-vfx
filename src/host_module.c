// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "host_module.h"

#include "random.h"

#include "maul-vfx/instance.h"

#include <float.h>
#include <math.h>

// The salt of a host module's seed.
#define HOST_SALT 0x484F5354u

// The array of an attribute, or NULL when the emitter does not store it.
static void* ArrayOf(mvfxInstance* instance, int32_t emitter, int32_t attribute)
{
    if (!mvfxHasAttribute(&instance->effect->emitters[emitter].plan, attribute))
    {
        return nullptr;
    }
    return mvfxAttributeArray(instance, emitter, (mvfxAttribute)attribute);
}

// Fills n pointers from consecutive attributes.
static void Fill(mvfxInstance* instance, int32_t emitter, int32_t first, int32_t n, float* out[])
{
    for (int32_t k = 0; k < n; ++k)
    {
        out[k] = ArrayOf(instance, emitter, first + k);
    }
}

static mvfxParticleArrays ArraysOf(mvfxInstance* instance, int32_t emitter, bool killable)
{
    mvfxParticleArrays arrays = {
        .count = instance->emitters[emitter].count,
        .angle = ArrayOf(instance, emitter, mvfx_attributeAngle),
        .spin = ArrayOf(instance, emitter, mvfx_attributeSpin),
        .frame = ArrayOf(instance, emitter, mvfx_attributeFrame),
        .serial = ArrayOf(instance, emitter, mvfx_attributeSerial),
        .birthLow = ArrayOf(instance, emitter, mvfx_attributeBirthLow),
        .birthHigh = ArrayOf(instance, emitter, mvfx_attributeBirthHigh),
        .inverseLife = ArrayOf(instance, emitter, mvfx_attributeInverseLife),
        .collisions = ArrayOf(instance, emitter, mvfx_attributeCollisions),
        .kill = killable ? instance->killScratch : nullptr,
    };
    Fill(instance, emitter, mvfx_attributePositionX, 3, arrays.position);
    Fill(instance, emitter, mvfx_attributeVelocityX, 3, arrays.velocity);
    Fill(instance, emitter, mvfx_attributeColorR, 4, arrays.color);
    Fill(instance, emitter, mvfx_attributeSizeX, 3, arrays.size);
    Fill(instance, emitter, mvfx_attributeOrientationX, 4, arrays.orientation);
    Fill(instance, emitter, mvfx_attributeAngularX, 3, arrays.angularVelocity);
    arrays.neighborCount = ArrayOf(instance, emitter, mvfx_attributeNeighborCount);
    for (int32_t k = 0; k < 3; ++k)
    {
        arrays.neighborCenter[k] =
            ArrayOf(instance, emitter, (mvfxAttribute)(mvfx_attributeNeighborCenterX + k));
        arrays.neighborVelocity[k] =
            ArrayOf(instance, emitter, (mvfxAttribute)(mvfx_attributeNeighborVelocityX + k));
    }
    for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
    {
        Fill(instance, emitter, mvfx_attributeCustom + 4 * i, 4, arrays.custom[i]);
    }
    return arrays;
}

// Whether particle i's values in n arrays (NULL for one not stored) are
// each finite and at most bound in size, bound per array.
static bool AreWithin(float* const* arrays, int32_t n, int32_t i, const float* bounds)
{
    bool within = true;
    for (int32_t k = 0; k < n; ++k)
    {
        within = within && (arrays[k] == nullptr || fabsf(arrays[k][i]) <= bounds[k]);
    }
    return within;
}

// Whether any of count values is not finite or past bound in size: a
// reduction over one array, which vectorizes.
static bool AnyPast(const float* values, int32_t count, float bound)
{
    uint32_t past = 0;
    for (int32_t i = 0; values != nullptr && i < count; ++i)
    {
        past |= fabsf(values[i]) <= bound ? 0u : 1u;
    }
    return past != 0;
}

// Whether any of n arrays holds a value past its bound.
static bool AnyArrayPast(float* const* arrays, int32_t n, int32_t count, const float* bounds)
{
    bool past = false;
    for (int32_t k = 0; k < n; ++k)
    {
        past = past || AnyPast(arrays[k], count, bounds[k]);
    }
    return past;
}

// Marks for the kill each particle a host module left holding a value
// that is not finite or is past its limit: a place past MVFX_MAX_PLACE, a
// velocity past the speed the step holds, a size, colour, spin, angular
// velocity or custom attribute past what the emitter's particles may
// store, an angle or orientation not finite. Those not marked already are
// counted in valuesMarked.
static void MarkPastLimits(mvfxInstance* instance, int32_t emitter, const mvfxParticleArrays* a)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    const mvfxStoredCaps* caps = &instance->effect->emitters[emitter].overLife.caps;
    float fastest = MVFX_MAX_SPEED * state->velocityModules.sigma;
    const float finite[4] = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    const float places[3] = {MVFX_MAX_PLACE, MVFX_MAX_PLACE, MVFX_MAX_PLACE};
    const float speeds[3] = {fastest, fastest, fastest};
    const float spins[3] = {caps->spin, caps->spin, caps->spin};
    float* angle[1] = {a->angle};
    float* spin[1] = {a->spin};
    // An array at a time first: the particles are looked at one by one
    // only where some value is past its limit.
    int32_t n = a->count;
    bool past =
        AnyArrayPast(a->position, 3, n, places) || AnyArrayPast(a->velocity, 3, n, speeds) ||
        AnyArrayPast(a->color, 4, n, caps->color) || AnyArrayPast(a->size, 3, n, caps->size) ||
        AnyArrayPast(angle, 1, n, finite) || AnyArrayPast(spin, 1, n, &caps->spin) ||
        AnyArrayPast(a->orientation, 4, n, finite) || AnyArrayPast(a->angularVelocity, 3, n, spins);
    for (int32_t c = 0; !past && c < MVFX_MAX_CUSTOM_ATTRIBUTES; ++c)
    {
        const float custom[4] = {caps->custom[c], caps->custom[c], caps->custom[c],
                                 caps->custom[c]};
        past = AnyArrayPast(a->custom[c], 4, n, custom);
    }
    for (int32_t i = 0; past && i < n; ++i)
    {
        bool within =
            AreWithin(a->position, 3, i, places) && AreWithin(a->velocity, 3, i, speeds) &&
            AreWithin(a->color, 4, i, caps->color) && AreWithin(a->size, 3, i, caps->size) &&
            AreWithin(angle, 1, i, finite) && AreWithin(spin, 1, i, &caps->spin) &&
            AreWithin(a->orientation, 4, i, finite) && AreWithin(a->angularVelocity, 3, i, spins);
        for (int32_t c = 0; within && c < MVFX_MAX_CUSTOM_ATTRIBUTES; ++c)
        {
            const float custom[4] = {caps->custom[c], caps->custom[c], caps->custom[c],
                                     caps->custom[c]};
            within = AreWithin(a->custom[c], 4, i, custom);
        }
        if (!within && instance->killScratch[i] == 0)
        {
            instance->killScratch[i] = 1;
            state->valuesMarked += 1;
        }
    }
}

void mvfxRunHostModules(mvfxInstance* instance, int32_t emitter, mvfxModuleStage stage)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    for (int32_t m = 0; m < compiled->hostModuleCount; ++m)
    {
        const mvfxHostModule* module = &compiled->hostModules[m];
        if (module->stage != stage)
        {
            continue;
        }
        mvfxWords4 seed =
            mvfxPcg4d((mvfxWords4){instance->seed, (uint32_t)emitter, (uint32_t)m, HOST_SALT});
        const double* anchor = instance->emitters[emitter].anchor;
        mvfxModuleCall call = {
            .particles = ArraysOf(instance, emitter, stage == mvfx_stageAfterMotion),
            .stage = stage,
            .emitter = emitter,
            .module = m,
            .seconds =
                (double)(instance->clock - instance->stepStart) / (double)MVFX_TICKS_PER_SECOND,
            .startTick = instance->stepStart,
            .endTick = instance->clock,
            .anchor = {anchor[0], anchor[1], anchor[2]},
            .worldSpace = compiled->space == mvfx_spaceWorld,
            .seed = seed.x,
        };
        module->function(&call, module->user);
        MarkPastLimits(instance, emitter, &call.particles);
    }
}

void mvfxModule_Random(const mvfxModuleCall* call, uint32_t serial, uint32_t stream,
                       uint32_t out[4])
{
    mvfxWords4 words =
        mvfxPcg4d((mvfxWords4){call->seed, serial, stream, (uint32_t)(uint64_t)call->endTick});
    out[0] = words.x;
    out[1] = words.y;
    out[2] = words.z;
    out[3] = words.w;
}
