// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's units' self-collision on the GPU (self.wgsl), in place of
// its unit pass: its numbers written, its reach, its keys, the sort's four
// radix passes over them (sort.wgsl), its gather; then each substep the
// unit pass's first half (sim/units.wgsl's UNITS_MOVE), the part and the
// commit when the substep has ticks, and the unit pass's rest
// (UNITS_REST), as the core's mvfxStepUnits.

#include "kernels.h"
#include "numbers.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

// The self passes: the numbers' write, the reach, the keys, the radix
// passes from the fourth, the gather; then four a substep.
#define SELF_WRITE  0
#define SELF_REACH  1
#define SELF_KEYS   2
#define SELF_RADIX  3
#define SELF_GATHER (SELF_RADIX + 2 * MVFX_SORT_PASSES)

// A substep's passes from its first: the unit pass's first half, the
// part, the commit, the unit pass's rest.
#define SUBSTEP_MOVE   0
#define SUBSTEP_PART   1
#define SUBSTEP_COMMIT 2
#define SUBSTEP_REST   3

// What the unit pass's dispatches run (sim/units.wgsl's UNITS_*).
#define UNITS_MOVE 1u
#define UNITS_REST 2u

// The self module's bindings: the numbers, the particles, the
// statistics, the scratch words, the counts, the keys and indices, the
// digit counts.
#define SELF_BINDINGS 7

static_assert(SELF_GATHER + 1 == SELF_FIRST_PASSES, "a pass for each self kernel");

// Creates a buffer of size bytes and usages; false when the device refuses.
static bool CreateBuffer(mvfxGpuSim* sim, uint64_t size, mrhiBufferUsage usage, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return mvfxSimMade(sim, mrhiCreateBuffer(sim->device, &def, out));
}

bool mvfxCreateSimSelf(mvfxGpuSim* sim, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    const mvfxEmitterView* view = &sim->views[e];
    emitter->selfCollides = emitter->units && view->unit.selfThickness > 0.0f;
    emitter->selfSubsteps = emitter->selfCollides ? view->unit.substeps : 0;
    if (!emitter->selfCollides)
    {
        return true;
    }
    // The start places, the moves, the counts and the candidates; keys and
    // indices twice over the sort's blocks; two sets of digit counts; the
    // counts.
    uint64_t c = (uint64_t)emitter->capacity;
    uint64_t blocks = mvfxSortBlocks(emitter->capacity);
    uint64_t scratch = 4u * (7u + (uint64_t)view->unit.selfContacts) * c;
    mrhiBufferId* b = emitter->selfBuffers;
    return CreateBuffer(sim, MVFX_SELF_WORDS * 4u, mrhi_bufferUniform | mrhi_bufferCopyDestination,
                        &b[SELF_NUMBERS]) &&
           CreateBuffer(sim, scratch, mrhi_bufferStorage, &b[SELF_SCRATCH]) &&
           CreateBuffer(sim, 4u * 4u * blocks * MVFX_SORT_BLOCK, mrhi_bufferStorage,
                        &b[SELF_SORT]) &&
           CreateBuffer(sim, 4u * 2u * 256u * blocks, mrhi_bufferStorage, &b[SELF_DIGITS]) &&
           CreateBuffer(sim, 4u * 4u, mrhi_bufferStorage | mrhi_bufferCopyDestination,
                        &b[SELF_COUNT]);
}

void mvfxDestroySimSelf(mvfxGpuSim* sim, Emitter* emitter)
{
    for (int32_t b = 0; b < SELF_BUFFERS; ++b)
    {
        if (emitter->selfBuffers[b].index1 != 0)
        {
            (void)mrhiDestroyBuffer(sim->device, emitter->selfBuffers[b]);
        }
    }
}

void mvfxSimSelfNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan)
{
    Emitter* emitter = &sim->emitters[e];
    if (!emitter->selfCollides)
    {
        return;
    }
    emitter->selfTicks = plan->ticks;
    mvfxSelfNumbers(&sim->views[e], plan, emitter->selfWords);
}

// Whether substep s of the step has ticks, as the core's: those that have
// none are skipped.
static bool HasTicks(const Emitter* emitter, int32_t s)
{
    int64_t ticks = emitter->selfTicks;
    int64_t substeps = emitter->selfSubsteps;
    return ticks * (s + 1) / substeps != ticks * s / substeps;
}

// Whether self pass p runs this step: those of the emitter's substeps,
// the part and commit of a substep with ticks.
static bool Runs(const Emitter* emitter, int32_t p)
{
    if (p < SELF_FIRST_PASSES)
    {
        return true;
    }
    int32_t s = (p - SELF_FIRST_PASSES) / 4;
    int32_t at = (p - SELF_FIRST_PASSES) % 4;
    return s < emitter->selfSubsteps &&
           (at == SUBSTEP_MOVE || at == SUBSTEP_REST || HasTicks(emitter, s));
}

// Adds a pass of these accesses.
static bool AddPass(mvfxGpuSim* sim, mrhiPassClass passClass, const mrhiAccess* accesses,
                    int32_t count, mrhiPassId* passOut)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = passClass;
    def.accesses = accesses;
    def.accessCount = count;
    // Its writes outlive the frame, read by the next step or the host.
    def.neverCull = true;
    return mrhiAddPass(sim->device, &def, passOut) == mrhi_success;
}

bool mvfxAddSimSelfPasses(mvfxGpuSim* sim, Emitter* emitter, const mrhiAccess* used, int32_t count)
{
    mrhiResourceId* c = emitter->selfResources;
    for (int32_t b = 0; b < SELF_BUFFERS; ++b)
    {
        if (mrhiImportBuffer(sim->device, emitter->selfBuffers[b], &c[b]) != mrhi_success)
        {
            return false;
        }
    }
    const mrhiResourceId* r = emitter->resources;
    const mrhiAccess written[2] = {
        {.resource = c[SELF_NUMBERS], .kind = mrhi_accessCopyDestination},
        {.resource = c[SELF_COUNT], .kind = mrhi_accessCopyDestination}};
    const mrhiAccess own[SELF_BINDINGS] = {
        {.resource = c[SELF_NUMBERS], .kind = mrhi_accessUniform},
        {.resource = r[BUFFER_CURRENT], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageReadWrite},
        {.resource = c[SELF_SCRATCH], .kind = mrhi_accessStorageReadWrite},
        {.resource = c[SELF_COUNT], .kind = mrhi_accessStorageReadWrite},
        {.resource = c[SELF_SORT], .kind = mrhi_accessStorageReadWrite},
        {.resource = c[SELF_DIGITS], .kind = mrhi_accessStorageReadWrite}};
    const mrhiAccess sorted[3] = {
        {.resource = c[SELF_COUNT], .kind = mrhi_accessStorageRead},
        {.resource = c[SELF_SORT], .kind = mrhi_accessStorageReadWrite},
        {.resource = c[SELF_DIGITS], .kind = mrhi_accessStorageReadWrite}};
    bool added = AddPass(sim, mrhi_passTransfer, written, 2, &emitter->selfPasses[SELF_WRITE]);
    for (int32_t p = SELF_REACH; added && p < SELF_PASSES; ++p)
    {
        int32_t at = (p - SELF_FIRST_PASSES) % 4;
        bool radix = p >= SELF_RADIX && p < SELF_GATHER;
        bool units = p >= SELF_FIRST_PASSES && (at == SUBSTEP_MOVE || at == SUBSTEP_REST);
        const mrhiAccess* accesses = radix ? sorted : (units ? used : own);
        int32_t n = radix ? 3 : (units ? count : SELF_BINDINGS);
        added = !Runs(emitter, p) ||
                AddPass(sim, mrhi_passGraphics, accesses, n, &emitter->selfPasses[p]);
    }
    return added;
}

// Records the write of the self numbers and the counts emptied, every
// step.
static bool RecordWrite(mvfxGpuSim* sim, Emitter* emitter)
{
    mrhiPassId pass = emitter->selfPasses[SELF_WRITE];
    const mrhiResourceId* c = emitter->selfResources;
    bool recorded =
        mrhiBeginPass(sim->device, pass) == mrhi_success &&
        mvfxUpload(sim->device, &sim->uploaded, pass, c[SELF_NUMBERS], 0, emitter->selfWords,
                   sizeof emitter->selfWords) &&
        mrhiClearBuffer(sim->device, pass, c[SELF_COUNT], 0, MRHI_WHOLE_SIZE) == mrhi_success;
    return mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
}

// Records a kernel's pass: its pipeline, bindings, root block and
// workgroups.
static bool RecordKernel(mvfxGpuSim* sim, mrhiPassId pass, mvfxKernel kernel,
                         const mrhiResourceId* bound, int32_t count, const uint32_t root[4],
                         uint32_t groups)
{
    mrhiBinding bindings[SELF_BINDINGS];
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

// Records the sort's radix passes over the self keys: each pass's
// offsets in one workgroup, then its scatter over the blocks.
static bool RecordRadix(mvfxGpuSim* sim, const Emitter* emitter)
{
    const mrhiResourceId* c = emitter->selfResources;
    const mrhiResourceId bound[3] = {c[SELF_COUNT], c[SELF_SORT], c[SELF_DIGITS]};
    uint32_t groups = emitter->selfWords[2];
    bool recorded = true;
    for (uint32_t r = 0; recorded && r < MVFX_SORT_PASSES; ++r)
    {
        const uint32_t root[4] = {8u * r, (uint32_t)emitter->capacity, groups, 0};
        const mrhiPassId* passes = emitter->selfPasses + SELF_RADIX + 2 * r;
        recorded = RecordKernel(sim, passes[0], mvfx_kernelSortOffsets, bound, 3, root, 1) &&
                   RecordKernel(sim, passes[1], mvfx_kernelSortScatter, bound, 3, root, groups);
    }
    return recorded;
}

// Records substep s's passes: the unit pass's halves about the part and
// commit (those only when it has ticks).
static bool RecordSubstep(mvfxGpuSim* sim, const Emitter* emitter, const mrhiResourceId* bound,
                          int32_t s)
{
    const mrhiPassId* passes = emitter->selfPasses + SELF_FIRST_PASSES + 4 * s;
    uint32_t groups = emitter->selfWords[3];
    const uint32_t move[4] = {UNITS_MOVE, (uint32_t)s, 0, 0};
    const uint32_t rest[4] = {UNITS_REST, (uint32_t)s, 0, 0};
    const uint32_t root[4] = {(uint32_t)s, 0, 0, 0};
    bool ticks = HasTicks(emitter, s);
    return mvfxRecordSimDispatch(sim, emitter, passes[SUBSTEP_MOVE], mvfx_kernelUnits,
                                 emitter->unitGroups, move) &&
           (!ticks || (RecordKernel(sim, passes[SUBSTEP_PART], mvfx_kernelSelfPart, bound,
                                    SELF_BINDINGS, root, groups) &&
                       RecordKernel(sim, passes[SUBSTEP_COMMIT], mvfx_kernelSelfCommit, bound,
                                    SELF_BINDINGS, root, groups))) &&
           mvfxRecordSimDispatch(sim, emitter, passes[SUBSTEP_REST], mvfx_kernelUnits,
                                 emitter->unitGroups, rest);
}

bool mvfxRecordSimSelf(mvfxGpuSim* sim, Emitter* emitter)
{
    const mrhiResourceId* c = emitter->selfResources;
    const mrhiResourceId* r = emitter->resources;
    const mrhiResourceId bound[SELF_BINDINGS] = {
        c[SELF_NUMBERS], r[BUFFER_CURRENT], r[BUFFER_STATISTICS], c[SELF_SCRATCH],
        c[SELF_COUNT],   c[SELF_SORT],      c[SELF_DIGITS]};
    const mrhiPassId* passes = emitter->selfPasses;
    uint32_t blocks = emitter->selfWords[2];
    uint32_t groups = emitter->selfWords[3];
    static const uint32_t first[4] = {0};
    bool recorded = RecordWrite(sim, emitter) &&
                    RecordKernel(sim, passes[SELF_REACH], mvfx_kernelSelfReach, bound,
                                 SELF_BINDINGS, first, groups) &&
                    RecordKernel(sim, passes[SELF_KEYS], mvfx_kernelSelfKeys, bound, SELF_BINDINGS,
                                 first, blocks) &&
                    RecordRadix(sim, emitter) &&
                    RecordKernel(sim, passes[SELF_GATHER], mvfx_kernelSelfGather, bound,
                                 SELF_BINDINGS, first, groups);
    for (int32_t s = 0; recorded && s < emitter->selfSubsteps; ++s)
    {
        recorded = RecordSubstep(sim, emitter, bound, s);
    }
    return recorded;
}
