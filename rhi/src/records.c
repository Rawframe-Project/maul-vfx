// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "records.h"

#include "upload.h"

#include "maul-rhi/resources.h"

#include <string.h>

bool mvfxCreateRecordBuffers(mrhiDevice* device, mvfxRecordPasses* passes)
{
    bool made = true;
    for (int32_t p = 0; made && p < MVFX_GPU_RECORD_PASSES; ++p)
    {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = MVFX_RECORD_WORDS * sizeof(uint32_t);
        def.usage = mrhi_bufferUniform | mrhi_bufferCopyDestination;
        made = mrhiCreateBuffer(device, &def, &passes->numbers[p]) == mrhi_success;
    }
    return made;
}

void mvfxDestroyRecordBuffers(mrhiDevice* device, mvfxRecordPasses* passes)
{
    for (int32_t p = 0; p < MVFX_GPU_RECORD_PASSES; ++p)
    {
        if (passes->numbers[p].index1 != 0)
        {
            (void)mrhiDestroyBuffer(device, passes->numbers[p]);
        }
    }
}

// The bindings of the records module's entries: the numbers, the
// particles, the statistics, the grids, the records, the count, the sort's
// keys and indices and its digit counts.
#define RECORD_BINDINGS 8

// Adds a compute pass of these accesses.
static bool AddKernelPass(mrhiDevice* device, const mrhiAccess* accesses, int32_t count,
                          mrhiPassId* passOut)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passGraphics;
    def.accesses = accesses;
    def.accessCount = count;
    def.neverCull = true;
    return mrhiAddPass(device, &def, passOut) == mrhi_success;
}

// Adds a sort's radix passes, offsets and scatter each, over the
// statistics, the keys and indices and the digit counts.
static bool AddRadixPasses(mrhiDevice* device, mvfxRecordPass* pass)
{
    const mrhiAccess used[3] = {
        {.resource = pass->resources.statistics, .kind = mrhi_accessStorageRead},
        {.resource = pass->resources.sort, .kind = mrhi_accessStorageReadWrite},
        {.resource = pass->resources.digits, .kind = mrhi_accessStorageReadWrite}};
    bool added = true;
    for (int32_t r = 0; added && r < 2 * MVFX_SORT_PASSES; ++r)
    {
        added = AddKernelPass(device, used, 3, &pass->radix[r]);
    }
    return added;
}

bool mvfxAddRecordPass(mrhiDevice* device, mvfxRecordPasses* passes,
                       const mvfxRecordResources* resources, const uint32_t* words,
                       int64_t capacity)
{
    mvfxRecordPass* pass = &passes->passes[passes->count];
    *pass = (mvfxRecordPass){.resources = *resources};
    memcpy(pass->words, words, sizeof pass->words);
    pass->sorted = words[MVFX_RECORD_ORDER] == MVFX_ORDER_SORTED;
    // One invocation a record, 64 a workgroup, at most a dispatch's.
    int64_t groups = (capacity + 63) / 64;
    pass->groups = (uint32_t)(groups < 65535 ? groups : 65535);
    pass->words[5] = pass->groups > 0 ? pass->groups : 1;
    if (mrhiImportBuffer(device, passes->numbers[passes->count], &pass->numbers) != mrhi_success)
    {
        return false;
    }
    const mrhiAccess written[1] = {{.resource = pass->numbers, .kind = mrhi_accessCopyDestination}};
    const mrhiAccess used[RECORD_BINDINGS] = {
        {.resource = pass->numbers, .kind = mrhi_accessUniform},
        {.resource = resources->particles, .kind = mrhi_accessStorageRead},
        {.resource = resources->statistics, .kind = mrhi_accessStorageRead},
        {.resource = resources->grids, .kind = mrhi_accessStorageRead},
        {.resource = resources->records, .kind = mrhi_accessStorageReadWrite},
        {.resource = resources->count, .kind = mrhi_accessStorageReadWrite},
        {.resource = resources->sort, .kind = mrhi_accessStorageReadWrite},
        {.resource = resources->digits, .kind = mrhi_accessStorageReadWrite}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = written;
    def.accessCount = 1;
    def.neverCull = true;
    bool added = mrhiAddPass(device, &def, &pass->write) == mrhi_success;
    if (pass->sorted)
    {
        added = added && AddKernelPass(device, used, RECORD_BINDINGS, &pass->keys) &&
                AddRadixPasses(device, pass);
    }
    added = added && AddKernelPass(device, used, RECORD_BINDINGS, &pass->kernel);
    passes->count += added ? 1 : 0;
    return added;
}

bool mvfxAddEmptyRecordPass(mrhiDevice* device, mvfxRecordPasses* passes, mrhiResourceId count,
                            uint32_t word)
{
    mvfxRecordPass* pass = &passes->passes[passes->count];
    *pass = (mvfxRecordPass){.empty = true, .resources = {.count = count}};
    pass->words[0] = word;
    const mrhiAccess written[1] = {{.resource = count, .kind = mrhi_accessCopyDestination}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = written;
    def.accessCount = 1;
    def.neverCull = true;
    bool added = mrhiAddPass(device, &def, &pass->write) == mrhi_success;
    passes->count += added ? 1 : 0;
    return added;
}

// Records an empty pass's count of 0.
static bool RecordEmpty(mrhiDevice* device, uint64_t* uploaded, const mvfxRecordPass* pass)
{
    static const uint32_t zero = 0;
    bool recorded = mrhiBeginPass(device, pass->write) == mrhi_success &&
                    mvfxUpload(device, uploaded, pass->write, pass->resources.count,
                               (uint64_t)pass->words[0] * 4u, &zero, sizeof zero);
    return mrhiEndPass(device, pass->write) == mrhi_success && recorded;
}

// Records a kernel's pass: its pipeline, bindings, root block (none for
// NULL) and workgroups.
static bool RecordKernel(mrhiDevice* device, mrhiPassId pass, mrhiComputePipelineId pipeline,
                         const mrhiResourceId* bound, int32_t count, const uint32_t* root,
                         uint32_t groups)
{
    mrhiBinding bindings[RECORD_BINDINGS];
    for (int32_t b = 0; b < count; ++b)
    {
        bindings[b] =
            (mrhiBinding){.slot = (uint32_t)b, .resource = bound[b], .size = MRHI_WHOLE_SIZE};
    }
    bool ran = mrhiBeginPass(device, pass) == mrhi_success &&
               mrhiSetComputePipeline(device, pass, pipeline) == mrhi_success &&
               mrhiSetBindings(device, pass, 0, bindings, (uint32_t)count) == mrhi_success &&
               (root == nullptr ||
                mrhiSetRootBlock(device, pass, 0, root, 4 * sizeof(uint32_t)) == mrhi_success) &&
               mrhiDispatch(device, pass, groups, 1, 1) == mrhi_success;
    return mrhiEndPass(device, pass) == mrhi_success && ran;
}

// The workgroups of a sort's keys and scatter kernels, striding over its
// blocks.
static uint32_t SortGroups(const mvfxRecordPass* pass)
{
    uint32_t groups = pass->words[MVFX_RECORD_ORDER + 3];
    return groups > 0 ? groups : 1;
}

// Records a sort's radix passes: each pass's offsets in one workgroup,
// then its scatter over the blocks.
static bool RecordRadix(mrhiDevice* device, const mvfxRecordPipelines* pipelines,
                        const mvfxRecordPass* pass)
{
    const mrhiResourceId bound[3] = {pass->resources.statistics, pass->resources.sort,
                                     pass->resources.digits};
    uint32_t groups = SortGroups(pass);
    bool recorded = true;
    for (uint32_t r = 0; recorded && r < MVFX_SORT_PASSES; ++r)
    {
        const uint32_t root[4] = {8u * r, pass->words[0], groups, 0};
        recorded =
            RecordKernel(device, pass->radix[2 * r], pipelines->offsets, bound, 3, root, 1) &&
            RecordKernel(device, pass->radix[2 * r + 1], pipelines->scatter, bound, 3, root,
                         groups);
    }
    return recorded;
}

// Records one pass's numbers, its sort and its kernel.
static bool RecordPass(mrhiDevice* device, uint64_t* uploaded, const mvfxRecordPipelines* pipelines,
                       const mvfxRecordPass* pass)
{
    if (pass->empty)
    {
        return RecordEmpty(device, uploaded, pass);
    }
    bool recorded = mrhiBeginPass(device, pass->write) == mrhi_success &&
                    mvfxUpload(device, uploaded, pass->write, pass->numbers, 0, pass->words,
                               sizeof pass->words);
    recorded = mrhiEndPass(device, pass->write) == mrhi_success && recorded;
    const mvfxRecordResources* r = &pass->resources;
    const mrhiResourceId bound[RECORD_BINDINGS] = {pass->numbers, r->particles, r->statistics,
                                                   r->grids,      r->records,   r->count,
                                                   r->sort,       r->digits};
    if (recorded && pass->sorted)
    {
        recorded = RecordKernel(device, pass->keys, pipelines->keys, bound, RECORD_BINDINGS,
                                nullptr, SortGroups(pass)) &&
                   RecordRadix(device, pipelines, pass);
    }
    return recorded && RecordKernel(device, pass->kernel, pipelines->records, bound,
                                    RECORD_BINDINGS, nullptr, pass->words[5]);
}

bool mvfxRecordRecordPasses(mrhiDevice* device, uint64_t* uploaded,
                            const mvfxRecordPipelines* pipelines, mvfxRecordPasses* passes)
{
    bool recorded = true;
    for (int32_t p = 0; recorded && p < passes->count; ++p)
    {
        recorded = RecordPass(device, uploaded, pipelines, &passes->passes[p]);
    }
    passes->count = 0;
    return recorded;
}
