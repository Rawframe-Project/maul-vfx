// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's neighbor grid on the GPU (grid.wgsl), after its room pass
// and before its update: its numbers written, its keys, the sort's four
// radix passes over them (sort.wgsl), its values, and the query giving
// its particles their neighbor attributes from its source's grid.

#include "kernels.h"
#include "numbers.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

// The grid passes: the numbers' write, the keys, the radix passes from
// the third, the values and the query last.
#define GRID_WRITE     0
#define GRID_KEYS      1
#define GRID_RADIX     2
#define GRID_VALUES    (GRID_PASSES - 2)
#define GRID_NEIGHBORS (GRID_PASSES - 1)

// The grid module's bindings: the buffers of GRID_BUFFERS' order with the
// particles and statistics after the numbers, then the step's tables.
#define GRID_BINDINGS (GRID_BUFFERS + 3)

static_assert(GRID_RADIX + 2 * MVFX_SORT_PASSES == GRID_VALUES, "a pass for each radix kernel");

// Creates a buffer of size bytes and usages; false when the device refuses.
static bool CreateBuffer(mvfxGpuSim* sim, uint64_t size, mrhiBufferUsage usage, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return mvfxSimMade(sim, mrhiCreateBuffer(sim->device, &def, out));
}

static void DestroyBuffer(mvfxGpuSim* sim, mrhiBufferId buffer)
{
    if (buffer.index1 != 0)
    {
        (void)mrhiDestroyBuffer(sim->device, buffer);
    }
}

bool mvfxCreateSimGrid(mvfxGpuSim* sim, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    const mvfxEmitterView* view = &sim->views[e];
    emitter->gridSource = view->neighbors.enabled ? view->neighbors.source : -1;
    emitter->gridFlags = mvfxGridFlags(view, emitter->gridSource == e);
    if (emitter->gridFlags == 0)
    {
        return true;
    }
    bool made =
        CreateBuffer(sim, MVFX_GRID_WORDS * 4u, mrhi_bufferUniform | mrhi_bufferCopyDestination,
                     &emitter->gridNumbers);
    if ((emitter->gridFlags & MVFX_GRID_BUILDS) == 0)
    {
        return made;
    }
    // Keys and indices twice over the sort's blocks, then six arrays of
    // values; two sets of digit counts; the count, emptied at the first
    // step.
    uint64_t c = (uint64_t)emitter->capacity;
    uint64_t blocks = mvfxSortBlocks(emitter->capacity);
    uint64_t span = blocks * MVFX_SORT_BLOCK;
    return made &&
           CreateBuffer(sim, 4u * (4u * span + 6u * c), mrhi_bufferStorage, &emitter->gridWords) &&
           CreateBuffer(sim, 4u * 2u * 256u * blocks, mrhi_bufferStorage, &emitter->gridDigits) &&
           CreateBuffer(sim, 4u, mrhi_bufferStorage | mrhi_bufferCopyDestination,
                        &emitter->gridCount);
}

void mvfxDestroySimGrid(mvfxGpuSim* sim, Emitter* emitter)
{
    DestroyBuffer(sim, emitter->gridNumbers);
    DestroyBuffer(sim, emitter->gridWords);
    DestroyBuffer(sim, emitter->gridDigits);
    DestroyBuffer(sim, emitter->gridCount);
}

void mvfxSimGridNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan)
{
    Emitter* emitter = &sim->emitters[e];
    if (emitter->gridFlags == 0)
    {
        return;
    }
    int32_t s = emitter->gridSource >= 0 ? emitter->gridSource : e;
    const Emitter* source = &sim->emitters[s];
    mvfxGridNumbers(&sim->views[e], &sim->views[s], plan, &emitter->tables, emitter->gridMask,
                    source->gridMask, source->capacity, emitter->gridSource == e,
                    emitter->gridNumberWords);
}

// The buffers the grid passes bind for an emitter: its numbers and its
// own grid's, or the simulation's for one that builds none; its source's
// words and count, or the simulation's (unread) for a source of its own
// or of no room.
static void GridBuffers(const mvfxGpuSim* sim, const Emitter* emitter,
                        mrhiBufferId buffers[GRID_BUFFERS])
{
    bool builds = (emitter->gridFlags & MVFX_GRID_BUILDS) != 0;
    buffers[GRID_NUMBERS] = emitter->gridNumbers;
    buffers[GRID_WORDS] = builds ? emitter->gridWords : sim->ownGrid[0];
    buffers[GRID_DIGITS] = builds ? emitter->gridDigits : sim->ownGrid[1];
    buffers[GRID_COUNT] = builds ? emitter->gridCount : sim->ownGrid[2];
    buffers[GRID_SOURCE_WORDS] = sim->ownSort;
    buffers[GRID_SOURCE_COUNT] = sim->ownDigits;
    const Emitter* source =
        emitter->gridSource >= 0 ? &sim->emitters[emitter->gridSource] : nullptr;
    if (source != nullptr && source != emitter && source->capacity > 0)
    {
        buffers[GRID_SOURCE_WORDS] = source->gridWords;
        buffers[GRID_SOURCE_COUNT] = source->gridCount;
    }
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

// Whether an emitter's grid pass p runs: the write always, the keys, the
// sort and the values when it builds, the query when it finds or steers.
static bool Runs(const Emitter* emitter, int32_t p)
{
    uint32_t needs = p == GRID_WRITE       ? emitter->gridFlags
                     : p == GRID_NEIGHBORS ? MVFX_GRID_FINDS | MVFX_GRID_STEERS
                                           : MVFX_GRID_BUILDS;
    return (emitter->gridFlags & needs) != 0;
}

bool mvfxAddSimGridPasses(mvfxGpuSim* sim, Emitter* emitter)
{
    if (emitter->gridFlags == 0)
    {
        return true;
    }
    // A buffer imported again has the same id: the source's are those of
    // its step, which ran first.
    mrhiBufferId buffers[GRID_BUFFERS];
    GridBuffers(sim, emitter, buffers);
    mrhiResourceId* g = emitter->gridResources;
    for (int32_t b = 0; b < GRID_BUFFERS; ++b)
    {
        if (mrhiImportBuffer(sim->device, buffers[b], &g[b]) != mrhi_success)
        {
            return false;
        }
    }
    const mrhiResourceId* r = emitter->resources;
    const mrhiAccess written[2] = {
        {.resource = g[GRID_NUMBERS], .kind = mrhi_accessCopyDestination},
        {.resource = g[GRID_COUNT], .kind = mrhi_accessCopyDestination}};
    const mrhiAccess used[GRID_BINDINGS] = {
        {.resource = g[GRID_NUMBERS], .kind = mrhi_accessUniform},
        {.resource = r[BUFFER_CURRENT], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageReadWrite},
        {.resource = g[GRID_WORDS], .kind = mrhi_accessStorageReadWrite},
        {.resource = g[GRID_DIGITS], .kind = mrhi_accessStorageReadWrite},
        {.resource = g[GRID_COUNT], .kind = mrhi_accessStorageReadWrite},
        {.resource = g[GRID_SOURCE_WORDS], .kind = mrhi_accessStorageRead},
        {.resource = g[GRID_SOURCE_COUNT], .kind = mrhi_accessStorageRead},
        {.resource = r[BUFFER_GRIDS], .kind = mrhi_accessStorageRead}};
    const mrhiAccess sorted[3] = {
        {.resource = g[GRID_COUNT], .kind = mrhi_accessStorageRead},
        {.resource = g[GRID_WORDS], .kind = mrhi_accessStorageReadWrite},
        {.resource = g[GRID_DIGITS], .kind = mrhi_accessStorageReadWrite}};
    bool builds = (emitter->gridFlags & MVFX_GRID_BUILDS) != 0;
    bool added =
        AddPass(sim, mrhi_passTransfer, written, builds ? 2 : 1, &emitter->gridPasses[GRID_WRITE]);
    for (int32_t p = GRID_KEYS; added && p < GRID_PASSES; ++p)
    {
        bool radix = p >= GRID_RADIX && p < GRID_VALUES;
        added = !Runs(emitter, p) || AddPass(sim, mrhi_passGraphics, radix ? sorted : used,
                                             radix ? 3 : GRID_BINDINGS, &emitter->gridPasses[p]);
    }
    return added;
}

// The words of an emitter's grid numbers a step after the first changes:
// the clock's and the shift's, and for one that steers on to its path's
// last point.
static uint32_t StepWords(const Emitter* emitter)
{
    if ((emitter->gridFlags & MVFX_GRID_STEERS) == 0)
    {
        return MVFX_GRID_STEER - MVFX_GRID_CLOCK;
    }
    uint32_t points = emitter->gridNumberWords[MVFX_GRID_STEER + 1];
    return MVFX_GRID_STEER + 32u + 4u * points - MVFX_GRID_CLOCK;
}

// Records the write of the grid's numbers: all of them at the first step,
// those a step changes after; its count emptied at the first.
static bool RecordWrite(mvfxGpuSim* sim, Emitter* emitter)
{
    mrhiPassId pass = emitter->gridPasses[GRID_WRITE];
    const mrhiResourceId* g = emitter->gridResources;
    uint32_t first = emitter->gridNumbersWritten ? MVFX_GRID_CLOCK : 0u;
    uint32_t words = emitter->gridNumbersWritten ? StepWords(emitter) : MVFX_GRID_WORDS;
    bool empties = (emitter->gridFlags & MVFX_GRID_BUILDS) != 0 && !emitter->gridEmptied;
    bool recorded =
        mrhiBeginPass(sim->device, pass) == mrhi_success &&
        mvfxUpload(sim->device, &sim->uploaded, pass, g[GRID_NUMBERS], (uint64_t)first * 4u,
                   emitter->gridNumberWords + first, (uint64_t)words * 4u) &&
        (!empties ||
         mrhiClearBuffer(sim->device, pass, g[GRID_COUNT], 0, MRHI_WHOLE_SIZE) == mrhi_success);
    recorded = mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
    emitter->gridNumbersWritten = emitter->gridNumbersWritten || recorded;
    emitter->gridEmptied = emitter->gridEmptied || (recorded && empties);
    return recorded;
}

// Records a kernel's pass: its pipeline, bindings, root block (none for
// NULL) and workgroups.
static bool RecordKernel(mvfxGpuSim* sim, mrhiPassId pass, mvfxKernel kernel,
                         const mrhiResourceId* bound, int32_t count, const uint32_t* root,
                         uint32_t groups)
{
    mrhiBinding bindings[GRID_BINDINGS];
    for (int32_t b = 0; b < count; ++b)
    {
        bindings[b] =
            (mrhiBinding){.slot = (uint32_t)b, .resource = bound[b], .size = MRHI_WHOLE_SIZE};
    }
    bool ran = mrhiBeginPass(sim->device, pass) == mrhi_success &&
               mrhiSetComputePipeline(sim->device, pass, mvfxGpuPipeline(sim->gpu, kernel)) ==
                   mrhi_success &&
               mrhiSetBindings(sim->device, pass, 0, bindings, (uint32_t)count) == mrhi_success &&
               (root == nullptr || mrhiSetRootBlock(sim->device, pass, 0, root,
                                                    4 * sizeof(uint32_t)) == mrhi_success) &&
               mrhiDispatch(sim->device, pass, groups, 1, 1) == mrhi_success;
    return mrhiEndPass(sim->device, pass) == mrhi_success && ran;
}

// Records the sort's radix passes over the grid's keys: each pass's
// offsets in one workgroup, then its scatter over the blocks.
static bool RecordRadix(mvfxGpuSim* sim, const Emitter* emitter)
{
    const mrhiResourceId* g = emitter->gridResources;
    const mrhiResourceId bound[3] = {g[GRID_COUNT], g[GRID_WORDS], g[GRID_DIGITS]};
    uint32_t groups = emitter->gridNumberWords[2];
    bool recorded = true;
    for (uint32_t r = 0; recorded && r < MVFX_SORT_PASSES; ++r)
    {
        const uint32_t root[4] = {8u * r, (uint32_t)emitter->capacity, groups, 0};
        const mrhiPassId* passes = emitter->gridPasses + GRID_RADIX + 2 * r;
        recorded = RecordKernel(sim, passes[0], mvfx_kernelSortOffsets, bound, 3, root, 1) &&
                   RecordKernel(sim, passes[1], mvfx_kernelSortScatter, bound, 3, root, groups);
    }
    return recorded;
}

bool mvfxRecordSimGrid(mvfxGpuSim* sim, Emitter* emitter)
{
    if (emitter->gridFlags == 0)
    {
        return true;
    }
    const mrhiResourceId* g = emitter->gridResources;
    const mrhiResourceId* r = emitter->resources;
    const mrhiResourceId bound[GRID_BINDINGS] = {
        g[GRID_NUMBERS], r[BUFFER_CURRENT],    r[BUFFER_STATISTICS], g[GRID_WORDS],  g[GRID_DIGITS],
        g[GRID_COUNT],   g[GRID_SOURCE_WORDS], g[GRID_SOURCE_COUNT], r[BUFFER_GRIDS]};
    const uint32_t* words = emitter->gridNumberWords;
    const mrhiPassId* passes = emitter->gridPasses;
    int32_t count = GRID_BINDINGS;
    bool recorded = RecordWrite(sim, emitter);
    if (recorded && Runs(emitter, GRID_KEYS))
    {
        recorded = RecordKernel(sim, passes[GRID_KEYS], mvfx_kernelGridKeys, bound, count, nullptr,
                                words[2]) &&
                   RecordRadix(sim, emitter) &&
                   RecordKernel(sim, passes[GRID_VALUES], mvfx_kernelGridValues, bound, count,
                                nullptr, words[3]);
    }
    return recorded && (!Runs(emitter, GRID_NEIGHBORS) ||
                        RecordKernel(sim, passes[GRID_NEIGHBORS], mvfx_kernelNeighbors, bound,
                                     count, nullptr, words[3]));
}
