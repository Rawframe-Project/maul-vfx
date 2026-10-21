// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface's step on the GPU (water.wgsl), after every gas's, as
// the core steps it: its fields in a buffer of their own, a pass a
// dispatch; and the host's reads of its fields (water_read.wgsl).

#ifndef MAUL_VFX_RHI_SRC_SIM_WATER_H
#define MAUL_VFX_RHI_SRC_SIM_WATER_H

#include "kernels.h"

#include "maul-rhi/frame.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/plan.h"

// The words of a surface's numbers (water_numbers.wgsl's Water): its
// sizes, factors, head and sink, then its pushes.
#define MVFX_WATER_WORDS (16u + 4u * MVFX_MAX_WATER_PUSHES)

// A surface's buffers: its numbers, its fields (heights now and next,
// rates now and next), its statistics record.
#define WATER_NUMBERS    0
#define WATER_FIELDS     1
#define WATER_STATISTICS 2
#define WATER_BUFFERS    3

// The most dispatches a step takes: the sinks, a substep each, the
// settle.
#define WATER_DISPATCHES (2 + MVFX_MAX_WATER_SUBSTEPS)

// A dispatch of a surface's step: its kernel, the buffer current and its
// workgroups.
typedef struct WaterDispatch
{
    mvfxKernel kernel;
    uint32_t now;
    uint32_t groups;
} WaterDispatch;

// The words of a water read's numbers (water_read.wgsl's Read).
#define WATER_READ_WORDS 20u

// A read of a surface a frame: its numbers' buffer, the frame's ids of it
// and of the host's, its passes, its numbers and the words it writes.
typedef struct WaterRead
{
    mrhiBufferId numbers;
    mrhiResourceId resource;
    mrhiResourceId target;
    mrhiPassId passes[2];
    uint32_t words[WATER_READ_WORDS];
    uint32_t written;
} WaterRead;

// A surface: its view, buffers and numbers; the buffer current now, and
// after the step added (current once it is recorded: a frame the host
// drops leaves the surface as it was); the statistics record the step's
// write starts with (nothing moving, the step's end); the step's
// dispatches and their passes (the first pass writing the numbers), held
// whole so a lane of a split frame (sim_lanes.c) copies it; whether the
// step clears it first, whether it was added, whether its buffers are
// imported into the frame open; the frame's reads.
typedef struct SimWater
{
    mvfxWaterView view;
    mrhiBufferId buffers[WATER_BUFFERS];
    mrhiResourceId resources[WATER_BUFFERS];
    uint32_t words[MVFX_WATER_WORDS];
    uint32_t now;
    uint32_t after;
    uint32_t record[MVFX_GPU_WATER_STATISTICS_WORDS];
    WaterDispatch dispatches[WATER_DISPATCHES];
    mrhiPassId passes[WATER_DISPATCHES + 1];
    int32_t count;
    bool clears;
    bool added;
    bool imported;
    WaterRead reads[MVFX_GPU_WATER_READS];
    int32_t readCount;
} SimWater;

// Reads an effect's surfaces into the simulation; mvfx_errorState with the
// surface in refusal for one the part does not run (shallow water).
mvfxResult mvfxReadSimWaters(mvfxGpuSim* sim, mvfxEffectId effect, int32_t* refusal);

// Makes a surface's buffers; false when the device refuses.
bool mvfxCreateSimWater(mvfxGpuSim* sim, SimWater* water);
void mvfxDestroySimWater(mvfxGpuSim* sim, SimWater* water);

// Adds surface w's step's passes for its plan, then records them.
mvfxResult mvfxAddSimWaterPasses(mvfxGpuSim* sim, int32_t w);
bool mvfxRecordSimWater(mvfxGpuSim* sim, SimWater* water);

#endif
