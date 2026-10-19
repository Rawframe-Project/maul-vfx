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
    MVFX_KERNEL_COUNT
} mvfxKernel;

// The device an object runs on.
mrhiDevice* mvfxGpuDevice(const mvfxGpu* gpu);

// A ready object's pipeline for a kernel.
mrhiComputePipelineId mvfxGpuPipeline(const mvfxGpu* gpu, mvfxKernel kernel);

#endif
