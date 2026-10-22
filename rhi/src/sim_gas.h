// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A smoke gas's step on the GPU (gas.wgsl), after every emitter's: its
// fields and its solve's levels in buffers of their own, a pass a stage
// of each substep as the core's gas step runs them.

#ifndef MAUL_VFX_RHI_SRC_SIM_GAS_H
#define MAUL_VFX_RHI_SRC_SIM_GAS_H

#include "grids.h"
#include "kernels.h"
#include "numbers.h"

#include "maul-rhi/frame.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/plan.h"

// The words of a gas's numbers (gas.wgsl's Gas): its own, its forces'
// (rhi/src/numbers.h's MVFX_FORCE_WORDS), its burn's and modes, then its
// obstacles' head and its obstacles now and before.
#define MVFX_GAS_WORDS                                                                             \
    (416u + 4u + 132u * MVFX_MAX_FORCES + 8u + 4u + 4u * MVFX_MAX_COLLIDERS * MVFX_COLLIDER_WORDS)

// A gas's buffers: its numbers, its fields (each buffer's faces then its
// scalars), its solve's levels (pressures, right-hand sides, scratches,
// weights, areas and coefficients), the grids its forces read.
#define GAS_NUMBERS    0
#define GAS_FIELDS     1
#define GAS_SOLVE      2
#define GAS_GRIDS      3
#define GAS_STATISTICS 4
#define GAS_BUFFERS    5

// What a dispatch of a gas's step binds: the gas's buffers, a splat's
// with the gas's, a splat's sort's (sort.wgsl's), a liquid gas's
// (liquid_gas.wgsl's) or a surface gas's (surface_gas.wgsl's).
typedef enum GasBinding
{
    GAS_BINDS_GAS = 0,
    GAS_BINDS_SPLAT = 1,
    GAS_BINDS_SORT = 2,
    GAS_BINDS_LIQUID = 3,
    GAS_BINDS_SURFACE = 4
} GasBinding;

// The splat a dispatch of a liquid or surface gas's own names: its
// emitter's.
#define LIQUID_SPLAT (-2)

// A dispatch of a gas's step: its kernel, root words and workgroups, what
// it binds and the splat whose buffers (-1 for none).
typedef struct GasDispatch
{
    mvfxKernel kernel;
    uint32_t root[4];
    uint32_t groups;
    GasBinding binds;
    int32_t splat;
} GasDispatch;

// The words of a splat's numbers (splat.wgsl's Splat).
#define SPLAT_NUMBER_WORDS 24u

// A splat's buffers: its numbers, its sorted words (the sort's keys and
// indices, what it keeps of its particles, its runs), its sort's digit
// counts; then those of its emitter it reads: its particles after its
// step and its statistics.
#define SPLAT_NUMBERS    0
#define SPLAT_SORTED     1
#define SPLAT_DIGITS     2
#define SPLAT_PARTICLES  3
#define SPLAT_STATISTICS 4
#define SPLAT_BUFFERS    5

// An emitter splatting into a gas, or a liquid or surface gas's
// emitter: its index, the words of one of its sort's arrays and its
// capacity; the arrays it keeps of a particle (a splat's places and
// velocities, a liquid's affine matrices besides, a surface's places
// alone), its runs (its gas's cells, a surface's buckets) and the words
// after them (a liquid's two sets of its faces' flags); its buffers and
// the frame's ids of them and of its emitter's; its numbers; its drag
// (the plan's).
typedef struct SimSplat
{
    int32_t emitter;
    uint32_t span;
    uint32_t capacity;
    uint32_t kept;
    uint32_t runs;
    uint32_t tail;
    mrhiBufferId buffers[3];
    mrhiResourceId resources[SPLAT_BUFFERS];
    uint32_t words[SPLAT_NUMBER_WORDS];
    float drag;
} SimSplat;

// The workgroups of a point pass, and those of a row of its dispatch (a
// device takes 65,535 along a dimension; gas.wgsl's ROW invocations).
#define GAS_GROUP 64u
#define GAS_ROW   32768u

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

// The words of a gas sample query's numbers (gas_sample.wgsl's Sample).
#define GAS_SAMPLE_WORDS 16u

// A sample query of a gas a frame: its numbers' buffer, the frame's ids
// of it and of the host's points and samples, its passes, its numbers and
// its points.
typedef struct GasSamples
{
    mrhiBufferId numbers;
    mrhiResourceId resource;
    mrhiResourceId points;
    mrhiResourceId target;
    mrhiPassId passes[2];
    uint32_t words[GAS_SAMPLE_WORDS];
    uint32_t count;
} GasSamples;

// The words of a surface mesh's numbers (surface_mesh.wgsl's Mesh), and
// its passes: one writing them, then the kernel's five.
#define MESH_WORDS  24u
#define MESH_PASSES 6

// A surface's mesh a frame: its numbers' buffer, the frame's ids of it and
// of the buffers it writes (positions, normals, indices, counts: the
// simulation's own words where the host gives none), its passes, its
// numbers, its block passes' workgroups, and whether its write uploads
// the marching tables too.
typedef struct SurfaceMesh
{
    mrhiBufferId numbers;
    mrhiResourceId resource;
    mrhiResourceId out[4];
    mrhiPassId passes[MESH_PASSES];
    uint32_t words[MESH_WORDS];
    uint32_t groups;
    bool uploads;
} SurfaceMesh;

typedef struct SimGas
{
    mvfxGasView view;
    mrhiBufferId buffers[GAS_BUFFERS];
    mrhiResourceId resources[GAS_BUFFERS];
    uint32_t words[MVFX_GAS_WORDS];
    mvfxGpuGrids grids;
    // The first level the tail runs in one workgroup.
    uint32_t tail;
    // The buffer current now, and after the step added (current once it
    // is recorded: a frame the host drops leaves the gas as it was); and
    // whether the levels' areas and coefficients are made (not since the
    // start or a clear).
    uint32_t now;
    uint32_t after;
    // The statistics record the step's write starts with: nothing held,
    // the step's end.
    uint32_t record[MVFX_GPU_GAS_STATISTICS_WORDS];
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
    // The frame's sample queries.
    GasSamples samples[MVFX_GPU_GAS_SAMPLES];
    int32_t sampleCount;
    // A surface's meshing (sim_surface_mesh.c): its scratch (the slots,
    // the blocks' sums), the marching tables and whether they are on the
    // device, and the frame's meshes.
    mrhiBufferId meshScratch;
    mrhiBufferId marching;
    bool marchingWritten;
    SurfaceMesh meshes[MVFX_GPU_SURFACE_MESHES];
    int32_t meshCount;
    // The emitters splatting into it (sim_splat.c), how many and the room
    // held for them, and the accesses of its write pass.
    SimSplat* splats;
    int32_t splatCount;
    int32_t splatRoom;
    mrhiAccess* writes;
    // A liquid gas's emitter (sim_liquid_gas.c) or the one a surface gas
    // wraps (sim_surface_gas.c), its sorted words theirs: whether it has
    // one with room.
    SimSplat liquid;
    bool hasLiquid;
} SimGas;

// Reads an effect's gases into the simulation; mvfx_errorState with the
// gas in refusal for one the part does not run.
mvfxResult mvfxReadSimGases(mvfxGpuSim* sim, mvfxEffectId effect, int32_t* refusal);

// Makes a gas's buffers and dispatch room; false when the device or the
// allocator refuses.
// The instance's obstacles' grids take hostWords of its grids' buffer.
bool mvfxCreateSimGas(mvfxGpuSim* sim, SimGas* gas, uint32_t hostWords);
void mvfxDestroySimGas(mvfxGpuSim* sim, SimGas* gas);

// Adds a gas's step's passes for its plan, then records them.
mvfxResult mvfxAddSimGasPasses(mvfxGpuSim* sim, int32_t g);
bool mvfxRecordSimGas(mvfxGpuSim* sim, SimGas* gas);

// Imports a gas's buffers into the frame open, once a frame; false when
// the device refuses.
bool mvfxImportSimGas(mvfxGpuSim* sim, SimGas* gas);

// A gas's reads and sample queries (sim_gas_read.c): their numbers'
// buffers made with the gas (false when the device refuses) and
// destroyed; the frame's recorded, after its step.
bool mvfxCreateSimGasReads(mvfxGpuSim* sim, SimGas* gas);
void mvfxDestroySimGasReads(mvfxGpuSim* sim, SimGas* gas);
bool mvfxRecordSimGasReads(mvfxGpuSim* sim, SimGas* gas);

// A surface's meshes (sim_surface_mesh.c): its buffers made with the gas
// (false when the device refuses; nothing for a gas not a surface) and
// destroyed; the frame's recorded, after its reads.
bool mvfxCreateSimSurfaceMesh(mvfxGpuSim* sim, SimGas* gas);
void mvfxDestroySimSurfaceMesh(mvfxGpuSim* sim, SimGas* gas);
bool mvfxRecordSimSurfaceMeshes(mvfxGpuSim* sim, SimGas* gas);

// The passes a gas's step takes at most, with splats splatting into it.
int32_t mvfxSimGasPasses(const mvfxGasView* view, int32_t splats);

// A step's projection's dispatches (gas.wgsl) at substep u from buffer now.
void mvfxSimGasProject(SimGas* gas, uint32_t u, uint32_t now);

// A liquid gas's step (sim_liquid_gas.c): the passes it takes about a
// projection of the given passes, and its dispatches for a plan from
// buffer now (the projection's alone without an emitter with room).
int32_t mvfxSimLiquidGasPasses(const mvfxGasView* view, int32_t projection);
void mvfxPlanSimLiquid(const mvfxGpuSim* sim, SimGas* gas, const mvfxGasPlan* plan, uint32_t now);

// A surface gas's step (sim_surface_gas.c): the passes it takes, and its
// dispatches for a plan, into the first buffer.
int32_t mvfxSimSurfaceGasPasses(const mvfxGasView* view);
void mvfxPlanSimSurface(const mvfxGpuSim* sim, SimGas* gas, const mvfxGasPlan* plan);

// Emitters splatting into a gas (sim_splat.c): made with the gas, gas g,
// for each emitter splatting into it that has room (false when the device
// or the allocator refuses), and destroyed; the passes they take a step;
// for a plan, each one's numbers and its sort's dispatches, then for each
// substep its pull and its gather's dispatches; each frame, theirs and
// their emitters' buffers imported, the write pass's accesses and its
// writes (their numbers, their runs emptied), and a dispatch's accesses
// and bindings.
bool mvfxCreateSimSplats(mvfxGpuSim* sim, SimGas* gas, int32_t g);
void mvfxDestroySimSplats(mvfxGpuSim* sim, SimGas* gas);
int32_t mvfxSimSplatPasses(int32_t splats, int32_t substeps);
void mvfxPlanSimSplats(const mvfxGpuSim* sim, SimGas* gas, const mvfxGasPlan* plan);
void mvfxSimSplatSubstep(SimGas* gas, uint32_t u, float h, uint32_t now, uint32_t cells,
                         uint32_t faces);
bool mvfxImportSimSplats(mvfxGpuSim* sim, SimGas* gas);
int32_t mvfxSimSplatWrites(const SimGas* gas, mrhiAccess* accesses);
int32_t mvfxSimSplatAccesses(const SimGas* gas, const GasDispatch* d, const mrhiAccess* used,
                             mrhiAccess out[GAS_BUFFERS + SPLAT_BUFFERS]);
bool mvfxWriteSimSplats(mvfxGpuSim* sim, mrhiPassId pass, SimGas* gas);
int32_t mvfxSimSplatBindings(const SimGas* gas, const GasDispatch* d,
                             mrhiBinding out[GAS_BUFFERS + 4]);
// The splat a dispatch names: one splatting into the gas, or its own
// emitter's (LIQUID_SPLAT).
const SimSplat* mvfxSimSplatOf(const SimGas* gas, int32_t splat);

// The floats of a gas's faces a gas force reads: each axis's array in
// turn, through the last axis of more than one cell.
uint32_t mvfxSimGasFaces(const mvfxGasView* view);

// The first float of a gas's current faces in its fields.
uint32_t mvfxSimGasFacesAt(const SimGas* gas);

#endif
