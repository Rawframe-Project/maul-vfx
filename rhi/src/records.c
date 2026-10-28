// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "records.h"

#include "upload.h"

#include "maul-rhi/resources.h"

#include <string.h>

// Creates a uniform buffer of words the transfer passes write.
static mrhiResult CreateNumbers(mrhiDevice* device, uint32_t words, mrhiBufferId* bufferOut)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = words * sizeof(uint32_t);
    def.usage = mrhi_bufferUniform | mrhi_bufferCopyDestination;
    return mrhiCreateBuffer(device, &def, bufferOut);
}

mrhiResult mvfxCreateRecordBuffers(mrhiDevice* device, mvfxRecordPasses* passes)
{
    mrhiResult made = mrhi_success;
    for (int32_t p = 0; made == mrhi_success && p < MVFX_GPU_RECORD_PASSES; ++p)
    {
        made = CreateNumbers(device, MVFX_RECORD_WORDS, &passes->numbers[p]);
        made = made == mrhi_success
                   ? CreateNumbers(device, MVFX_RIBBON_WORDS, &passes->ribbonNumbers[p])
                   : made;
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
        if (passes->ribbonNumbers[p].index1 != 0)
        {
            (void)mrhiDestroyBuffer(device, passes->ribbonNumbers[p]);
        }
    }
}

// The bindings of the records module's entries: the numbers, the
// particles, the statistics, the grids, the records, the count, the sort's
// keys and indices and its digit counts; the ribbon records module's add
// the list and the ribbon numbers.
#define RECORD_BINDINGS 8
#define RIBBON_BINDINGS 10

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

// Adds a record, ribbon, sprite or sheet pass (the last three with ribbon
// numbers, their sink's kind among them) of these numbers over these
// resources, its workgroups for capacity records or particles.
// A pass of these numbers and ribbon numbers (NULL for a record pass):
// what it is by its sink, whether it sorts. Plexus links (a links span in
// the ribbon numbers' sink.w) come chained already: no keys or radix
// passes. Sheets are written oldest first.
static mvfxRecordPass PassOf(const mvfxRecordResources* resources, const uint32_t* words,
                             const uint32_t* ribbonWords)
{
    uint32_t sink = ribbonWords != nullptr ? ribbonWords[24] : MVFX_SINK_POINTS;
    bool sprites = ribbonWords != nullptr && sink == MVFX_SINK_SPRITES;
    bool sheets = ribbonWords != nullptr && sink == MVFX_SINK_SHEETS;
    bool ribbons = ribbonWords != nullptr && !sprites && !sheets;
    bool links = ribbons && ribbonWords[27] != 0u;
    return (mvfxRecordPass){
        .resources = *resources,
        .ribbons = ribbons,
        .sprites = sprites,
        .sheets = sheets,
        .sorted = ribbons ? !links : !sheets && words[MVFX_RECORD_ORDER] == MVFX_ORDER_SORTED};
}

static bool AddPass(mrhiDevice* device, mvfxRecordPasses* passes,
                    const mvfxRecordResources* resources, const uint32_t* words,
                    const uint32_t* ribbonWords, int64_t capacity)
{
    mvfxRecordPass* pass = &passes->passes[passes->count];
    bool numbered = ribbonWords != nullptr;
    *pass = PassOf(resources, words, ribbonWords);
    memcpy(pass->words, words, sizeof pass->words);
    // One invocation a record, 64 a workgroup, at most a dispatch's.
    int64_t groups = (capacity + 63) / 64;
    pass->groups = (uint32_t)(groups < 65535 ? groups : 65535);
    pass->words[5] = pass->groups > 0 ? pass->groups : 1;
    if (mrhiImportBuffer(device, passes->numbers[passes->count], &pass->numbers) != mrhi_success ||
        (numbered && mrhiImportBuffer(device, passes->ribbonNumbers[passes->count],
                                      &pass->ribbonNumbers) != mrhi_success))
    {
        return false;
    }
    if (numbered)
    {
        memcpy(pass->ribbonWords, ribbonWords, sizeof pass->ribbonWords);
        // The most pieces a segment is cut into (ribbon_records.wgsl's cut).
        pass->cut = pass->ribbons && ribbonWords[16] > 1u;
    }
    const mrhiAccess written[2] = {
        {.resource = pass->numbers, .kind = mrhi_accessCopyDestination},
        {.resource = pass->ribbonNumbers, .kind = mrhi_accessCopyDestination}};
    const mrhiAccess used[RIBBON_BINDINGS] = {
        {.resource = pass->numbers, .kind = mrhi_accessUniform},
        {.resource = resources->particles, .kind = mrhi_accessStorageRead},
        {.resource = resources->statistics, .kind = mrhi_accessStorageRead},
        {.resource = resources->grids, .kind = mrhi_accessStorageRead},
        {.resource = resources->records, .kind = mrhi_accessStorageReadWrite},
        {.resource = resources->count, .kind = mrhi_accessStorageReadWrite},
        {.resource = resources->sort, .kind = mrhi_accessStorageReadWrite},
        {.resource = resources->digits, .kind = mrhi_accessStorageReadWrite},
        {.resource = resources->list, .kind = mrhi_accessStorageReadWrite},
        {.resource = pass->ribbonNumbers, .kind = mrhi_accessUniform}};
    int32_t bound = numbered ? RIBBON_BINDINGS : RECORD_BINDINGS;
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = written;
    def.accessCount = numbered ? 2 : 1;
    def.neverCull = true;
    bool added = mrhiAddPass(device, &def, &pass->write) == mrhi_success;
    if (pass->sorted)
    {
        added = added && AddKernelPass(device, used, bound, &pass->keys) &&
                AddRadixPasses(device, pass);
    }
    if (pass->cut)
    {
        added = added && AddKernelPass(device, used, bound, &pass->pieces);
    }
    if (pass->ribbons)
    {
        added = added && AddKernelPass(device, used, bound, &pass->chains);
    }
    if (pass->sheets)
    {
        added = added && AddKernelPass(device, used, bound, &pass->fit);
    }
    added = added && AddKernelPass(device, used, bound, &pass->kernel);
    passes->count += added ? 1 : 0;
    return added;
}

bool mvfxAddRecordPass(mrhiDevice* device, mvfxRecordPasses* passes,
                       const mvfxRecordResources* resources, const uint32_t* words,
                       int64_t capacity)
{
    return AddPass(device, passes, resources, words, nullptr, capacity);
}

bool mvfxAddRibbonPass(mrhiDevice* device, mvfxRecordPasses* passes,
                       const mvfxRecordResources* resources, const uint32_t* words,
                       const uint32_t* ribbonWords, int64_t capacity)
{
    return AddPass(device, passes, resources, words, ribbonWords, capacity);
}

bool mvfxAddLightPass(mrhiDevice* device, mvfxRecordPasses* passes,
                      const mvfxRecordResources* resources, const uint32_t* words)
{
    mvfxRecordPass* pass = &passes->passes[passes->count];
    *pass = (mvfxRecordPass){.resources = *resources, .lights = true};
    memcpy(pass->words, words, sizeof pass->words);
    pass->groups = words[MVFX_RECORD_LIGHT + 2];
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
    bool added = mrhiAddPass(device, &def, &pass->write) == mrhi_success &&
                 AddKernelPass(device, used, RECORD_BINDINGS, &pass->lightCount) &&
                 AddKernelPass(device, used, RECORD_BINDINGS, &pass->lightOffsets) &&
                 AddKernelPass(device, used, RECORD_BINDINGS, &pass->kernel);
    passes->count += added ? 1 : 0;
    return added;
}

bool mvfxAddEmptyRecordPass(mrhiDevice* device, mvfxRecordPasses* passes, mrhiResourceId count,
                            uint32_t word, uint32_t counts)
{
    mvfxRecordPass* pass = &passes->passes[passes->count];
    *pass = (mvfxRecordPass){.empty = true, .resources = {.count = count}};
    pass->words[0] = word;
    pass->words[1] = counts;
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

// Records an empty pass's counts of 0.
static bool RecordEmpty(mrhiDevice* device, uint64_t* uploaded, const mvfxRecordPass* pass)
{
    static const uint32_t zero[4] = {0};
    bool recorded = mrhiBeginPass(device, pass->write) == mrhi_success &&
                    mvfxUpload(device, uploaded, pass->write, pass->resources.count,
                               (uint64_t)pass->words[0] * 4u, zero, pass->words[1] * 4u);
    return mrhiEndPass(device, pass->write) == mrhi_success && recorded;
}

// Records a kernel's pass: its pipeline, bindings, root block (none for
// NULL) and workgroups.
static bool RecordKernel(mrhiDevice* device, mrhiPassId pass, mrhiComputePipelineId pipeline,
                         const mrhiResourceId* bound, int32_t count, const uint32_t* root,
                         uint32_t groups)
{
    mrhiBinding bindings[RIBBON_BINDINGS];
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

// Records one pass's numbers, its sort, a ribbon pass's chains and its
// kernel, or a lights pass's three.
// Whether a pass reads ribbon numbers: a ribbon, sprite or sheet pass.
static bool IsNumbered(const mvfxRecordPass* pass)
{
    return pass->ribbons || pass->sprites || pass->sheets;
}

// Records a pass's kernels before its own: the sort's keys and radix
// passes, a ribbon pass's pieces and chains, a sheet pass's fit.
static bool RecordBefore(mrhiDevice* device, const mvfxRecordPipelines* pipelines,
                         const mvfxRecordPass* pass, const mrhiResourceId* bound, int32_t count)
{
    bool recorded = true;
    if (pass->sorted)
    {
        // A sprite pass's keys are the records' (their module's bindings).
        recorded = RecordKernel(
                       device, pass->keys, pass->ribbons ? pipelines->ribbonKeys : pipelines->keys,
                       bound, pass->ribbons ? count : RECORD_BINDINGS, nullptr, SortGroups(pass)) &&
                   RecordRadix(device, pipelines, pass);
    }
    if (recorded && pass->cut)
    {
        recorded = RecordKernel(device, pass->pieces, pipelines->ribbonPieces, bound, count,
                                nullptr, pass->words[5]);
    }
    if (recorded && pass->ribbons)
    {
        recorded =
            RecordKernel(device, pass->chains, pipelines->ribbonChains, bound, count, nullptr, 1);
    }
    if (recorded && pass->sheets)
    {
        recorded = RecordKernel(device, pass->fit, pipelines->sheetFit, bound, count, nullptr, 1);
    }
    return recorded;
}

// The pipeline of a pass's own kernel, but for a lights pass's.
static mrhiComputePipelineId KernelOf(const mvfxRecordPipelines* pipelines,
                                      const mvfxRecordPass* pass)
{
    return pass->ribbons   ? pipelines->ribbonPoints
           : pass->sprites ? pipelines->spriteQuads
           : pass->sheets  ? pipelines->sheetPoints
                           : pipelines->records;
}

static bool RecordPass(mrhiDevice* device, uint64_t* uploaded, const mvfxRecordPipelines* pipelines,
                       const mvfxRecordPass* pass)
{
    if (pass->empty)
    {
        return RecordEmpty(device, uploaded, pass);
    }
    bool recorded =
        mrhiBeginPass(device, pass->write) == mrhi_success &&
        mvfxUpload(device, uploaded, pass->write, pass->numbers, 0, pass->words,
                   sizeof pass->words) &&
        (!IsNumbered(pass) || mvfxUpload(device, uploaded, pass->write, pass->ribbonNumbers, 0,
                                         pass->ribbonWords, sizeof pass->ribbonWords));
    recorded = mrhiEndPass(device, pass->write) == mrhi_success && recorded;
    const mvfxRecordResources* r = &pass->resources;
    const mrhiResourceId bound[RIBBON_BINDINGS] = {
        pass->numbers, r->particles, r->statistics, r->grids, r->records,
        r->count,      r->sort,      r->digits,     r->list,  pass->ribbonNumbers};
    int32_t count = IsNumbered(pass) ? RIBBON_BINDINGS : RECORD_BINDINGS;
    recorded = recorded && RecordBefore(device, pipelines, pass, bound, count);
    if (pass->lights)
    {
        return recorded &&
               RecordKernel(device, pass->lightCount, pipelines->lightCount, bound, count, nullptr,
                            pass->groups) &&
               RecordKernel(device, pass->lightOffsets, pipelines->lightOffsets, bound, count,
                            nullptr, 1) &&
               RecordKernel(device, pass->kernel, pipelines->lightWrite, bound, count, nullptr,
                            pass->groups);
    }
    return recorded && RecordKernel(device, pass->kernel, KernelOf(pipelines, pass), bound, count,
                                    nullptr, pass->words[5]);
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
