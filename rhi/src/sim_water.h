// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A water surface's step on the GPU (water.wgsl), after every gas's, as
// the core steps it: its fields in a buffer of their own, a pass a
// dispatch; and the host's reads of its fields (water_read.wgsl).

#ifndef MAUL_VFX_RHI_SRC_SIM_WATER_H
#define MAUL_VFX_RHI_SRC_SIM_WATER_H

#include "grids.h"
#include "kernels.h"
#include "numbers.h"

#include "maul-rhi/frame.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/plan.h"

// The words of a surface's numbers (water_numbers.wgsl's Water): its
// sizes, factors, head, sink and faces; its pushes, and shallow water's
// pushes' weight sums, sources and terrain.
#define MVFX_WATER_WORDS                                                                           \
    (20u + 8u * MVFX_MAX_WATER_PUSHES + 8u * MVFX_MAX_WATER_SOURCES + MVFX_COLLIDER_WORDS)

// A surface's buffers: its numbers, its fields (waves: heights now and
// next, rates now and next; shallow water: depths, X faces and Z faces,
// now and next each, the givers' scales, the ground), its statistics
// record, its terrain's grid.
#define WATER_NUMBERS    0
#define WATER_FIELDS     1
#define WATER_STATISTICS 2
#define WATER_GRIDS      3
#define WATER_BUFFERS    4

// The most dispatches a step takes: waves' sinks, a substep each and the
// settle; shallow water's start, pushes, three a substep and settle.
#define WATER_DISPATCHES (3 + 3 * MVFX_MAX_WATER_SUBSTEPS)

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

// The fields a coupled emitter reads (sim/water.wgsl): waves' heights and
// rates, or shallow water's depths and ground, then its faces across X and
// across Z.
#define WATER_COPIES 4

// A surface: its view, buffers, numbers and its terrain's grid; the buffer
// current now, and after the step added (current once it is recorded: a
// frame the host drops leaves the surface as it was); whether its fields
// hold a step's (a step recorded, or added in a lane before); the statistics
// record the step's write starts with (nothing moving, the step's end);
// the step's dispatches and their passes (the first pass writing the
// numbers), held whole so a lane of a split frame (sim_lanes.c) copies it;
// whether the step clears it first, whether it was added, whether its
// buffers are imported into the frame open; the frame's reads.
typedef struct SimWater
{
    mvfxWaterView view;
    mrhiBufferId buffers[WATER_BUFFERS];
    mrhiResourceId resources[WATER_BUFFERS];
    uint32_t words[MVFX_WATER_WORDS];
    mvfxGpuGrids grids;
    uint32_t now;
    uint32_t after;
    bool stepped;
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

// Reads an effect's surfaces into the simulation.
mvfxResult mvfxReadSimWaters(mvfxGpuSim* sim, mvfxEffectId effect);

// Makes a surface's buffers, a host's terrain's grid taking hostWords of
// its grids' buffer; false when the device refuses or a grid is gone.
bool mvfxCreateSimWater(mvfxGpuSim* sim, SimWater* water, uint32_t hostWords);
void mvfxDestroySimWater(mvfxGpuSim* sim, SimWater* water);

// The fields a coupled emitter copies from buffer now: each one's first
// float and floats (0 for waves' faces).
void mvfxSimWaterFields(const SimWater* water, uint32_t now, uint32_t firsts[WATER_COPIES],
                        uint32_t floats[WATER_COPIES]);

// Adds surface w's step's passes for its plan, then records them.
mvfxResult mvfxAddSimWaterPasses(mvfxGpuSim* sim, int32_t w);
bool mvfxRecordSimWater(mvfxGpuSim* sim, SimWater* water);

#endif
