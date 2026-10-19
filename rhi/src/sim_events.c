// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's events on the GPU: the kernels counting and writing them
// (sim.wgsl), and the records pass giving them their values over life
// (records.wgsl), its numbers and buffers.

#include "kernels.h"
#include "numbers.h"
#include "record.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

// Creates an emitter's events buffer when it writes events or spawns from
// another's (its spawn region after its own events, as many of the
// source's as it writes at most), and its events' numbers.
bool mvfxCreateSimEvents(mvfxGpuSim* sim, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    const mvfxEmitterView* view = &sim->views[e];
    emitter->writesEvents = view->events != 0;
    bool spawns = emitter->spawnSource >= 0;
    if (!emitter->writesEvents && !spawns)
    {
        return true;
    }
    uint64_t words = mvfxEventBufferWords(emitter->capacity, view->eventCapacity) +
                     (spawns ? mvfxSpawnWords(sim->views[emitter->spawnSource].eventCapacity) : 0);
    // A unit's point's tears of a step, its edges' facts after the rest.
    bool tears = view->unitPoints > 1 && view->unit.tearStrain > 0.0f &&
                 (view->events & mvfx_eventMaskTear) != 0;
    emitter->tearFacts = tears ? (uint32_t)words : 0u;
    words += tears ? (uint64_t)MVFX_TEAR_FACTS * (uint64_t)emitter->capacity : 0u;
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 4u * words;
    def.usage = mrhi_bufferStorage | mrhi_bufferCopySource |
                (spawns ? mrhi_bufferCopyDestination : (mrhiBufferUsage)0);
    if (mrhiCreateBuffer(sim->device, &def, &emitter->events) != mrhi_success)
    {
        return false;
    }
    def.size = MVFX_RECORD_WORDS * 4u;
    def.usage = mrhi_bufferUniform | mrhi_bufferCopyDestination;
    return !emitter->writesEvents ||
           mrhiCreateBuffer(sim->device, &def, &emitter->eventNumbers) == mrhi_success;
}

// The numbers of an emitter's events' values over life: a record pass's
// over its events, no field written, its workgroups striding over them.
void mvfxSimEventNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan)
{
    Emitter* emitter = &sim->emitters[e];
    if (!emitter->writesEvents)
    {
        return;
    }
    static const mvfxLayoutView none = {0};
    mvfxGpuRecordsDef def = mvfxDefaultGpuRecordsDef();
    def.emitter = e;
    def.offset = 4u * mvfxEventFirst(emitter->capacity);
    def.capacity = sim->views[e].eventCapacity;
    mvfxRecordNumbers(&sim->views[e], plan, &none, &def, &emitter->tables, sim->srgb,
                      emitter->eventWords);
    uint32_t groups = ((uint32_t)def.capacity + 63u) / 64u;
    emitter->eventWords[5] = groups < 65535u ? groups : 65535u;
}

// Imports what the pass giving an emitter's events their values over life
// binds beside the step's buffers.
bool mvfxImportEventValues(mvfxGpuSim* sim, Emitter* emitter)
{
    const mrhiBufferId own[4] = {emitter->eventNumbers, sim->ownCount, sim->ownSort,
                                 sim->ownDigits};
    bool imported = true;
    for (int32_t b = 0; imported && b < 4; ++b)
    {
        imported =
            mrhiImportBuffer(sim->device, own[b], &emitter->eventResources[b]) == mrhi_success;
    }
    return imported;
}

// Adds the pass giving an emitter's events their values over life, which
// binds the records kernel's buffers: its numbers, the particles (unread),
// the statistics, the grids, the events (as records), and the
// simulation's words for a count and a sort.
bool mvfxAddEventValuesPass(mvfxGpuSim* sim, Emitter* emitter)
{
    const mrhiResourceId* r = emitter->resources;
    const mrhiResourceId* o = emitter->eventResources;
    const mrhiAccess used[8] = {{.resource = o[0], .kind = mrhi_accessUniform},
                                {.resource = r[BUFFER_OTHER], .kind = mrhi_accessStorageRead},
                                {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageRead},
                                {.resource = r[BUFFER_GRIDS], .kind = mrhi_accessStorageRead},
                                {.resource = r[BUFFER_EVENTS], .kind = mrhi_accessStorageReadWrite},
                                {.resource = o[1], .kind = mrhi_accessStorageReadWrite},
                                {.resource = o[2], .kind = mrhi_accessStorageReadWrite},
                                {.resource = o[3], .kind = mrhi_accessStorageReadWrite}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passGraphics;
    def.accesses = used;
    def.accessCount = 8;
    def.neverCull = true;
    return mrhiAddPass(sim->device, &def, &emitter->passes[PASS_EVENT_VALUES]) == mrhi_success;
}

// Records the writes of the events' numbers their pass reads: all of them
// at the first step, the words a plan changes after.
bool mvfxWriteEventNumbers(mvfxGpuSim* sim, mrhiPassId pass, Emitter* emitter)
{
    mrhiResourceId numbers = emitter->eventResources[0];
    if (!emitter->eventNumbersWritten)
    {
        emitter->eventNumbersWritten = mvfxUpload(sim->device, &sim->uploaded, pass, numbers, 0,
                                                  emitter->eventWords, sizeof emitter->eventWords);
        return emitter->eventNumbersWritten;
    }
    static const uint32_t spans[2] = {0, MVFX_RECORD_MAP};
    bool written = true;
    for (int32_t s = 0; written && s < 2; ++s)
    {
        written = mvfxUpload(sim->device, &sim->uploaded, pass, numbers, spans[s] * 4u,
                             emitter->eventWords + spans[s], MVFX_RECORD_SPAN * 4u);
    }
    return written;
}

// Records the pass giving an emitter's events their values over life.
static bool RecordEventValues(mvfxGpuSim* sim, const Emitter* emitter)
{
    mrhiPassId pass = emitter->passes[PASS_EVENT_VALUES];
    const mrhiResourceId* r = emitter->resources;
    const mrhiResourceId* o = emitter->eventResources;
    const mrhiResourceId bound[8] = {
        o[0], r[BUFFER_OTHER], r[BUFFER_STATISTICS], r[BUFFER_GRIDS], r[BUFFER_EVENTS], o[1], o[2],
        o[3]};
    mrhiBinding bindings[8];
    for (int32_t b = 0; b < 8; ++b)
    {
        bindings[b] =
            (mrhiBinding){.slot = (uint32_t)b, .resource = bound[b], .size = MRHI_WHOLE_SIZE};
    }
    bool recorded =
        mrhiBeginPass(sim->device, pass) == mrhi_success &&
        mrhiSetComputePipeline(sim->device, pass,
                               mvfxGpuPipeline(sim->gpu, mvfx_kernelEventValues)) == mrhi_success &&
        mrhiSetBindings(sim->device, pass, 0, bindings, 8) == mrhi_success &&
        mrhiDispatch(sim->device, pass, emitter->eventWords[5], 1, 1) == mrhi_success;
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
}

// Records an emitter's events kernels, when it writes events.
bool mvfxRecordSimEvents(mvfxGpuSim* sim, const Emitter* emitter, uint32_t blocks)
{
    return !emitter->writesEvents ||
           (mvfxRecordSimKernel(sim, emitter, PASS_EVENT_COUNT, mvfx_kernelEventCount, blocks) &&
            mvfxRecordSimKernel(sim, emitter, PASS_EVENT_OFFSETS, mvfx_kernelEventOffsets, 1) &&
            mvfxRecordSimKernel(sim, emitter, PASS_EVENT_WRITE, mvfx_kernelEventWrite, blocks) &&
            RecordEventValues(sim, emitter));
}
