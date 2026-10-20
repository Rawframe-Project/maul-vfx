// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A smoke gas's step on the GPU (gas.wgsl), after every emitter's: its
// fields and its solve's levels in buffers of their own, a pass a stage
// of each substep as the core's gas step runs them.

#ifndef MAUL_VFX_RHI_SRC_SIM_GAS_H
#define MAUL_VFX_RHI_SRC_SIM_GAS_H

#include "kernels.h"

#include "maul-rhi/frame.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/plan.h"

// The words of a gas's numbers (gas.wgsl's Gas).
#define MVFX_GAS_WORDS 384u

// A gas's buffers: its numbers, its fields (each buffer's faces then its
// scalars), its solve's levels (pressures, right-hand sides, scratches,
// weights, areas and coefficients).
#define GAS_NUMBERS 0
#define GAS_FIELDS  1
#define GAS_SOLVE   2
#define GAS_BUFFERS 3

// A dispatch of a gas's step: its kernel, root words and workgroups.
typedef struct GasDispatch
{
    mvfxKernel kernel;
    uint32_t root[4];
    uint32_t groups;
} GasDispatch;

// The words of a gas read's numbers (gas_read.wgsl's Read).
#define GAS_READ_WORDS 20u

// A read of a gas a frame: its numbers' buffer, the frame's ids of it and
// of the host's, its passes, its numbers and the words it writes.
typedef struct GasRead
{
    mrhiBufferId numbers;
    mrhiResourceId resource;
    mrhiResourceId target;
    mrhiPassId passes[2];
    uint32_t words[GAS_READ_WORDS];
    uint32_t written;
} GasRead;

typedef struct SimGas
{
    mvfxGasView view;
    mrhiBufferId buffers[GAS_BUFFERS];
    mrhiResourceId resources[GAS_BUFFERS];
    uint32_t words[MVFX_GAS_WORDS];
    // The first level the tail runs in one workgroup.
    uint32_t tail;
    // The buffer current now, and whether the levels' areas and
    // coefficients are made (not since the start or a clear).
    uint32_t now;
    bool prepared;
    // The step's dispatches and their passes (the first pass writing the
    // numbers), the most a step takes, and whether it clears first.
    GasDispatch* dispatches;
    mrhiPassId* passes;
    int32_t capacity;
    int32_t count;
    bool clears;
    bool added;
    // Whether its buffers are imported into the frame open.
    bool imported;
    // The frame's reads.
    GasRead reads[MVFX_GPU_GAS_READS];
    int32_t readCount;
} SimGas;

// Reads an effect's gases into the simulation; mvfx_errorState with the
// gas in refusal for one the part does not run.
mvfxResult mvfxReadSimGases(mvfxGpuSim* sim, mvfxEffectId effect, int32_t* refusal);

// Makes a gas's buffers and dispatch room; false when the device or the
// allocator refuses.
bool mvfxCreateSimGas(mvfxGpuSim* sim, SimGas* gas);
void mvfxDestroySimGas(mvfxGpuSim* sim, SimGas* gas);

// Adds a gas's step's passes for its plan, then records them.
mvfxResult mvfxAddSimGasPasses(mvfxGpuSim* sim, int32_t g);
bool mvfxRecordSimGas(mvfxGpuSim* sim, SimGas* gas);

// The passes a gas's step takes at most.
int32_t mvfxSimGasPasses(const mvfxGasView* view);

#endif
