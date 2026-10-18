// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A simulation's record passes: a records def checked, the frame's
// particles they read, the pass added with its numbers.

#include "sim_state.h"

#include "maul-rhi/resources.h"

#include <math.h>

mvfxGpuRecordsDef mvfxDefaultGpuRecordsDef(void)
{
    return (mvfxGpuRecordsDef){.cookie = MVFX_GPU_RECORDS_DEF_COOKIE};
}

// Whether a records def is one a pass can be added from, past its layout.
static bool IsRecordsDefValid(const mvfxGpuSim* sim, const mvfxGpuRecordsDef* def)
{
    bool within = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        within = within && fabs(def->view.origin[k]) <= 0x1p52 &&
                 fabs(def->view.eye[k]) <= 0x1p52 && isfinite(def->view.forward[k]) &&
                 isfinite(def->view.up[k]);
    }
    return def->cookie == MVFX_GPU_RECORDS_DEF_COOKIE && def->emitter >= 0 &&
           def->emitter < sim->emitterCount && def->records.index1 != 0 && def->offset % 4u == 0 &&
           def->countOffset % 4u == 0 && def->capacity >= 0 && within;
}

// The frame's particles, statistics and grids of an emitter a record pass
// reads: the step's of the frame, an earlier record pass's, or imported.
static bool RecordInputs(mvfxGpuSim* sim, Emitter* emitter, mvfxRecordResources* out)
{
    if (emitter->added)
    {
        out->particles = emitter->resources[BUFFER_OTHER];
        out->statistics = emitter->resources[BUFFER_STATISTICS];
        out->grids = emitter->resources[BUFFER_GRIDS];
        return true;
    }
    if (emitter->records.count > 0)
    {
        const mvfxRecordResources* earlier = &emitter->records.passes[0].resources;
        out->particles = earlier->particles;
        out->statistics = earlier->statistics;
        out->grids = earlier->grids;
        return true;
    }
    return mrhiImportBuffer(sim->device, emitter->particles[emitter->current], &out->particles) ==
               mrhi_success &&
           mrhiImportBuffer(sim->device, emitter->statistics, &out->statistics) == mrhi_success &&
           mrhiImportBuffer(sim->device, emitter->grids, &out->grids) == mrhi_success;
}

mvfxResult mvfxGpuSim_AddRecords(mvfxGpuSim* sim, const mvfxGpuRecordsDef* def)
{
    if (sim == nullptr || def == nullptr || !IsRecordsDefValid(sim, def))
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorState;
    }
    mvfxLayoutView layout;
    mvfxResult result = mvfxLayout_GetView(sim->context, def->layout, &layout);
    if (result != mvfx_success || !mvfxIsRecordLayout(&layout))
    {
        return result != mvfx_success ? result : mvfx_errorInvalid;
    }
    Emitter* emitter = &sim->emitters[def->emitter];
    if (emitter->records.count == MVFX_GPU_RECORD_PASSES)
    {
        return mvfx_errorLimit;
    }
    mvfxEmitterPlan plan;
    result = mvfxInstance_GetEmitterPlan(sim->context, sim->instance, def->emitter, &plan);
    if (result != mvfx_success)
    {
        return result;
    }
    if (emitter->capacity == 0)
    {
        // No particles at the instance's quality level: a count of 0.
        return def->count.index1 == 0 ||
                       mvfxAddEmptyRecordPass(sim->device, &emitter->records, def->count,
                                              (uint32_t)(def->countOffset / 4u))
                   ? mvfx_success
                   : mvfx_errorState;
    }
    mvfxRecordResources resources = {.records = def->records, .count = def->count};
    bool sorts = emitter->sort.index1 != 0;
    if (!RecordInputs(sim, emitter, &resources) ||
        (def->count.index1 == 0 &&
         mrhiImportBuffer(sim->device, sim->ownCount, &resources.count) != mrhi_success) ||
        mrhiImportBuffer(sim->device, sorts ? emitter->sort : sim->ownSort, &resources.sort) !=
            mrhi_success ||
        mrhiImportBuffer(sim->device, sorts ? emitter->digits : sim->ownDigits,
                         &resources.digits) != mrhi_success)
    {
        return mvfx_errorState;
    }
    uint32_t words[MVFX_RECORD_WORDS];
    mvfxRecordNumbers(&sim->views[def->emitter], &plan, &layout, def, &emitter->tables, sim->srgb,
                      words);
    return mvfxAddRecordPass(sim->device, &emitter->records, &resources, words, def->capacity)
               ? mvfx_success
               : mvfx_errorState;
}
