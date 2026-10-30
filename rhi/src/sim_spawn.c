// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's births from another's events on the GPU: the emitters'
// step order, the source's events copied into its spawn region, the
// numbers and kernels of its births from them (sim.wgsl's spawn kernels).

#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

// The order the emitters step in, as the core's: repeatedly the lowest
// index whose source of events (if any) has stepped (an effect's sources
// make no cycle).
void mvfxOrderSimEmitters(mvfxGpuSim* sim)
{
    bool placed[MVFX_MAX_EMITTERS] = {false};
    for (int32_t n = 0; n < sim->emitterCount; ++n)
    {
        int32_t next = -1;
        for (int32_t e = 0; next < 0 && e < sim->emitterCount; ++e)
        {
            const mvfxEventSpawn* spawn = &sim->views[e].eventSpawn;
            next = !placed[e] && (!spawn->enabled || placed[spawn->source]) ? e : next;
        }
        placed[next] = true;
        sim->order[n] = next;
    }
}

// Adds the pass copying the source's events, as many as it wrote, into an
// emitter's spawn region (none when the source is left out), and for units
// held to the source's particles those particles into its births buffer.
bool mvfxAddSpawnCopyPass(mvfxGpuSim* sim, Emitter* emitter)
{
    const Emitter* source = &sim->emitters[emitter->spawnSource];
    mrhiAccess copied[5] = {
        {.resource = emitter->resources[BUFFER_EVENTS], .kind = mrhi_accessCopyDestination}};
    int32_t count = 1;
    if (emitter->sourceCopy > 0)
    {
        copied[count++] = (mrhiAccess){.resource = emitter->resources[BUFFER_BIRTHS],
                                       .kind = mrhi_accessCopyDestination};
    }
    if (source->capacity > 0)
    {
        copied[count++] = (mrhiAccess){.resource = source->resources[BUFFER_EVENTS],
                                       .kind = mrhi_accessCopySource};
        copied[count++] = (mrhiAccess){.resource = source->resources[BUFFER_STATISTICS],
                                       .kind = mrhi_accessCopySource};
    }
    if (source->capacity > 0 && emitter->sourceCopy > 0)
    {
        copied[count++] = (mrhiAccess){.resource = source->resources[BUFFER_OTHER],
                                       .kind = mrhi_accessCopySource};
    }
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = copied;
    def.accessCount = count;
    def.neverCull = true;
    return mrhiAddPass(sim->device, &def, &emitter->passes[PASS_SPAWN_COPY]) == mrhi_success;
}

static_assert(MVFX_SPAWN_FROM == MVFX_SPAWN_SOURCE + 1, "a left-out source's words one upload");

// Records the copy of the source's events, as many as it wrote and the
// first of its step's, into an emitter's spawn region; none when the
// source is left out, its count 0.
static bool RecordSpawnCopy(mvfxGpuSim* sim, const Emitter* emitter)
{
    static const uint32_t none[2] = {0};
    const Emitter* source = &sim->emitters[emitter->spawnSource];
    mrhiPassId pass = emitter->passes[PASS_SPAWN_COPY];
    mrhiResourceId into = emitter->resources[BUFFER_EVENTS];
    uint64_t first =
        4u * mvfxSpawnFirst(emitter->capacity, sim->views[emitter - sim->emitters].eventCapacity);
    int32_t capacity = sim->views[emitter->spawnSource].eventCapacity;
    bool recorded = mrhiPass_Begin(sim->device, pass) == mrhi_success;
    if (source->capacity > 0)
    {
        recorded = recorded &&
                   mrhiPass_CopyBuffer(sim->device, pass, source->resources[BUFFER_STATISTICS],
                                       4u * MVFX_GPU_EVENTS_WRITTEN, into,
                                       first + 4u * MVFX_SPAWN_SOURCE, 4u) == mrhi_success &&
                   mrhiPass_CopyBuffer(sim->device, pass, source->resources[BUFFER_STATISTICS],
                                       4u * MVFX_RECORD_EVENT_FROM, into,
                                       first + 4u * MVFX_SPAWN_FROM, 4u) == mrhi_success &&
                   mrhiPass_CopyBuffer(sim->device, pass, source->resources[BUFFER_EVENTS],
                                       4u * (uint64_t)mvfxEventFirst(source->capacity), into,
                                       first + 4u * MVFX_SPAWN_EVENTS,
                                       4u * MVFX_EVENT_WORDS * (uint64_t)capacity) == mrhi_success;
    }
    else
    {
        recorded = recorded && mvfxUpload(sim->device, &sim->uploaded, pass, into,
                                          first + 4u * MVFX_SPAWN_SOURCE, none, sizeof none);
    }
    recorded = recorded && mvfxRecordSourceCopy(sim, emitter, pass);
    return mrhiPass_End(sim->device, pass) == mrhi_success && recorded;
}

// Records an emitter's births from events, when it spawns from them.
bool mvfxRecordSpawn(mvfxGpuSim* sim, const Emitter* emitter, uint32_t groups)
{
    return emitter->spawnSource < 0 ||
           (RecordSpawnCopy(sim, emitter) &&
            mvfxRecordSimKernel(sim, emitter, PASS_SPAWN_COUNT, mvfx_kernelSpawnCount, 1) &&
            mvfxRecordSimKernel(sim, emitter, PASS_SPAWN_SORT, mvfx_kernelSpawnSort, 1) &&
            mvfxRecordSimKernel(sim, emitter, PASS_SPAWN_SCAN, mvfx_kernelSpawnScan, 1) &&
            mvfxRecordSimKernel(sim, emitter, PASS_SPAWN_MERGE, mvfx_kernelSpawnMerge, groups));
}

mvfxResult mvfxSpawnStepNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan)
{
    Emitter* emitter = &sim->emitters[e];
    if (emitter->spawnSource < 0)
    {
        return mvfx_success;
    }
    mvfxEmitterPlan sourcePlan;
    mvfxResult result = mvfxSimEmitterPlan(sim, emitter->spawnSource, &sourcePlan);
    if (result == mvfx_success)
    {
        mvfxSpawnNumbers(&sim->views[e], plan, &sim->views[emitter->spawnSource], &sourcePlan,
                         emitter->hostRecords, emitter->words);
    }
    return result;
}
