// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's plexus links on the GPU (plexus.wgsl), after its bounds:
// its numbers written at the first step, its keys, the sort's four radix
// passes over them (sort.wgsl), each particle's counts, their slots and
// the links.

#include "kernels.h"
#include "numbers.h"
#include "records.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

// The plexus passes: the numbers' write, the keys, the radix passes from
// the third, then the counts, the slots and the links.
#define PLEXUS_WRITE 0
#define PLEXUS_KEYS  1
#define PLEXUS_RADIX 2
#define PLEXUS_COUNT (PLEXUS_RADIX + 2 * MVFX_SORT_PASSES)
#define PLEXUS_SLOTS (PLEXUS_COUNT + 1)
#define PLEXUS_STORE (PLEXUS_COUNT + 2)

static_assert(PLEXUS_STORE + 1 == PLEXUS_PASSES, "a pass for each plexus kernel");

// The plexus module's bindings: the particles, the statistics, the keys
// and indices, the digit counts, the counts and slots, the links, the
// numbers.
#define PLEXUS_BINDINGS 7

// Creates a buffer of size bytes and usages; false when the device refuses.
static bool CreateBuffer(mvfxGpuSim* sim, uint64_t size, mrhiBufferUsage usage, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return mrhiCreateBuffer(sim->device, &def, out) == mrhi_success;
}

bool mvfxCreateSimPlexus(mvfxGpuSim* sim, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    const mvfxPlexusDef* def = &sim->views[e].plexus;
    emitter->plexus = def->enabled;
    if (!def->enabled)
    {
        return true;
    }
    // Keys and indices twice over the sort's blocks; two sets of digit
    // counts; found, owned and first slot a particle; two indices a link.
    uint64_t c = (uint64_t)emitter->capacity;
    uint64_t blocks = mvfxSortBlocks(emitter->capacity);
    mrhiBufferId* b = emitter->plexusBuffers;
    return CreateBuffer(sim, PLEXUS_WORDS * 4u, mrhi_bufferUniform | mrhi_bufferCopyDestination,
                        &b[PLEXUS_NUMBERS]) &&
           CreateBuffer(sim, 4u * 4u * blocks * MVFX_SORT_BLOCK, mrhi_bufferStorage,
                        &b[PLEXUS_SORT]) &&
           CreateBuffer(sim, 4u * 2u * 256u * blocks, mrhi_bufferStorage, &b[PLEXUS_DIGITS]) &&
           CreateBuffer(sim, 4u * 3u * c, mrhi_bufferStorage, &b[PLEXUS_COUNTS]) &&
           CreateBuffer(sim, 8u * (uint64_t)def->maxLinks,
                        mrhi_bufferStorage | mrhi_bufferCopySource, &b[PLEXUS_LINKS]);
}

void mvfxDestroySimPlexus(mvfxGpuSim* sim, Emitter* emitter)
{
    for (int32_t b = 0; b < PLEXUS_BUFFERS; ++b)
    {
        if (emitter->plexusBuffers[b].index1 != 0)
        {
            (void)mrhiDestroyBuffer(sim->device, emitter->plexusBuffers[b]);
        }
    }
}

void mvfxSimPlexusNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan)
{
    Emitter* emitter = &sim->emitters[e];
    if (emitter->plexus)
    {
        mvfxPlexusNumbers(&sim->views[e], plan, emitter->plexusWords);
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

bool mvfxAddSimPlexusPasses(mvfxGpuSim* sim, Emitter* emitter)
{
    if (!emitter->plexus)
    {
        return true;
    }
    mrhiResourceId* x = emitter->plexusResources;
    for (int32_t b = 0; b < PLEXUS_BUFFERS; ++b)
    {
        if (mrhiImportBuffer(sim->device, emitter->plexusBuffers[b], &x[b]) != mrhi_success)
        {
            return false;
        }
    }
    const mrhiResourceId* r = emitter->resources;
    const mrhiAccess written = {.resource = x[PLEXUS_NUMBERS], .kind = mrhi_accessCopyDestination};
    const mrhiAccess used[PLEXUS_BINDINGS] = {
        {.resource = r[BUFFER_OTHER], .kind = mrhi_accessStorageRead},
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageReadWrite},
        {.resource = x[PLEXUS_SORT], .kind = mrhi_accessStorageReadWrite},
        {.resource = x[PLEXUS_DIGITS], .kind = mrhi_accessStorageReadWrite},
        {.resource = x[PLEXUS_COUNTS], .kind = mrhi_accessStorageReadWrite},
        {.resource = x[PLEXUS_LINKS], .kind = mrhi_accessStorageReadWrite},
        {.resource = x[PLEXUS_NUMBERS], .kind = mrhi_accessUniform}};
    const mrhiAccess sorted[3] = {
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageRead},
        {.resource = x[PLEXUS_SORT], .kind = mrhi_accessStorageReadWrite},
        {.resource = x[PLEXUS_DIGITS], .kind = mrhi_accessStorageReadWrite}};
    // The numbers are the same every step: written once.
    emitter->plexusWriting = !emitter->plexusWritten;
    bool added = !emitter->plexusWriting ||
                 AddPass(sim, mrhi_passTransfer, &written, 1, &emitter->plexusPasses[PLEXUS_WRITE]);
    for (int32_t p = PLEXUS_KEYS; added && p < PLEXUS_PASSES; ++p)
    {
        bool radix = p >= PLEXUS_RADIX && p < PLEXUS_COUNT;
        added = AddPass(sim, mrhi_passGraphics, radix ? sorted : used, radix ? 3 : PLEXUS_BINDINGS,
                        &emitter->plexusPasses[p]);
    }
    return added;
}

// Records the write of the plexus numbers, at the first step.
static bool RecordWrite(mvfxGpuSim* sim, Emitter* emitter)
{
    if (!emitter->plexusWriting)
    {
        return true;
    }
    mrhiPassId pass = emitter->plexusPasses[PLEXUS_WRITE];
    bool recorded =
        mrhiBeginPass(sim->device, pass) == mrhi_success &&
        mvfxUpload(sim->device, &sim->uploaded, pass, emitter->plexusResources[PLEXUS_NUMBERS], 0,
                   emitter->plexusWords, sizeof emitter->plexusWords);
    recorded = mrhiEndPass(sim->device, pass) == mrhi_success && recorded;
    emitter->plexusWritten = recorded;
    return recorded;
}

// Records a kernel's pass: its pipeline, bindings and workgroups.
static bool RecordKernel(mvfxGpuSim* sim, mrhiPassId pass, mvfxKernel kernel,
                         const mrhiResourceId* bound, int32_t count, const uint32_t* root,
                         uint32_t groups)
{
    mrhiBinding bindings[PLEXUS_BINDINGS];
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

// Records the sort's radix passes over the plexus keys: each pass's
// offsets in one workgroup, then its scatter over the blocks.
static bool RecordRadix(mvfxGpuSim* sim, const Emitter* emitter)
{
    const mrhiResourceId* x = emitter->plexusResources;
    const mrhiResourceId bound[3] = {emitter->resources[BUFFER_STATISTICS], x[PLEXUS_SORT],
                                     x[PLEXUS_DIGITS]};
    uint32_t groups = emitter->plexusWords[3];
    bool recorded = true;
    for (uint32_t r = 0; recorded && r < MVFX_SORT_PASSES; ++r)
    {
        const uint32_t root[4] = {8u * r, (uint32_t)emitter->capacity, groups, 0};
        const mrhiPassId* passes = emitter->plexusPasses + PLEXUS_RADIX + 2 * r;
        recorded = RecordKernel(sim, passes[0], mvfx_kernelSortOffsets, bound, 3, root, 1) &&
                   RecordKernel(sim, passes[1], mvfx_kernelSortScatter, bound, 3, root, groups);
    }
    return recorded;
}

bool mvfxRecordSimPlexus(mvfxGpuSim* sim, Emitter* emitter)
{
    if (!emitter->plexus)
    {
        return true;
    }
    const mrhiResourceId* x = emitter->plexusResources;
    const mrhiResourceId* r = emitter->resources;
    const mrhiResourceId bound[PLEXUS_BINDINGS] = {
        r[BUFFER_OTHER],  r[BUFFER_STATISTICS], x[PLEXUS_SORT],   x[PLEXUS_DIGITS],
        x[PLEXUS_COUNTS], x[PLEXUS_LINKS],      x[PLEXUS_NUMBERS]};
    const mrhiPassId* passes = emitter->plexusPasses;
    uint32_t blocks = emitter->plexusWords[3];
    uint32_t groups = emitter->plexusWords[6];
    return RecordWrite(sim, emitter) &&
           RecordKernel(sim, passes[PLEXUS_KEYS], mvfx_kernelPlexusKeys, bound, PLEXUS_BINDINGS,
                        nullptr, blocks) &&
           RecordRadix(sim, emitter) &&
           RecordKernel(sim, passes[PLEXUS_COUNT], mvfx_kernelPlexusCount, bound, PLEXUS_BINDINGS,
                        nullptr, groups) &&
           RecordKernel(sim, passes[PLEXUS_SLOTS], mvfx_kernelPlexusOffsets, bound, PLEXUS_BINDINGS,
                        nullptr, 1) &&
           RecordKernel(sim, passes[PLEXUS_STORE], mvfx_kernelPlexusWrite, bound, PLEXUS_BINDINGS,
                        nullptr, groups);
}
