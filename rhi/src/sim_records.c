// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A simulation's record and ribbon passes: a def checked, the frame's
// particles they read, the pass added with its numbers.

#include "sim_state.h"

#include "maul-rhi/resources.h"

#include <math.h>

mvfxGpuRecordsDef mvfxDefaultGpuRecordsDef(void)
{
    return (mvfxGpuRecordsDef){.cookie = MVFX_GPU_RECORDS_DEF_COOKIE};
}

mvfxGpuRibbonsDef mvfxDefaultGpuRibbonsDef(void)
{
    return (mvfxGpuRibbonsDef){.cookie = MVFX_GPU_RIBBONS_DEF_COOKIE};
}

// Whether a view's origin and eye are within range and its directions
// finite.
static bool IsViewWithin(const mvfxView* view)
{
    bool within = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        within = within && fabs(view->origin[k]) <= 0x1p52 && fabs(view->eye[k]) <= 0x1p52 &&
                 isfinite(view->forward[k]) && isfinite(view->up[k]);
    }
    return within;
}

// Whether a records def is one a pass can be added from, past its layout.
static bool IsRecordsDefValid(const mvfxGpuSim* sim, const mvfxGpuRecordsDef* def)
{
    return def->cookie == MVFX_GPU_RECORDS_DEF_COOKIE && def->emitter >= 0 &&
           def->emitter < sim->emitterCount && def->records.index1 != 0 && def->offset % 4u == 0 &&
           def->countOffset % 4u == 0 && def->capacity >= 0 && IsViewWithin(&def->view);
}

// Whether a ribbons def is one a pass can be added from, past its layout:
// its emitter's ribbons without a pinned head.
static bool IsRibbonsDefValid(const mvfxGpuSim* sim, const mvfxGpuRibbonsDef* def)
{
    if (def->cookie != MVFX_GPU_RIBBONS_DEF_COOKIE || def->emitter < 0 ||
        def->emitter >= sim->emitterCount)
    {
        return false;
    }
    const mvfxRibbonView* ribbon = &sim->views[def->emitter].ribbon;
    return def->points.index1 != 0 && def->ribbons.index1 != 0 && def->pointOffset % 4u == 0 &&
           def->ribbonOffset % 4u == 0 && def->countsOffset % 4u == 0 && def->pointCapacity >= 0 &&
           def->ribbonCapacity >= 0 && IsViewWithin(&def->view) && !ribbon->pinHead;
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
    for (int32_t p = 0; p < emitter->records.count; ++p)
    {
        // An empty pass (counts of 0 for an emitter without ribbons)
        // reads none.
        const mvfxRecordPass* earlier = &emitter->records.passes[p];
        if (!earlier->empty)
        {
            out->particles = earlier->resources.particles;
            out->statistics = earlier->resources.statistics;
            out->grids = earlier->resources.grids;
            return true;
        }
    }
    return mrhiImportBuffer(sim->device, emitter->particles[emitter->current], &out->particles) ==
               mrhi_success &&
           mrhiImportBuffer(sim->device, emitter->statistics, &out->statistics) == mrhi_success &&
           mrhiImportBuffer(sim->device, emitter->grids, &out->grids) == mrhi_success;
}

// The frame's resources a pass binds past the host's records and count:
// its inputs, the simulation's own count word where the host gives none,
// the emitter's sort words or the simulation's own for one without.
static bool PassInputs(mvfxGpuSim* sim, Emitter* emitter, mvfxRecordResources* resources)
{
    bool sorts = emitter->sort.index1 != 0;
    return RecordInputs(sim, emitter, resources) &&
           (resources->count.index1 != 0 ||
            mrhiImportBuffer(sim->device, sim->ownCount, &resources->count) == mrhi_success) &&
           mrhiImportBuffer(sim->device, sorts ? emitter->sort : sim->ownSort, &resources->sort) ==
               mrhi_success &&
           mrhiImportBuffer(sim->device, sorts ? emitter->digits : sim->ownDigits,
                            &resources->digits) == mrhi_success;
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
                                              (uint32_t)(def->countOffset / 4u), 1u)
                   ? mvfx_success
                   : mvfx_errorState;
    }
    mvfxRecordResources resources = {.records = def->records, .count = def->count};
    if (!PassInputs(sim, emitter, &resources))
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

mvfxResult mvfxGpuSim_AddRibbons(mvfxGpuSim* sim, const mvfxGpuRibbonsDef* def)
{
    if (sim == nullptr || def == nullptr || !IsRibbonsDefValid(sim, def))
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorState;
    }
    mvfxLayoutView layout;
    mvfxResult result = mvfxLayout_GetView(sim->context, def->layout, &layout);
    if (result != mvfx_success || !mvfxIsRibbonLayout(&layout))
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
    const mvfxEmitterView* view = &sim->views[def->emitter];
    if (emitter->capacity == 0 || view->ribbon.ids == mvfx_ribbonNone)
    {
        // No particles at the instance's quality level, or no ribbons:
        // counts of 0.
        return def->counts.index1 == 0 ||
                       mvfxAddEmptyRecordPass(sim->device, &emitter->records, def->counts,
                                              (uint32_t)(def->countsOffset / 4u),
                                              MVFX_GPU_RIBBON_COUNTS)
                   ? mvfx_success
                   : mvfx_errorState;
    }
    mvfxRecordResources resources = {
        .records = def->points, .count = def->counts, .list = def->ribbons};
    if (!PassInputs(sim, emitter, &resources))
    {
        return mvfx_errorState;
    }
    // The points' numbers as a record pass's, the list's first word
    // beside them.
    mvfxGpuRecordsDef records = mvfxDefaultGpuRecordsDef();
    records.emitter = def->emitter;
    records.view = def->view;
    records.offset = def->pointOffset;
    records.capacity = def->pointCapacity;
    uint32_t words[MVFX_RECORD_WORDS];
    mvfxRecordNumbers(view, &plan, &layout, &records, &emitter->tables, sim->srgb, words);
    words[7] = (uint32_t)(def->ribbonOffset / 4u);
    uint32_t ribbonWords[MVFX_RIBBON_WORDS];
    mvfxRibbonNumbers(view, &plan, &emitter->tables, def->ribbonCapacity,
                      def->counts.index1 != 0 ? (uint32_t)(def->countsOffset / 4u) : 0u,
                      ribbonWords);
    return mvfxAddRibbonPass(sim->device, &emitter->records, &resources, words, ribbonWords,
                             plan.capacity)
               ? mvfx_success
               : mvfx_errorState;
}
