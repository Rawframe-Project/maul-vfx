// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "host_module.h"

#include "random.h"

#include "maul-vfx/instance.h"

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
