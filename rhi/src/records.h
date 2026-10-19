// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's record passes: each a transfer pass writing its
// numbers, for a sort by place a keys pass and four radix passes of two
// kernels each, and a compute pass writing the records into the host's
// buffer. A ribbon pass is one too: its keys the ribbon ids, then a pass
// finding the chains and one writing their points; so is a lights pass:
// each block's carriers counted, the counts scanned, the lights written.

#ifndef MAUL_VFX_RHI_SRC_RECORDS_H
#define MAUL_VFX_RHI_SRC_RECORDS_H

#include "numbers.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"

// The radix passes of a sort, 8 bits each.
#define MVFX_SORT_PASSES 4

// The frame's resources a record pass reads and writes: the particles,
// the statistics record, the grids (the baked tables), the host's
// records and count (its own word where the host gives none), the sort's
// keys and indices and its digit counts (the simulation's own words for
// an emitter that does not sort), and a ribbon pass's list.
typedef struct mvfxRecordResources
{
    mrhiResourceId particles;
    mrhiResourceId statistics;
    mrhiResourceId grids;
    mrhiResourceId records;
    mrhiResourceId count;
    mrhiResourceId sort;
    mrhiResourceId digits;
    mrhiResourceId list;
} mvfxRecordResources;

// The pipelines record passes run: the records', the keys', a radix
// pass's offsets' and scatter's, and a ribbon pass's keys', chains' and
// points'.
typedef struct mvfxRecordPipelines
{
    mrhiComputePipelineId records;
    mrhiComputePipelineId keys;
    mrhiComputePipelineId offsets;
    mrhiComputePipelineId scatter;
    mrhiComputePipelineId ribbonKeys;
    mrhiComputePipelineId ribbonChains;
    mrhiComputePipelineId ribbonPoints;
    mrhiComputePipelineId ribbonPieces;
    mrhiComputePipelineId lightCount;
    mrhiComputePipelineId lightOffsets;
    mrhiComputePipelineId lightWrite;
} mvfxRecordPipelines;

// A record pass added to the frame open: its passes (only the transfer
// pass, writing counts of 0, for an emitter without particles at the
// instance's quality level or a ribbon pass of one not drawn as ribbons;
// the sort's for one that sorts or a ribbon pass; the chains' for a
// ribbon pass, and the pieces' for ribbons of more pieces than one; a
// lights pass's counts and offsets), resources, numbers and workgroups.
typedef struct mvfxRecordPass
{
    bool empty;
    bool sorted;
    bool ribbons;
    bool cut;
    bool lights;
    mrhiPassId write;
    mrhiPassId keys;
    mrhiPassId radix[2 * MVFX_SORT_PASSES];
    mrhiPassId pieces;
    mrhiPassId chains;
    mrhiPassId lightCount;
    mrhiPassId lightOffsets;
    mrhiPassId kernel;
    mrhiResourceId numbers;
    mrhiResourceId ribbonNumbers;
    mvfxRecordResources resources;
    uint32_t groups;
    uint32_t words[MVFX_RECORD_WORDS];
    uint32_t ribbonWords[MVFX_RIBBON_WORDS];
} mvfxRecordPass;

// An emitter's record passes: a numbers buffer each (and a ribbon numbers
// buffer each for an emitter drawn as ribbons), those added this frame.
typedef struct mvfxRecordPasses
{
    mrhiBufferId numbers[MVFX_GPU_RECORD_PASSES];
    mrhiBufferId ribbonNumbers[MVFX_GPU_RECORD_PASSES];
    mvfxRecordPass passes[MVFX_GPU_RECORD_PASSES];
    int32_t count;
} mvfxRecordPasses;

// Creates the numbers buffers, the ribbon numbers too for an emitter
// drawn as ribbons; false when the device refuses one.
bool mvfxCreateRecordBuffers(mrhiDevice* device, mvfxRecordPasses* passes, bool ribbons);

// Destroys the buffers made.
void mvfxDestroyRecordBuffers(mrhiDevice* device, mvfxRecordPasses* passes);

// Adds a record pass of these numbers over these resources, its records'
// workgroups for capacity records; false when the device refuses an
// import or a pass.
bool mvfxAddRecordPass(mrhiDevice* device, mvfxRecordPasses* passes,
                       const mvfxRecordResources* resources, const uint32_t* words,
                       int64_t capacity);

// Adds a ribbon pass of these numbers over these resources, its points'
// workgroups for capacity particles; false when the device refuses an
// import or a pass.
bool mvfxAddRibbonPass(mrhiDevice* device, mvfxRecordPasses* passes,
                       const mvfxRecordResources* resources, const uint32_t* words,
                       const uint32_t* ribbonWords, int64_t capacity);

// Adds a lights pass of these numbers over these resources; false when
// the device refuses an import or a pass.
bool mvfxAddLightPass(mrhiDevice* device, mvfxRecordPasses* passes,
                      const mvfxRecordResources* resources, const uint32_t* words);

// Adds a pass writing counts of 0 (at most 4) from a word of the host's
// count buffer, for an emitter without particles or one not drawn as
// ribbons; false when the device refuses it.
bool mvfxAddEmptyRecordPass(mrhiDevice* device, mvfxRecordPasses* passes, mrhiResourceId count,
                            uint32_t word, uint32_t counts);

// Records the passes added and lets them go, the bytes they upload added
// to uploaded; false when the device refuses a command.
bool mvfxRecordRecordPasses(mrhiDevice* device, uint64_t* uploaded,
                            const mvfxRecordPipelines* pipelines, mvfxRecordPasses* passes);

#endif
