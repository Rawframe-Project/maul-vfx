// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The part's kernels: the containers that hold them, each kernel's entry,
// and the object's pipeline for it, which the part's passes and its tests
// set.

#ifndef MAUL_VFX_RHI_SRC_KERNELS_H
#define MAUL_VFX_RHI_SRC_KERNELS_H

#include "maul-rhi/pipeline.h"
#include "maul-vfx-rhi/gpu.h"

// The containers, a shader each.
typedef enum mvfxContainer
{
    mvfx_containerPcg4d = 0,
    mvfx_containerSim = 1,
    mvfx_containerRecords = 2,
    mvfx_containerSort = 3,
    mvfx_containerRibbonRecords = 4,
    mvfx_containerGrid = 5,
    mvfx_containerContacts = 6,
    mvfx_containerPlexus = 7,
    mvfx_containerSelf = 8,
    mvfx_containerGas = 9,
    mvfx_containerGasRead = 10,
    mvfx_containerSplat = 11,
    mvfx_containerLiquid = 12,
    mvfx_containerLiquidGas = 13,
    mvfx_containerWater = 14,
    mvfx_containerWaterRead = 15,
    mvfx_containerSurfaceGas = 16,
    mvfx_containerWaterSample = 17,
    mvfx_containerGasSample = 18,
    mvfx_containerSurfaceMesh = 19,
    MVFX_CONTAINER_COUNT
} mvfxContainer;

// The kernels, a pipeline each.
typedef enum mvfxKernel
{
    // pcg4d of each index, the words the CPU's must equal.
    mvfx_kernelPcg4d = 0,
    // An emitter's step (sim.wgsl): its live particles moved; which of
    // the plan's births have room, as the CPU path's full step decides;
    // the survivors and the births kept in order by a scan in three
    // passes; their box measured.
    mvfx_kernelUpdate = 1,
    mvfx_kernelRoom = 2,
    mvfx_kernelReduce = 3,
    mvfx_kernelOffsets = 4,
    mvfx_kernelScatter = 5,
    mvfx_kernelMeasure = 6,
    // Which births with room an attractor kills in their first step.
    mvfx_kernelKillBirths = 7,
    // An emitter's records in a layout of the host's (records.wgsl).
    mvfx_kernelRecords = 8,
    // A sort by place: each live particle's key (records.wgsl), then a
    // radix pass's offsets and scatter (sort.wgsl).
    mvfx_kernelKeys = 9,
    mvfx_kernelSortOffsets = 10,
    mvfx_kernelSortScatter = 11,
    // An emitter's events (sim.wgsl): each block's count of each group's,
    // their places, the events written.
    mvfx_kernelEventCount = 12,
    mvfx_kernelEventOffsets = 13,
    mvfx_kernelEventWrite = 14,
    // The events' colours and sizes with the values over life
    // (records.wgsl).
    mvfx_kernelEventValues = 15,
    // An emitter's births from another's events (sim.wgsl): the events
    // that spawn kept, sorted by tick, their births placed and bounded,
    // merged with the plan's.
    mvfx_kernelSpawnCount = 16,
    mvfx_kernelSpawnSort = 17,
    mvfx_kernelSpawnScan = 18,
    mvfx_kernelSpawnMerge = 19,
    // Which ribbon each birth joins and its distance along it.
    mvfx_kernelRibbons = 20,
    // An emitter's ribbons in a layout of the host's
    // (ribbon_records.wgsl): each live particle's ribbon id as a key for
    // the radix passes, the chains and their list, a record a point.
    mvfx_kernelRibbonKeys = 21,
    mvfx_kernelRibbonChains = 22,
    mvfx_kernelRibbonPoints = 23,
    // Each segment of ribbons of more pieces than one cut into pieces.
    mvfx_kernelRibbonPieces = 24,
    // A beam's points placed (sim.wgsl).
    mvfx_kernelBeam = 25,
    // An emitter's lights (records.wgsl): each block's carriers counted,
    // the counts scanned, the lights written.
    mvfx_kernelLightCount = 26,
    mvfx_kernelLightOffsets = 27,
    mvfx_kernelLightWrite = 28,
    // An emitter's particles expanded into quads (ribbon_records.wgsl).
    mvfx_kernelSpriteQuads = 29,
    // An emitter's neighbor grid (grid.wgsl): its keys, their values in
    // its order after sort.wgsl's passes, and its particles' neighbors.
    mvfx_kernelGridKeys = 30,
    mvfx_kernelGridValues = 31,
    mvfx_kernelNeighbors = 32,
    // An emitter's particle contacts (contacts.wgsl): their radii, keys
    // before sort.wgsl's passes, projections and their commits, impulses
    // and their kick.
    mvfx_kernelContactRadii = 33,
    mvfx_kernelContactKeys = 34,
    mvfx_kernelContactProject = 35,
    mvfx_kernelContactCommit = 36,
    mvfx_kernelContactImpulses = 37,
    mvfx_kernelContactKick = 38,
    // An emitter's blocks' boxes taken again after its contacts (sim.wgsl).
    mvfx_kernelRebox = 39,
    // An emitter's plexus links (plexus.wgsl): their keys before
    // sort.wgsl's passes, each particle's count, the slots, the links.
    mvfx_kernelPlexusKeys = 40,
    mvfx_kernelPlexusCount = 41,
    mvfx_kernelPlexusOffsets = 42,
    mvfx_kernelPlexusWrite = 43,
    // An emitter's units moved by substeps (sim.wgsl).
    mvfx_kernelUnits = 44,
    // An emitter's sheets expanded into meshes (ribbon_records.wgsl): the
    // sheets that fit and their quads' places, then their vertices and
    // indices.
    mvfx_kernelSheetFit = 45,
    mvfx_kernelSheetPoints = 46,
    // An emitter's units' self-collision (self.wgsl): the reach and the
    // first start places, the keys before sort.wgsl's passes, the
    // candidates, and each substep's moves and their commit.
    mvfx_kernelSelfReach = 47,
    mvfx_kernelSelfKeys = 48,
    mvfx_kernelSelfGather = 49,
    mvfx_kernelSelfPart = 50,
    mvfx_kernelSelfCommit = 51,
    // An emitter's rigid particles moved by substeps (sim.wgsl), and
    // those that meet each other, in one workgroup.
    mvfx_kernelRigid = 52,
    mvfx_kernelRigidPairs = 53,
    // A smoke gas's step (gas.wgsl): its levels' areas and coefficients
    // once; each substep's sources, faces, confinement, carries, burn and
    // expansion's mean; the projection's divergence, sweeps,
    // restrictions, prolongations and tail; the correction; the holds at
    // the step's end; the obstacles' solids marked, emptied and moving.
    mvfx_kernelGasAreas = 54,
    mvfx_kernelGasCoefficients = 55,
    mvfx_kernelGasPourCells = 56,
    mvfx_kernelGasFaces = 57,
    mvfx_kernelGasCurl = 58,
    mvfx_kernelGasConfine = 59,
    mvfx_kernelGasCarry = 60,
    mvfx_kernelGasCarryCells = 61,
    mvfx_kernelGasBurn = 62,
    mvfx_kernelGasMean = 63,
    mvfx_kernelGasDivergence = 64,
    mvfx_kernelGasJacobi = 65,
    mvfx_kernelGasRestrict = 66,
    mvfx_kernelGasProlong = 67,
    mvfx_kernelGasTail = 68,
    mvfx_kernelGasCorrect = 69,
    mvfx_kernelGasHolds = 70,
    mvfx_kernelGasMark = 71,
    mvfx_kernelGasEmpty = 72,
    mvfx_kernelGasMove = 73,
    // A smoke gas's field written in a host's format (gas_read.wgsl).
    mvfx_kernelGasRead = 74,
    // An emitter splatting into a smoke gas (splat.wgsl): its particles
    // mapped into the box and keyed by cell, each cell's run in the sorted
    // order, then each substep the gather into the cells and the faces.
    mvfx_kernelSplatKeys = 75,
    mvfx_kernelSplatRuns = 76,
    mvfx_kernelSplatCells = 77,
    mvfx_kernelSplatFaces = 78,
    // A particle liquid's sim kernel passes (sim/liquid.wgsl): each
    // substep's move, each iteration's slide along the colliders.
    mvfx_kernelLiquidMove = 79,
    mvfx_kernelLiquidSlide = 80,
    // A particle liquid's own passes (liquid.wgsl): the grid's keys, each
    // iteration's multipliers and corrections, the velocities' gain,
    // viscosity, the curls, confinement and the scratch velocities kept.
    mvfx_kernelLiquidKeys = 81,
    mvfx_kernelLiquidMultipliers = 82,
    mvfx_kernelLiquidCorrections = 83,
    mvfx_kernelLiquidGain = 84,
    mvfx_kernelLiquidViscosity = 85,
    mvfx_kernelLiquidCurls = 86,
    mvfx_kernelLiquidConfine = 87,
    mvfx_kernelLiquidKeep = 88,
    // A liquid gas's own passes about the projection (liquid_gas.wgsl):
    // its particles keyed by cell and their runs, the cells marked (the
    // finest, then a coarse level a pass), the faces gathered, those of
    // liquid kept, a layer of the extension, the walls, the read back.
    mvfx_kernelLiquidGasKeys = 89,
    mvfx_kernelLiquidGasRuns = 90,
    mvfx_kernelLiquidGasMark = 91,
    mvfx_kernelLiquidGasCoarse = 92,
    mvfx_kernelLiquidGasGather = 93,
    mvfx_kernelLiquidGasKeep = 94,
    mvfx_kernelLiquidGasExtend = 95,
    mvfx_kernelLiquidGasWalls = 96,
    mvfx_kernelLiquidGasRead = 97,
    // A waves surface's step (water.wgsl): the pushes' sinks and the even
    // lift, a substep whole, the settle into the statistics record.
    mvfx_kernelWaterSink = 98,
    mvfx_kernelWaterSubstep = 99,
    mvfx_kernelWaterSettle = 100,
    // A surface's field written into a host's buffer (water_read.wgsl).
    mvfx_kernelWaterRead = 101,
    // A shallow surface's step (water.wgsl): its ground, fill and sources,
    // its pushes, a substep's faces, givers' scales and depths, its
    // settle.
    mvfx_kernelShallowStart = 102,
    mvfx_kernelShallowSpill = 103,
    mvfx_kernelShallowFaces = 104,
    mvfx_kernelShallowGiving = 105,
    mvfx_kernelShallowDepths = 106,
    mvfx_kernelShallowSettle = 107,
    // What a surface of waves' crossings sink, summed for the even lift
    // (water.wgsl).
    mvfx_kernelWaterCrossSum = 108,
    // A surface gas's passes (surface_gas.wgsl): its particles keyed by
    // bucket and their runs, each cell's distance, a smoothing pass.
    mvfx_kernelSurfaceKeys = 109,
    mvfx_kernelSurfaceRuns = 110,
    mvfx_kernelSurfaceField = 111,
    mvfx_kernelSurfaceSmooth = 112,
    // A surface sampled at a host's points (water_sample.wgsl).
    mvfx_kernelWaterSample = 113,
    // A gas's field sampled at a host's points (gas_sample.wgsl).
    mvfx_kernelGasSample = 114,
    // A surface's mesh (surface_mesh.wgsl): each block's counts, their
    // offsets, the vertices, the first refusal, the indices.
    mvfx_kernelMeshCount = 115,
    mvfx_kernelMeshOffsets = 116,
    mvfx_kernelMeshVertices = 117,
    mvfx_kernelMeshRefuse = 118,
    mvfx_kernelMeshIndices = 119,
    MVFX_KERNEL_COUNT
} mvfxKernel;

// The device an object runs on.
mrhiDevice* mvfxGpuDevice(const mvfxGpu* gpu);

// A ready object's pipeline for a kernel.
mrhiComputePipelineId mvfxGpuPipeline(const mvfxGpu* gpu, mvfxKernel kernel);

#endif
