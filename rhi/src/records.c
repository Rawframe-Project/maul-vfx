// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "records.h"

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

bool mvfxAddRecordPass(mrhiDevice* device, mvfxRecordPasses* passes,
                       const mvfxRecordResources* resources, const uint32_t* words,
                       int64_t capacity)
{
    mvfxRecordPass* pass = &passes->passes[passes->count];
    *pass = (mvfxRecordPass){.resources = *resources};
    memcpy(pass->words, words, sizeof pass->words);
    // One invocation a record, 64 a workgroup, at most a dispatch's.
    int64_t groups = (capacity + 63) / 64;
    pass->groups = (uint32_t)(groups < 65535 ? groups : 65535);
    pass->words[5] = pass->groups > 0 ? pass->groups : 1;
    if (mrhiImportBuffer(device, passes->numbers[passes->count], &pass->numbers) != mrhi_success)
    {
        return false;
    }
    const mrhiAccess written[1] = {{.resource = pass->numbers, .kind = mrhi_accessCopyDestination}};
    const mrhiAccess used[6] = {
        {.resource = pass->numbers, .kind = mrhi_accessUniform},
        {.resource = resources->particles, .kind = mrhi_accessStorageRead},
        {.resource = resources->statistics, .kind = mrhi_accessStorageRead},
        {.resource = resources->grids, .kind = mrhi_accessStorageRead},
        {.resource = resources->records, .kind = mrhi_accessStorageReadWrite},
        {.resource = resources->count, .kind = mrhi_accessStorageReadWrite}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = written;
    def.accessCount = 1;
    def.neverCull = true;
    bool added = mrhiAddPass(device, &def, &pass->write) == mrhi_success;
    def.passClass = mrhi_passGraphics;
    def.accesses = used;
    def.accessCount = 6;
    added = added && mrhiAddPass(device, &def, &pass->kernel) == mrhi_success;
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
static bool RecordEmpty(mrhiDevice* device, const mvfxRecordPass* pass)
{
    static const uint32_t zero = 0;
    bool recorded =
        mrhiBeginPass(device, pass->write) == mrhi_success &&
        mrhiWriteBuffer(device, pass->write, pass->resources.count, (uint64_t)pass->words[0] * 4u,
                        &zero, sizeof zero) == mrhi_success;
    return mrhiEndPass(device, pass->write) == mrhi_success && recorded;
}

// Records one pass's numbers and its kernel.
static bool RecordPass(mrhiDevice* device, mrhiComputePipelineId pipeline,
                       const mvfxRecordPass* pass)
{
    if (pass->empty)
    {
        return RecordEmpty(device, pass);
    }
    bool recorded = mrhiBeginPass(device, pass->write) == mrhi_success &&
                    mrhiWriteBuffer(device, pass->write, pass->numbers, 0, pass->words,
                                    sizeof pass->words) == mrhi_success;
    recorded = mrhiEndPass(device, pass->write) == mrhi_success && recorded;
    const mrhiResourceId bound[6] = {
        pass->numbers,         pass->resources.particles, pass->resources.statistics,
        pass->resources.grids, pass->resources.records,   pass->resources.count};
    mrhiBinding bindings[6];
    for (int32_t b = 0; b < 6; ++b)
    {
        bindings[b] =
            (mrhiBinding){.slot = (uint32_t)b, .resource = bound[b], .size = MRHI_WHOLE_SIZE};
    }
    bool ran = recorded && mrhiBeginPass(device, pass->kernel) == mrhi_success &&
               mrhiSetComputePipeline(device, pass->kernel, pipeline) == mrhi_success &&
               mrhiSetBindings(device, pass->kernel, 0, bindings, 6) == mrhi_success &&
               mrhiDispatch(device, pass->kernel, pass->words[5], 1, 1) == mrhi_success;
    return mrhiEndPass(device, pass->kernel) == mrhi_success && ran;
}

bool mvfxRecordRecordPasses(mrhiDevice* device, mrhiComputePipelineId pipeline,
                            mvfxRecordPasses* passes)
{
    bool recorded = true;
    for (int32_t p = 0; recorded && p < passes->count; ++p)
    {
        recorded = RecordPass(device, pipeline, &passes->passes[p]);
    }
    passes->count = 0;
    return recorded;
}
