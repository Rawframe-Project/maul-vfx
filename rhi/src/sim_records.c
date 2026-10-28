// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A simulation's record, ribbon and lights passes: a def checked, the frame's
// particles they read, the pass added with its numbers; the counts of a
// statistics record a clear empties, and the bounds it gives.

#include "numbers.h"
#include "record.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/resources.h"

#include <math.h>
#include <string.h>

mvfxGpuRecordsDef mvfxDefaultGpuRecordsDef(void)
{
    return (mvfxGpuRecordsDef){.cookie = MVFX_GPU_RECORDS_DEF_COOKIE};
}

mvfxGpuLightsDef mvfxDefaultGpuLightsDef(void)
{
    return (mvfxGpuLightsDef){.cookie = MVFX_GPU_LIGHTS_DEF_COOKIE};
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

// Whether a ribbons def is one a pass can be added from, past its layout.
static bool IsRibbonsDefValid(const mvfxGpuSim* sim, const mvfxGpuRibbonsDef* def)
{
    return def->cookie == MVFX_GPU_RIBBONS_DEF_COOKIE && def->emitter >= 0 &&
           def->emitter < sim->emitterCount && def->points.index1 != 0 &&
           def->ribbons.index1 != 0 && def->pointOffset % 4u == 0 && def->ribbonOffset % 4u == 0 &&
           def->countsOffset % 4u == 0 && def->pointCapacity >= 0 && def->ribbonCapacity >= 0 &&
           IsViewWithin(&def->view);
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
// the emitter's sort words and digit counts or the simulation's own for
// one without (one with lights alone has digit counts).
static bool PassInputs(mvfxGpuSim* sim, Emitter* emitter, mvfxRecordResources* resources)
{
    bool sorts = emitter->sort.index1 != 0;
    bool counts = emitter->digits.index1 != 0;
    return RecordInputs(sim, emitter, resources) &&
           (resources->count.index1 != 0 ||
            mrhiImportBuffer(sim->device, sim->ownCount, &resources->count) == mrhi_success) &&
           mrhiImportBuffer(sim->device, sorts ? emitter->sort : sim->ownSort, &resources->sort) ==
               mrhi_success &&
           mrhiImportBuffer(sim->device, counts ? emitter->digits : sim->ownDigits,
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
        return mvfx_errorBusy;
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
    result = mvfxSimEmitterPlan(sim, def->emitter, &plan);
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

mvfxResult mvfxGpuSim_AddLights(mvfxGpuSim* sim, const mvfxGpuLightsDef* def)
{
    if (sim == nullptr || def == nullptr || def->cookie != MVFX_GPU_LIGHTS_DEF_COOKIE ||
        def->emitter < 0 || def->emitter >= sim->emitterCount || def->lights.index1 == 0 ||
        def->offset % 4u != 0 || def->countOffset % 4u != 0 || def->capacity < 0 ||
        !IsViewWithin(&def->view))
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorBusy;
    }
    Emitter* emitter = &sim->emitters[def->emitter];
    if (emitter->records.count == MVFX_GPU_RECORD_PASSES)
    {
        return mvfx_errorLimit;
    }
    mvfxEmitterPlan plan;
    mvfxResult result = mvfxSimEmitterPlan(sim, def->emitter, &plan);
    if (result != mvfx_success)
    {
        return result;
    }
    const mvfxEmitterView* view = &sim->views[def->emitter];
    if (emitter->capacity == 0 || !view->light.enabled)
    {
        // No particles at the instance's quality level, or no lights: a
        // count of 0.
        return def->count.index1 == 0 ||
                       mvfxAddEmptyRecordPass(sim->device, &emitter->records, def->count,
                                              (uint32_t)(def->countOffset / 4u), 1u)
                   ? mvfx_success
                   : mvfx_errorState;
    }
    mvfxRecordResources resources = {.records = def->lights, .count = def->count};
    if (!PassInputs(sim, emitter, &resources))
    {
        return mvfx_errorState;
    }
    uint32_t words[MVFX_RECORD_WORDS];
    mvfxLightNumbers(view, &plan, def, &emitter->tables, sim->srgb, words);
    return mvfxAddLightPass(sim->device, &emitter->records, &resources, words) ? mvfx_success
                                                                               : mvfx_errorState;
}

// Adds a ribbon pass writing what kind of sink asks (points with the list,
// strips with indices in the def's ribbons buffer, its capacity theirs),
// from a def checked: of the emitter's ribbons, or of its plexus links
// (their buffer bound for the sort's, read as chains of two).
static mvfxResult AddRibbons(mvfxGpuSim* sim, const mvfxGpuRibbonsDef* def, mvfxRibbonSink sink,
                             bool links)
{
    if (sim->ending)
    {
        return mvfx_errorBusy;
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
    result = mvfxSimEmitterPlan(sim, def->emitter, &plan);
    if (result != mvfx_success)
    {
        return result;
    }
    const mvfxEmitterView* view = &sim->views[def->emitter];
    if (emitter->capacity == 0 ||
        (links ? !view->plexus.enabled : view->ribbon.ids == mvfx_ribbonNone))
    {
        // No particles at the instance's quality level, or no ribbons or
        // links: counts of 0.
        return def->counts.index1 == 0 ||
                       mvfxAddEmptyRecordPass(sim->device, &emitter->records, def->counts,
                                              (uint32_t)(def->countsOffset / 4u),
                                              MVFX_GPU_RIBBON_COUNTS)
                   ? mvfx_success
                   : mvfx_errorState;
    }
    mvfxRecordResources resources = {
        .records = def->points, .count = def->counts, .list = def->ribbons};
    if (!PassInputs(sim, emitter, &resources) ||
        (links && mrhiImportBuffer(sim->device, emitter->plexusBuffers[PLEXUS_LINKS],
                                   &resources.sort) != mrhi_success))
    {
        return mvfx_errorState;
    }
    // The points' numbers as a record pass's, the list's (or the
    // indices') first word beside them.
    mvfxGpuRecordsDef records = mvfxDefaultGpuRecordsDef();
    records.emitter = def->emitter;
    records.view = def->view;
    records.offset = def->pointOffset;
    records.capacity = def->pointCapacity;
    uint32_t words[MVFX_RECORD_WORDS];
    mvfxRecordNumbers(view, &plan, &layout, &records, &emitter->tables, sim->srgb, words);
    words[7] = (uint32_t)(def->ribbonOffset / 4u);
    uint32_t ribbonWords[MVFX_RIBBON_WORDS];
    sink.capacity = def->ribbonCapacity;
    sink.countWord = def->counts.index1 != 0 ? (uint32_t)(def->countsOffset / 4u) : 0u;
    if (links)
    {
        mvfxLinkNumbers(view, &sink, ribbonWords);
    }
    else
    {
        mvfxRibbonNumbers(view, &plan, &emitter->tables, &sink, ribbonWords);
    }
    return mvfxAddRibbonPass(sim->device, &emitter->records, &resources, words, ribbonWords,
                             links ? 2 * (int64_t)view->plexus.maxLinks : plan.capacity)
               ? mvfx_success
               : mvfx_errorState;
}

mvfxResult mvfxGpuSim_AddRibbons(mvfxGpuSim* sim, const mvfxGpuRibbonsDef* def)
{
    if (sim == nullptr || def == nullptr || !IsRibbonsDefValid(sim, def))
    {
        return mvfx_errorInvalid;
    }
    return AddRibbons(sim, def, (mvfxRibbonSink){.kind = MVFX_SINK_POINTS}, false);
}

mvfxResult mvfxGpuSim_AddPlexus(mvfxGpuSim* sim, const mvfxGpuRibbonsDef* def)
{
    if (sim == nullptr || def == nullptr || !IsRibbonsDefValid(sim, def))
    {
        return mvfx_errorInvalid;
    }
    return AddRibbons(sim, def, (mvfxRibbonSink){.kind = MVFX_SINK_POINTS}, true);
}

mvfxGpuExpandDef mvfxDefaultGpuExpandDef(void)
{
    return (mvfxGpuExpandDef){.cookie = MVFX_GPU_EXPAND_DEF_COOKIE};
}

// Adds the passes expanding ribbons into strips, or into tubes of the
// def's sides (strips for 0) when tubes; or plexus links into strips.
static mvfxResult Expand(mvfxGpuSim* sim, const mvfxGpuExpandDef* def, bool tubes, bool links)
{
    if (sim == nullptr || def == nullptr || def->cookie != MVFX_GPU_EXPAND_DEF_COOKIE)
    {
        return mvfx_errorInvalid;
    }
    int32_t sides = tubes ? def->tube.sides : 0;
    if (sides != 0 && (sides < 3 || sides > MVFX_MAX_TUBE_SIDES))
    {
        return mvfx_errorInvalid;
    }
    // A ribbons def of the same buffers, its list the indices.
    mvfxGpuRibbonsDef ribbons = {.cookie = MVFX_GPU_RIBBONS_DEF_COOKIE,
                                 .emitter = def->emitter,
                                 .layout = def->layout,
                                 .view = def->view,
                                 .points = def->vertices,
                                 .pointOffset = def->vertexOffset,
                                 .pointCapacity = def->vertexCapacity,
                                 .ribbons = def->indices,
                                 .ribbonOffset = def->indexOffset,
                                 .ribbonCapacity = def->indexCapacity,
                                 .counts = def->counts,
                                 .countsOffset = def->countsOffset};
    if (!IsRibbonsDefValid(sim, &ribbons))
    {
        return mvfx_errorInvalid;
    }
    const mvfxRibbonSink sink = {.kind = sides > 0 ? MVFX_SINK_TUBES : MVFX_SINK_STRIPS,
                                 .sides = (uint32_t)sides,
                                 .caps = sides > 0 && def->tube.caps};
    return AddRibbons(sim, &ribbons, sink, links);
}

mvfxResult mvfxGpuSim_ExpandRibbons(mvfxGpuSim* sim, const mvfxGpuExpandDef* def)
{
    return Expand(sim, def, false, false);
}

mvfxResult mvfxGpuSim_ExpandTubes(mvfxGpuSim* sim, const mvfxGpuExpandDef* def)
{
    return Expand(sim, def, true, false);
}

mvfxResult mvfxGpuSim_ExpandPlexus(mvfxGpuSim* sim, const mvfxGpuExpandDef* def)
{
    return Expand(sim, def, false, true);
}

// Checks an expand def for quads: its buffers as a ribbons def's, its
// layout's view, the emitter's passes this frame; its plan.
static mvfxResult CheckQuads(mvfxGpuSim* sim, const mvfxGpuExpandDef* def, mvfxLayoutView* layout,
                             mvfxEmitterPlan* plan)
{
    if (sim == nullptr || def == nullptr || def->cookie != MVFX_GPU_EXPAND_DEF_COOKIE)
    {
        return mvfx_errorInvalid;
    }
    // A ribbons def of the same buffers checks them.
    const mvfxGpuRibbonsDef buffers = {.cookie = MVFX_GPU_RIBBONS_DEF_COOKIE,
                                       .emitter = def->emitter,
                                       .view = def->view,
                                       .points = def->vertices,
                                       .pointOffset = def->vertexOffset,
                                       .pointCapacity = def->vertexCapacity,
                                       .ribbons = def->indices,
                                       .ribbonOffset = def->indexOffset,
                                       .ribbonCapacity = def->indexCapacity,
                                       .countsOffset = def->countsOffset};
    if (!IsRibbonsDefValid(sim, &buffers))
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorBusy;
    }
    mvfxResult result = mvfxLayout_GetView(sim->context, def->layout, layout);
    if (result != mvfx_success || !mvfxIsRibbonLayout(layout))
    {
        return result != mvfx_success ? result : mvfx_errorInvalid;
    }
    if (sim->emitters[def->emitter].records.count == MVFX_GPU_RECORD_PASSES)
    {
        return mvfx_errorLimit;
    }
    return mvfxSimEmitterPlan(sim, def->emitter, plan);
}

// Adds the passes expanding an emitter's particles into a sink's quads
// (sprites or sheets): the vertices' numbers as a record pass's with the
// indices' first word beside them; counts of 0 for an emitter without
// particles at the instance's quality level, or of sheets for one of no
// sheets.
static mvfxResult ExpandQuads(mvfxGpuSim* sim, const mvfxGpuExpandDef* def, uint32_t kind)
{
    mvfxLayoutView layout;
    mvfxEmitterPlan plan;
    mvfxResult result = CheckQuads(sim, def, &layout, &plan);
    if (result != mvfx_success)
    {
        return result;
    }
    Emitter* emitter = &sim->emitters[def->emitter];
    const mvfxEmitterView* view = &sim->views[def->emitter];
    bool sheets = view->unitPoints > 1 && view->unit.kind == mvfx_unitSheet;
    if (emitter->capacity == 0 || (kind == MVFX_SINK_SHEETS && !sheets))
    {
        return def->counts.index1 == 0 ||
                       mvfxAddEmptyRecordPass(sim->device, &emitter->records, def->counts,
                                              (uint32_t)(def->countsOffset / 4u),
                                              MVFX_GPU_RIBBON_COUNTS)
                   ? mvfx_success
                   : mvfx_errorState;
    }
    mvfxRecordResources resources = {
        .records = def->vertices, .count = def->counts, .list = def->indices};
    if (!PassInputs(sim, emitter, &resources))
    {
        return mvfx_errorState;
    }
    mvfxGpuRecordsDef records = mvfxDefaultGpuRecordsDef();
    records.emitter = def->emitter;
    records.view = def->view;
    records.offset = def->vertexOffset;
    records.capacity = def->vertexCapacity;
    uint32_t words[MVFX_RECORD_WORDS];
    mvfxRecordNumbers(view, &plan, &layout, &records, &emitter->tables, sim->srgb, words);
    words[7] = (uint32_t)(def->indexOffset / 4u);
    const mvfxRibbonSink sink = {
        .kind = kind,
        .capacity = def->indexCapacity,
        .countWord = def->counts.index1 != 0 ? (uint32_t)(def->countsOffset / 4u) : 0u};
    uint32_t sinkWords[MVFX_RIBBON_WORDS];
    if (kind == MVFX_SINK_SHEETS)
    {
        mvfxSheetNumbers(view, &sink, sinkWords);
    }
    else
    {
        mvfxSpriteNumbers(view, &def->view, &sink, sinkWords);
    }
    return mvfxAddRibbonPass(sim->device, &emitter->records, &resources, words, sinkWords,
                             plan.capacity)
               ? mvfx_success
               : mvfx_errorState;
}

mvfxResult mvfxGpuSim_ExpandSprites(mvfxGpuSim* sim, const mvfxGpuExpandDef* def)
{
    return ExpandQuads(sim, def, MVFX_SINK_SPRITES);
}

mvfxResult mvfxGpuSim_ExpandSheets(mvfxGpuSim* sim, const mvfxGpuExpandDef* def)
{
    return ExpandQuads(sim, def, MVFX_SINK_SHEETS);
}

// Writes the map, and the head when the ribbons pin one. After a clear
// and at the simulation's first step (a simulation may be made for an
// instance that has planned before), the whole record goes up instead in
// one upload, every other word 0: the counts a clear empties, and the
// words of every count the emitter does not keep, which a host reading
// the whole record then reads as 0. One upload, as copies within a pass
// are not ordered.
bool mvfxWriteSimRecord(mvfxGpuSim* sim, mrhiPassId pass, Emitter* emitter)
{
    mrhiResourceId statistics = emitter->resources[BUFFER_STATISTICS];
    bool written = false;
    if (emitter->cleared || emitter->first)
    {
        uint32_t record[MVFX_GPU_STATISTICS_WORDS] = {0};
        memcpy(&record[MVFX_RECORD_SPACE], emitter->map, sizeof emitter->map);
        if (emitter->pinsHead)
        {
            memcpy(&record[MVFX_RECORD_HEAD], emitter->head, sizeof emitter->head);
        }
        written =
            mvfxUpload(sim->device, &sim->uploaded, pass, statistics, 0, record, sizeof record);
    }
    else
    {
        written =
            mvfxUpload(sim->device, &sim->uploaded, pass, statistics,
                       MVFX_RECORD_SPACE * sizeof(uint32_t), emitter->map, sizeof emitter->map) &&
            (!emitter->pinsHead ||
             mvfxUpload(sim->device, &sim->uploaded, pass, statistics,
                        MVFX_RECORD_HEAD * sizeof(uint32_t), emitter->head, sizeof emitter->head));
    }
    // Set in the lane recorded, so the last step's lane, which goes on
    // into the next frame, holds it.
    emitter->zeroed = emitter->zeroed || written;
    return written;
}

// Both particle buffers, and the events buffer when the emitter has its
// own (its events, and its scratch for births from events, tears and
// crossings).
static int32_t ClearedBuffers(const Emitter* emitter)
{
    return emitter->events.index1 != 0 ? 3 : 2;
}

void mvfxClearedSimBuffers(const Emitter* emitter, mrhiAccess* accesses, int32_t* count)
{
    static const int32_t buffers[3] = {BUFFER_CURRENT, BUFFER_OTHER, BUFFER_EVENTS};
    for (int32_t b = 0; emitter->first && b < ClearedBuffers(emitter); ++b)
    {
        accesses[*count] = (mrhiAccess){.resource = emitter->resources[buffers[b]],
                                        .kind = mrhi_accessCopyDestination};
        *count += 1;
    }
}

bool mvfxClearSimBuffers(mvfxGpuSim* sim, mrhiPassId pass, const Emitter* emitter)
{
    static const int32_t buffers[3] = {BUFFER_CURRENT, BUFFER_OTHER, BUFFER_EVENTS};
    bool cleared = true;
    for (int32_t b = 0; cleared && b < ClearedBuffers(emitter); ++b)
    {
        cleared = mrhiClearBuffer(sim->device, pass, emitter->resources[buffers[b]], 0,
                                  MRHI_WHOLE_SIZE) == mrhi_success;
    }
    return cleared;
}

mvfxResult mvfxGpuSim_BoundsOf(const mvfxGpuSim* sim, int32_t emitter, const uint32_t* record,
                               mvfxBounds* boundsOut)
{
    if (boundsOut != nullptr)
    {
        *boundsOut = (mvfxBounds){.empty = true};
    }
    if (sim == nullptr || record == nullptr || boundsOut == nullptr || emitter < 0 ||
        emitter >= sim->emitterCount)
    {
        return mvfx_errorInvalid;
    }
    *boundsOut = mvfxRecordBounds(&sim->views[emitter], record);
    return mvfx_success;
}
