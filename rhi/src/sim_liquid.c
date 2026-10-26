// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's particle liquid on the GPU (liquid.wgsl, sim/liquid.wgsl),
// after its kill pass, as the core's mvfxStepLiquid: its numbers written;
// then each substep with ticks its move (the sim kernel), the grid of the
// predicted places (its keys, the sort's four radix passes), each
// iteration's multipliers, corrections and slide (the sim kernel), the
// velocities' gain, then viscosity and confinement, each into scratch
// velocities kept after.

#include "allocator.h"
#include "kernels.h"
#include "numbers.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

#include <stdalign.h>
#include <string.h>

// The liquid module's bindings: its numbers, the particles' two buffers,
// the statistics, its words, its digit counts.
#define LIQUID_BINDINGS 6

// The passes of a substep: the move, the keys, the radix passes, three an
// iteration, the gain, and two for viscosity and three for confinement
// when it has them.
static int32_t SubstepPasses(const mvfxLiquidDef* def)
{
    return 2 + 2 * MVFX_SORT_PASSES + 3 * def->iterations + 1 + (def->viscosity > 0.0f ? 2 : 0) +
           (def->vorticity > 0.0f ? 3 : 0);
}

int32_t mvfxSimLiquidPasses(const mvfxEmitterView* view)
{
    const mvfxLiquidDef* def = &view->liquid;
    bool particles = def->enabled && def->solver == mvfx_liquidParticles;
    return particles ? 1 + def->substeps * SubstepPasses(def) : 0;
}

static bool CreateBuffer(mvfxGpuSim* sim, uint64_t size, mrhiBufferUsage usage, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return mrhiCreateBuffer(sim->device, &def, out) == mrhi_success;
}

bool mvfxCreateSimLiquid(mvfxGpuSim* sim, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    emitter->liquidPassCount = mvfxSimLiquidPasses(&sim->views[e]);
    emitter->liquid = emitter->liquidPassCount > 0;
    if (!emitter->liquid)
    {
        return true;
    }
    emitter->liquidPasses =
        mvfxRhiAllocate(&sim->allocator, (size_t)emitter->liquidPassCount * sizeof(mrhiPassId),
                        alignof(mrhiPassId));
    // The sort's keys and indices twice, the multipliers, three arrays of
    // scratch velocities and three of curls; two sets of digit counts.
    uint64_t c = (uint64_t)emitter->capacity;
    uint64_t blocks = mvfxSortBlocks(emitter->capacity);
    mrhiBufferId* b = emitter->liquidBuffers;
    return emitter->liquidPasses != nullptr &&
           CreateBuffer(sim, 4u * MVFX_LIQUID_WORDS,
                        mrhi_bufferUniform | mrhi_bufferCopyDestination, &b[LIQUID_NUMBERS]) &&
           CreateBuffer(sim, 4u * (4u * blocks * MVFX_SORT_BLOCK + 7u * c), mrhi_bufferStorage,
                        &b[LIQUID_WORDS]) &&
           CreateBuffer(sim, 4u * 2u * 256u * blocks, mrhi_bufferStorage, &b[LIQUID_DIGITS]);
}

void mvfxDestroySimLiquid(mvfxGpuSim* sim, Emitter* emitter)
{
    for (int32_t b = 0; b < LIQUID_BUFFERS; ++b)
    {
        if (emitter->liquidBuffers[b].index1 != 0)
        {
            (void)mrhiDestroyBuffer(sim->device, emitter->liquidBuffers[b]);
        }
    }
    mvfxRhiRelease(&sim->allocator, emitter->liquidPasses,
                   (size_t)emitter->liquidPassCount * sizeof(mrhiPassId), alignof(mrhiPassId));
    emitter->liquidPasses = nullptr;
}

void mvfxSimLiquidNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan)
{
    Emitter* emitter = &sim->emitters[e];
    if (!emitter->liquid)
    {
        return;
    }
    const mvfxEmitterView* view = &sim->views[e];
    const mvfxLiquidDef* def = &view->liquid;
    const mvfxPlannedLiquid* l = &plan->liquid;
    uint32_t* w = emitter->liquidWords;
    uint32_t groups = mvfxUpdateGroups(plan->capacity);
    const uint32_t head[8] = {(uint32_t)plan->capacity,          mvfxWordOf(view->arrays.position),
                              mvfxWordOf(view->arrays.velocity), (uint32_t)view->arrays.count + 2u,
                              mvfxTableMask(plan->capacity),     (uint32_t)def->maxNeighbors,
                              (uint32_t)def->tensilePower,       def->planar ? 1u : 0u};
    memcpy(w, head, sizeof head);
    // The kernels in their unit (a power of two, so dividing by it is
    // exact), and the confinement's strength over it.
    float inverseUnit = 1.0f / l->unit;
    mvfxFloatWords(w + 8, 1.0f / l->radius, l->radius * inverseUnit, l->square, l->poly6);
    mvfxFloatWords(w + 12, l->spiky, l->restInverse, l->softening, l->tensileInverse);
    mvfxFloatWords(w + 16, l->tensileScale, def->viscosity,
                   def->vorticity * l->spacing * inverseUnit, inverseUnit);
    uint32_t blocks = mvfxSortBlocks(plan->capacity);
    const uint32_t tail[4] = {(uint32_t)plan->ticks, (uint32_t)def->substeps,
                              groups > 0u ? groups : 1u, blocks < 65535u ? blocks : 65535u};
    memcpy(w + 20, tail, sizeof tail);
    emitter->liquidTicks = plan->ticks;
}

// Whether substep s of the step has ticks, as the core's: those that have
// none are skipped.
static bool HasTicks(const Emitter* emitter, int32_t s)
{
    int64_t ticks = emitter->liquidTicks;
    int64_t substeps = emitter->liquidWords[21];
    return ticks * (s + 1) / substeps != ticks * s / substeps;
}

static bool AddPass(mvfxGpuSim* sim, mrhiPassClass passClass, const mrhiAccess* accesses,
                    int32_t count, mrhiPassId* passOut)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = passClass;
    def.accesses = accesses;
    def.accessCount = (uint32_t)count;
    // Its writes outlive the frame, read by the next step or the host.
    def.neverCull = true;
    return mrhiAddPass(sim->device, &def, passOut) == mrhi_success;
}

// The kind of a substep's pass at: the sim kernel's (the move, the
// slides), the sort's, or the liquid module's.
static int32_t KindOf(const mvfxLiquidDef* def, int32_t at)
{
    int32_t iterations = 2 + 2 * MVFX_SORT_PASSES;
    bool slide =
        at >= iterations && at < iterations + 3 * def->iterations && (at - iterations) % 3 == 2;
    if (at == 0 || slide)
    {
        return 0;
    }
    return at >= 2 && at < iterations ? 1 : 2;
}

bool mvfxAddSimLiquidPasses(mvfxGpuSim* sim, Emitter* emitter, const mrhiAccess* used,
                            int32_t count)
{
    mrhiResourceId* q = emitter->liquidResources;
    for (int32_t b = 0; b < LIQUID_BUFFERS; ++b)
    {
        if (mrhiImportBuffer(sim->device, emitter->liquidBuffers[b], &q[b]) != mrhi_success)
        {
            return false;
        }
    }
    const mrhiResourceId* r = emitter->resources;
    const mrhiAccess written = {.resource = q[LIQUID_NUMBERS], .kind = mrhi_accessCopyDestination};
    const mrhiAccess own[LIQUID_BINDINGS] = {
        {.resource = q[LIQUID_NUMBERS], .kind = mrhi_accessUniform},
        {.resource = r[BUFFER_CURRENT], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_OTHER], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageReadWrite},
        {.resource = q[LIQUID_WORDS], .kind = mrhi_accessStorageReadWrite},
        {.resource = q[LIQUID_DIGITS], .kind = mrhi_accessStorageReadWrite}};
    const mrhiAccess sorted[3] = {
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageRead},
        {.resource = q[LIQUID_WORDS], .kind = mrhi_accessStorageReadWrite},
        {.resource = q[LIQUID_DIGITS], .kind = mrhi_accessStorageReadWrite}};
    const mvfxLiquidDef* def = &sim->views[emitter - sim->emitters].liquid;
    int32_t per = SubstepPasses(def);
    bool added = AddPass(sim, mrhi_passTransfer, &written, 1, &emitter->liquidPasses[0]);
    for (int32_t p = 1; added && p < emitter->liquidPassCount; ++p)
    {
        int32_t kind = KindOf(def, (p - 1) % per);
        const mrhiAccess* accesses = kind == 0 ? used : (kind == 1 ? sorted : own);
        int32_t n = kind == 0 ? count : (kind == 1 ? 3 : LIQUID_BINDINGS);
        added = !HasTicks(emitter, (p - 1) / per) ||
                AddPass(sim, mrhi_passGraphics, accesses, n, &emitter->liquidPasses[p]);
    }
    return added;
}

// Records a liquid module's or the sort's pass.
static bool RecordKernel(mvfxGpuSim* sim, mrhiPassId pass, mvfxKernel kernel,
                         const mrhiResourceId* bound, int32_t count, const uint32_t root[4],
                         uint32_t groups)
{
    mrhiBinding bindings[LIQUID_BINDINGS];
    for (int32_t b = 0; b < count; ++b)
    {
        bindings[b] =
            (mrhiBinding){.slot = (uint32_t)b, .resource = bound[b], .size = MRHI_WHOLE_SIZE};
    }
    bool ran = mrhiBeginPass(sim->device, pass) == mrhi_success &&
               mrhiSetComputePipeline(sim->device, pass, mvfxGpuPipeline(sim->gpu, kernel)) ==
                   mrhi_success &&
               mrhiSetBindings(sim->device, pass, 0, bindings, (uint32_t)count) == mrhi_success &&
               mrhiSetRootBlock(sim->device, pass, 0, root, 4 * sizeof(uint32_t)) == mrhi_success &&
               mrhiDispatch(sim->device, pass, groups, 1, 1) == mrhi_success;
    return mrhiEndPass(sim->device, pass) == mrhi_success && ran;
}

// Records substep s's passes from passes: the move, the grid, the
// iterations, the gain, viscosity and confinement.
static bool RecordSubstep(mvfxGpuSim* sim, const Emitter* emitter, const mrhiPassId* passes,
                          int32_t s)
{
    const mvfxLiquidDef* def = &sim->views[emitter - sim->emitters].liquid;
    const mrhiResourceId* q = emitter->liquidResources;
    const mrhiResourceId* r = emitter->resources;
    const mrhiResourceId bound[LIQUID_BINDINGS] = {q[LIQUID_NUMBERS], r[BUFFER_CURRENT],
                                                   r[BUFFER_OTHER],   r[BUFFER_STATISTICS],
                                                   q[LIQUID_WORDS],   q[LIQUID_DIGITS]};
    const mrhiResourceId sort[3] = {r[BUFFER_STATISTICS], q[LIQUID_WORDS], q[LIQUID_DIGITS]};
    uint32_t groups = emitter->liquidWords[22];
    uint32_t blocks = emitter->liquidWords[23];
    const uint32_t sim0[4] = {0, (uint32_t)s, 0, 0};
    const uint32_t root[4] = {(uint32_t)s, 0, 0, 0};
    bool recorded =
        mvfxRecordSimDispatch(sim, emitter, passes[0], mvfx_kernelLiquidMove, groups, sim0) &&
        RecordKernel(sim, passes[1], mvfx_kernelLiquidKeys, bound, LIQUID_BINDINGS, root, blocks);
    int32_t at = 2;
    for (uint32_t p = 0; recorded && p < MVFX_SORT_PASSES; ++p)
    {
        const uint32_t radix[4] = {8u * p, (uint32_t)emitter->capacity, blocks, 0};
        recorded =
            RecordKernel(sim, passes[at], mvfx_kernelSortOffsets, sort, 3, radix, 1) &&
            RecordKernel(sim, passes[at + 1], mvfx_kernelSortScatter, sort, 3, radix, blocks);
        at += 2;
    }
    for (int32_t n = 0; recorded && n < def->iterations; ++n)
    {
        // The capped are counted in the step's first substep's first
        // iteration, as the core's.
        const uint32_t first[4] = {(uint32_t)s, s == 0 && n == 0 ? 1u : 0u, 0, 0};
        recorded = RecordKernel(sim, passes[at], mvfx_kernelLiquidMultipliers, bound,
                                LIQUID_BINDINGS, first, groups) &&
                   RecordKernel(sim, passes[at + 1], mvfx_kernelLiquidCorrections, bound,
                                LIQUID_BINDINGS, root, groups) &&
                   mvfxRecordSimDispatch(sim, emitter, passes[at + 2], mvfx_kernelLiquidSlide,
                                         groups, sim0);
        at += 3;
    }
    recorded = recorded && RecordKernel(sim, passes[at++], mvfx_kernelLiquidGain, bound,
                                        LIQUID_BINDINGS, root, groups);
    static const mvfxKernel viscosity[2] = {mvfx_kernelLiquidViscosity, mvfx_kernelLiquidKeep};
    static const mvfxKernel vorticity[3] = {mvfx_kernelLiquidCurls, mvfx_kernelLiquidConfine,
                                            mvfx_kernelLiquidKeep};
    for (int32_t k = 0; recorded && def->viscosity > 0.0f && k < 2; ++k)
    {
        recorded =
            RecordKernel(sim, passes[at++], viscosity[k], bound, LIQUID_BINDINGS, root, groups);
    }
    for (int32_t k = 0; recorded && def->vorticity > 0.0f && k < 3; ++k)
    {
        recorded =
            RecordKernel(sim, passes[at++], vorticity[k], bound, LIQUID_BINDINGS, root, groups);
    }
    return recorded;
}

bool mvfxRecordSimLiquid(mvfxGpuSim* sim, Emitter* emitter)
{
    mrhiPassId pass = emitter->liquidPasses[0];
    bool recorded =
        mrhiBeginPass(sim->device, pass) == mrhi_success &&
        mvfxUpload(sim->device, &sim->uploaded, pass, emitter->liquidResources[LIQUID_NUMBERS], 0,
                   emitter->liquidWords, sizeof emitter->liquidWords);
    recorded = mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
    const mvfxLiquidDef* def = &sim->views[emitter - sim->emitters].liquid;
    int32_t per = SubstepPasses(def);
    for (int32_t s = 0; recorded && s < def->substeps; ++s)
    {
        recorded = !HasTicks(emitter, s) ||
                   RecordSubstep(sim, emitter, emitter->liquidPasses + 1 + s * per, s);
    }
    return recorded;
}
