// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's particle contacts on the GPU (contacts.wgsl), after its
// scatter and before its bounds: its numbers written, its radii, its
// keys, the sort's four radix passes over them (sort.wgsl), a projection
// and its commit for each iteration, its impulses and its kick.

#include "kernels.h"
#include "numbers.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

// The contact passes: the numbers' write, the radii, the keys, the radix
// passes from the fourth, then a projection and its commit for each
// iteration, the impulses and the kick last.
#define CONTACT_WRITE   0
#define CONTACT_RADII   1
#define CONTACT_KEYS    2
#define CONTACT_RADIX   3
#define CONTACT_PROJECT (CONTACT_RADIX + 2 * MVFX_SORT_PASSES)

// The contacts module's bindings: the record numbers, the particles, the
// statistics, the grids, the scratch words, the counts, the keys and
// indices, the digit counts, the contact numbers.
#define CONTACT_BINDINGS 9

static_assert(CONTACT_PROJECT + 2 * MVFX_MAX_CONTACT_ITERATIONS + 2 == CONTACT_PASSES,
              "a pass for each contact kernel");

// Creates a buffer of size bytes and usages; false when the device refuses.
static bool CreateBuffer(mvfxGpuSim* sim, uint64_t size, mrhiBufferUsage usage, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return mvfxSimMade(sim, mrhiCreateBuffer(sim->device, &def, out));
}

bool mvfxCreateSimContacts(mvfxGpuSim* sim, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    const mvfxContactDef* def = &sim->views[e].contacts;
    emitter->contacts = def->enable;
    emitter->contactIterations = def->enable ? def->iterations : 0;
    if (!def->enable)
    {
        return true;
    }
    // The places as built, the scratch, the radii; keys and indices twice
    // over the sort's blocks; two sets of digit counts; the counts.
    uint64_t c = (uint64_t)emitter->capacity;
    uint64_t blocks = mvfxSortBlocks(emitter->capacity);
    mrhiBufferUsage numbers = mrhi_bufferUniform | mrhi_bufferCopyDestination;
    mrhiBufferId* b = emitter->contactBuffers;
    return CreateBuffer(sim, MVFX_RECORD_WORDS * 4u, numbers, &b[CONTACT_RECORD]) &&
           CreateBuffer(sim, CONTACT_WORDS * 4u, numbers, &b[CONTACT_NUMBERS]) &&
           CreateBuffer(sim, 4u * 7u * c, mrhi_bufferStorage, &b[CONTACT_SCRATCH]) &&
           CreateBuffer(sim, 4u * 4u * blocks * MVFX_SORT_BLOCK, mrhi_bufferStorage,
                        &b[CONTACT_SORT]) &&
           CreateBuffer(sim, 4u * 2u * 256u * blocks, mrhi_bufferStorage, &b[CONTACT_DIGITS]) &&
           CreateBuffer(sim, 4u * 4u, mrhi_bufferStorage | mrhi_bufferCopyDestination,
                        &b[CONTACT_COUNT]);
}

void mvfxDestroySimContacts(mvfxGpuSim* sim, Emitter* emitter)
{
    for (int32_t b = 0; b < CONTACT_BUFFERS; ++b)
    {
        if (emitter->contactBuffers[b].index1 != 0)
        {
            (void)mrhiDestroyBuffer(sim->device, emitter->contactBuffers[b]);
        }
    }
}

void mvfxSimContactNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan)
{
    Emitter* emitter = &sim->emitters[e];
    if (!emitter->contacts)
    {
        return;
    }
    // A records pass's numbers that writes no field: the radii read its
    // sizes over life at the step's end.
    static const mvfxLayoutView none = {0};
    mvfxGpuRecordsDef def = mvfxDefaultGpuRecordsDef();
    def.emitter = e;
    mvfxRecordNumbers(&sim->views[e], plan, &none, &def, &emitter->tables, sim->srgb,
                      emitter->contactRecordWords);
    mvfxContactNumbers(&sim->views[e], plan, emitter->contactWords);
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

// Whether contact pass p runs for an emitter of these iterations.
static bool Runs(const Emitter* emitter, int32_t p)
{
    int32_t projections = CONTACT_PROJECT + 2 * emitter->contactIterations;
    return p < projections || p >= CONTACT_PROJECT + 2 * MVFX_MAX_CONTACT_ITERATIONS;
}

bool mvfxAddSimContactPasses(mvfxGpuSim* sim, Emitter* emitter)
{
    if (!emitter->contacts)
    {
        return true;
    }
    mrhiResourceId* c = emitter->contactResources;
    for (int32_t b = 0; b < CONTACT_BUFFERS; ++b)
    {
        if (mrhiImportBuffer(sim->device, emitter->contactBuffers[b], &c[b]) != mrhi_success)
        {
            return false;
        }
    }
    const mrhiResourceId* r = emitter->resources;
    const mrhiAccess written[3] = {
        {.resource = c[CONTACT_RECORD], .kind = mrhi_accessCopyDestination},
        {.resource = c[CONTACT_NUMBERS], .kind = mrhi_accessCopyDestination},
        {.resource = c[CONTACT_COUNT], .kind = mrhi_accessCopyDestination}};
    const mrhiAccess used[CONTACT_BINDINGS] = {
        {.resource = c[CONTACT_RECORD], .kind = mrhi_accessUniform},
        {.resource = r[BUFFER_OTHER], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_STATISTICS], .kind = mrhi_accessStorageReadWrite},
        {.resource = r[BUFFER_GRIDS], .kind = mrhi_accessStorageRead},
        {.resource = c[CONTACT_SCRATCH], .kind = mrhi_accessStorageReadWrite},
        {.resource = c[CONTACT_COUNT], .kind = mrhi_accessStorageReadWrite},
        {.resource = c[CONTACT_SORT], .kind = mrhi_accessStorageReadWrite},
        {.resource = c[CONTACT_DIGITS], .kind = mrhi_accessStorageReadWrite},
        {.resource = c[CONTACT_NUMBERS], .kind = mrhi_accessUniform}};
    const mrhiAccess sorted[3] = {
        {.resource = c[CONTACT_COUNT], .kind = mrhi_accessStorageRead},
        {.resource = c[CONTACT_SORT], .kind = mrhi_accessStorageReadWrite},
        {.resource = c[CONTACT_DIGITS], .kind = mrhi_accessStorageReadWrite}};
    bool added =
        AddPass(sim, mrhi_passTransfer, written, 3, &emitter->contactPasses[CONTACT_WRITE]);
    for (int32_t p = CONTACT_RADII; added && p < CONTACT_PASSES; ++p)
    {
        bool radix = p >= CONTACT_RADIX && p < CONTACT_PROJECT;
        added =
            !Runs(emitter, p) || AddPass(sim, mrhi_passGraphics, radix ? sorted : used,
                                         radix ? 3 : CONTACT_BINDINGS, &emitter->contactPasses[p]);
    }
    return added;
}

// Records the write of the contact numbers: all of them at the first
// step, the record numbers' words a plan changes after; the counts
// emptied every step.
static bool RecordWrite(mvfxGpuSim* sim, Emitter* emitter)
{
    mrhiPassId pass = emitter->contactPasses[CONTACT_WRITE];
    const mrhiResourceId* c = emitter->contactResources;
    bool recorded = mrhiPass_Begin(sim->device, pass) == mrhi_success;
    if (recorded && !emitter->contactWritten)
    {
        recorded = mvfxUpload(sim->device, &sim->uploaded, pass, c[CONTACT_RECORD], 0,
                              emitter->contactRecordWords, sizeof emitter->contactRecordWords) &&
                   mvfxUpload(sim->device, &sim->uploaded, pass, c[CONTACT_NUMBERS], 0,
                              emitter->contactWords, sizeof emitter->contactWords);
        emitter->contactWritten = recorded;
    }
    else
    {
        static const uint32_t spans[2] = {0, MVFX_RECORD_MAP};
        for (int32_t s = 0; recorded && s < 2; ++s)
        {
            recorded =
                mvfxUpload(sim->device, &sim->uploaded, pass, c[CONTACT_RECORD], spans[s] * 4u,
                           emitter->contactRecordWords + spans[s], MVFX_RECORD_SPAN * 4u);
        }
    }
    recorded = recorded && mrhiPass_ClearBuffer(sim->device, pass, c[CONTACT_COUNT], 0,
                                                MRHI_WHOLE_SIZE) == mrhi_success;
    return mrhiPass_End(sim->device, pass) == mrhi_success && recorded;
}

// Records a kernel's pass: its pipeline, bindings, root block and
// workgroups.
static bool RecordKernel(mvfxGpuSim* sim, mrhiPassId pass, mvfxKernel kernel,
                         const mrhiResourceId* bound, int32_t count, const uint32_t root[4],
                         uint32_t groups)
{
    mrhiBinding bindings[CONTACT_BINDINGS];
    for (int32_t b = 0; b < count; ++b)
    {
        bindings[b] =
            (mrhiBinding){.slot = (uint32_t)b, .resource = bound[b], .size = MRHI_WHOLE_SIZE};
    }
    bool ran =
        mrhiPass_Begin(sim->device, pass) == mrhi_success &&
        mrhiPass_SetComputePipeline(sim->device, pass, mvfxGpuPipeline(sim->gpu, kernel)) ==
            mrhi_success &&
        mrhiPass_SetBindings(sim->device, pass, 0, bindings, (uint32_t)count) == mrhi_success &&
        mrhiPass_SetRootBlock(sim->device, pass, 0, root, 4 * sizeof(uint32_t)) == mrhi_success &&
        mrhiPass_Dispatch(sim->device, pass, groups, 1, 1) == mrhi_success;
    return mrhiPass_End(sim->device, pass) == mrhi_success && ran;
}

// Records the sort's radix passes over the contact keys: each pass's
// offsets in one workgroup, then its scatter over the blocks.
static bool RecordRadix(mvfxGpuSim* sim, const Emitter* emitter)
{
    const mrhiResourceId* c = emitter->contactResources;
    const mrhiResourceId bound[3] = {c[CONTACT_COUNT], c[CONTACT_SORT], c[CONTACT_DIGITS]};
    uint32_t groups = emitter->contactWords[2];
    bool recorded = true;
    for (uint32_t r = 0; recorded && r < MVFX_SORT_PASSES; ++r)
    {
        const uint32_t root[4] = {8u * r, (uint32_t)emitter->capacity, groups, 0};
        const mrhiPassId* passes = emitter->contactPasses + CONTACT_RADIX + 2 * r;
        recorded = RecordKernel(sim, passes[0], mvfx_kernelSortOffsets, bound, 3, root, 1) &&
                   RecordKernel(sim, passes[1], mvfx_kernelSortScatter, bound, 3, root, groups);
    }
    return recorded;
}

bool mvfxRecordSimContacts(mvfxGpuSim* sim, Emitter* emitter)
{
    if (!emitter->contacts)
    {
        return true;
    }
    const mrhiResourceId* c = emitter->contactResources;
    const mrhiResourceId* r = emitter->resources;
    const mrhiResourceId bound[CONTACT_BINDINGS] = {
        c[CONTACT_RECORD], r[BUFFER_OTHER],    r[BUFFER_STATISTICS],
        r[BUFFER_GRIDS],   c[CONTACT_SCRATCH], c[CONTACT_COUNT],
        c[CONTACT_SORT],   c[CONTACT_DIGITS],  c[CONTACT_NUMBERS]};
    const mrhiPassId* passes = emitter->contactPasses;
    uint32_t blocks = emitter->contactWords[2];
    uint32_t groups = emitter->contactWords[3];
    static const uint32_t first[4] = {0};
    bool recorded = RecordWrite(sim, emitter) &&
                    RecordKernel(sim, passes[CONTACT_RADII], mvfx_kernelContactRadii, bound,
                                 CONTACT_BINDINGS, first, groups) &&
                    RecordKernel(sim, passes[CONTACT_KEYS], mvfx_kernelContactKeys, bound,
                                 CONTACT_BINDINGS, first, blocks) &&
                    RecordRadix(sim, emitter);
    for (int32_t n = 0; recorded && n < emitter->contactIterations; ++n)
    {
        const uint32_t root[4] = {(uint32_t)n, 0, 0, 0};
        const mrhiPassId* pair = passes + CONTACT_PROJECT + 2 * n;
        recorded = RecordKernel(sim, pair[0], mvfx_kernelContactProject, bound, CONTACT_BINDINGS,
                                root, groups) &&
                   RecordKernel(sim, pair[1], mvfx_kernelContactCommit, bound, CONTACT_BINDINGS,
                                root, groups);
    }
    const mrhiPassId* last = passes + CONTACT_PASSES - 2;
    return recorded &&
           RecordKernel(sim, last[0], mvfx_kernelContactImpulses, bound, CONTACT_BINDINGS, first,
                        groups) &&
           RecordKernel(sim, last[1], mvfx_kernelContactKick, bound, CONTACT_BINDINGS, first,
                        groups);
}
