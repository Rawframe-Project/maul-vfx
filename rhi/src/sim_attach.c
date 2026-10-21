// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's units held to frames on the GPU, after the births in its
// births buffer, where the unit pass finds them (sim.wgsl's unitFrames):
// the host's frames the plan gives (mvfxEmitterPlan.attachFrames, at the
// plan's end in the particles' space) staged eight words each, or a
// source's particles copied after the source's step (its live count, then
// its serials, places and, where it stores them, orientations, an array
// of the source's capacity each).

#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"

#include <string.h>

// The words before a source's copied arrays: its live count, padded.
#define SOURCE_HEAD 4u

mvfxResult mvfxSizeSimFrames(mvfxGpuSim* sim, const mvfxGpuSimDef* def, int32_t e, int32_t capacity)
{
    Emitter* emitter = &sim->emitters[e];
    const mvfxUnitDef* unit = &sim->views[e].unit;
    emitter->frameCapacity =
        capacity > 0 && unit->attach == mvfx_attachHost ? unit->attachFrameCapacity : 0;
    emitter->sourceCopy = 0;
    emitter->sourceTurned = false;
    if (capacity == 0 || unit->attach != mvfx_attachParticles)
    {
        return mvfx_success;
    }
    int32_t source = sim->views[e].eventSpawn.source;
    mvfxEmitterPlan plan;
    mvfxResult result =
        mvfxInstance_GetEmitterStepPlan(def->context, def->instance, source, 0, &plan);
    emitter->sourceCopy = plan.capacity;
    emitter->sourceTurned = sim->views[source].arrays.orientation >= 0;
    return result;
}

uint64_t mvfxSimFrameBytes(const Emitter* emitter)
{
    uint64_t arrays = 4u + (emitter->sourceTurned ? 4u : 0u);
    uint64_t copied =
        emitter->sourceCopy > 0 ? 4u * (SOURCE_HEAD + arrays * (uint64_t)emitter->sourceCopy) : 0u;
    return (uint64_t)emitter->frameCapacity * MVFX_FRAME_WORDS * 4u + copied;
}

void mvfxSimFrameNumbers(mvfxGpuSim* sim, Emitter* emitter, const mvfxEmitterPlan* plan)
{
    (void)sim;
    uint32_t* frames = emitter->words + MVFX_UNIT_FRAMES;
    if (emitter->sourceCopy > 0)
    {
        // The source's copy: its first vector, its capacity, whether it turns.
        frames[0] = (uint32_t)(emitter->framesAt / 16u);
        frames[1] = (uint32_t)emitter->sourceCopy;
        frames[2] = emitter->sourceTurned ? 1u : 0u;
        return;
    }
    if (emitter->frameCapacity == 0)
    {
        return;
    }
    int32_t count = plan->attachFrameCount < emitter->frameCapacity ? plan->attachFrameCount
                                                                    : emitter->frameCapacity;
    for (int32_t f = 0; f < count; ++f)
    {
        uint32_t* at = emitter->frameWords + MVFX_FRAME_WORDS * f;
        const float* frame = plan->attachFrames + 7 * f;
        mvfxFloatWords(at, frame[0], frame[1], frame[2], 0.0f);
        memcpy(at + 4, frame + 3, 4 * sizeof(float));
    }
    emitter->frameCount = count;
    // The host's frames: their first vector, how many.
    frames[0] = (uint32_t)(emitter->framesAt / 16u);
    frames[1] = (uint32_t)count;
}

bool mvfxUploadSimFrames(mvfxGpuSim* sim, Emitter* emitter, mrhiPassId pass)
{
    return emitter->frameCount == 0 ||
           mvfxUpload(sim->device, &sim->uploaded, pass, emitter->resources[BUFFER_BIRTHS],
                      emitter->framesAt, emitter->frameWords,
                      (uint64_t)emitter->frameCount * MVFX_FRAME_WORDS * 4u);
}

// Copies count arrays from array a on of the source's particles after its
// step (its other buffer) to word at of the emitter's births.
static bool CopyArrays(mvfxGpuSim* sim, mrhiPassId pass, const Emitter* emitter,
                       const Emitter* source, int16_t a, uint32_t count, uint64_t at)
{
    uint64_t c = (uint64_t)source->capacity;
    return mrhiCopyBuffer(sim->device, pass, source->resources[BUFFER_OTHER],
                          4u * mvfxWordOf(a) * c, emitter->resources[BUFFER_BIRTHS], 4u * at,
                          4u * count * c) == mrhi_success;
}

bool mvfxRecordSourceCopy(mvfxGpuSim* sim, const Emitter* emitter, mrhiPassId pass)
{
    static const uint32_t none = 0;
    if (emitter->sourceCopy == 0)
    {
        return true;
    }
    const Emitter* source = &sim->emitters[emitter->spawnSource];
    const mvfxAttributeArrays* arrays = &sim->views[emitter->spawnSource].arrays;
    uint64_t at = emitter->framesAt / 4u;
    uint64_t c = (uint64_t)emitter->sourceCopy;
    if (source->capacity == 0)
    {
        return mvfxUpload(sim->device, &sim->uploaded, pass, emitter->resources[BUFFER_BIRTHS],
                          4u * at, &none, sizeof none);
    }
    return mrhiCopyBuffer(sim->device, pass, source->resources[BUFFER_STATISTICS], 0,
                          emitter->resources[BUFFER_BIRTHS], 4u * at, 4u) == mrhi_success &&
           CopyArrays(sim, pass, emitter, source, arrays->serial, 1u, at + SOURCE_HEAD) &&
           CopyArrays(sim, pass, emitter, source, arrays->position, 3u, at + SOURCE_HEAD + c) &&
           (!emitter->sourceTurned || CopyArrays(sim, pass, emitter, source, arrays->orientation,
                                                 4u, at + SOURCE_HEAD + 4u * c));
}
