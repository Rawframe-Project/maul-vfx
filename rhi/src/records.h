// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's record passes: each a transfer pass writing its
// numbers, for a sort by place a keys pass and four radix passes of two
// kernels each, and a compute pass writing the records into the host's
// buffer.

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
// an emitter that does not sort).
typedef struct mvfxRecordResources
{
    mrhiResourceId particles;
    mrhiResourceId statistics;
    mrhiResourceId grids;
    mrhiResourceId records;
    mrhiResourceId count;
    mrhiResourceId sort;
    mrhiResourceId digits;
} mvfxRecordResources;

// The pipelines record passes run: the records', the keys', and a radix
// pass's offsets' and scatter's.
typedef struct mvfxRecordPipelines
{
    mrhiComputePipelineId records;
    mrhiComputePipelineId keys;
    mrhiComputePipelineId offsets;
    mrhiComputePipelineId scatter;
} mvfxRecordPipelines;

// A record pass added to the frame open: its passes (only the transfer
// pass, writing a count of 0, for an emitter without particles at the
// instance's quality level; the sort's for one that sorts), resources,
// numbers and workgroups.
typedef struct mvfxRecordPass
{
    bool empty;
    bool sorted;
    mrhiPassId write;
    mrhiPassId keys;
    mrhiPassId radix[2 * MVFX_SORT_PASSES];
    mrhiPassId kernel;
    mrhiResourceId numbers;
    mvfxRecordResources resources;
    uint32_t groups;
    uint32_t words[MVFX_RECORD_WORDS];
} mvfxRecordPass;

// An emitter's record passes: a numbers buffer each, those added this
// frame.
typedef struct mvfxRecordPasses
{
    mrhiBufferId numbers[MVFX_GPU_RECORD_PASSES];
    mvfxRecordPass passes[MVFX_GPU_RECORD_PASSES];
    int32_t count;
} mvfxRecordPasses;

// Creates the numbers buffers; false when the device refuses one.
bool mvfxCreateRecordBuffers(mrhiDevice* device, mvfxRecordPasses* passes);

// Destroys the buffers made.
void mvfxDestroyRecordBuffers(mrhiDevice* device, mvfxRecordPasses* passes);

// Adds a record pass of these numbers over these resources, its records'
// workgroups for capacity records; false when the device refuses an
// import or a pass.
bool mvfxAddRecordPass(mrhiDevice* device, mvfxRecordPasses* passes,
                       const mvfxRecordResources* resources, const uint32_t* words,
                       int64_t capacity);

// Adds a pass writing a count of 0 at a word of the host's count buffer,
// for an emitter without particles; false when the device refuses it.
bool mvfxAddEmptyRecordPass(mrhiDevice* device, mvfxRecordPasses* passes, mrhiResourceId count,
                            uint32_t word);

// Records the passes added and lets them go; false when the device
// refuses a command.
bool mvfxRecordRecordPasses(mrhiDevice* device, const mvfxRecordPipelines* pipelines,
                            mvfxRecordPasses* passes);

#endif
