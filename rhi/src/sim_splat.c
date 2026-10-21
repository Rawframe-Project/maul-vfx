// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Emitters splatting into a smoke gas (splat.wgsl): for each, its
// particles after its step mapped into the box and sorted by cell (the
// record sort's kernels, sort.wgsl), each cell's run found, then each
// substep a gather into the cells and the faces after the sources.

#include "allocator.h"
#include "numbers.h"
#include "sim_gas.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"

#include <stdalign.h>
#include <string.h>

// The words a splat keeps: the sort's four arrays, its particles' places
// and velocities (six), then its cells' runs (two words a cell, the
// outside's last); a liquid's its particles' affine matrices besides
// (nine) and, after the runs, two sets of its faces' flags.
static uint32_t SortedWords(const SimSplat* splat, const mvfxGasView* view)
{
    uint32_t cells = (uint32_t)view->cells;
    uint32_t faces = (uint32_t)(view->faces[0] + view->faces[1] + view->faces[2]);
    return splat->liquid ? 19u * splat->span + 2u * (cells + 1u) + 2u * faces
                         : 10u * splat->span + 2u * (cells + 1u);
}

// Where a splat's runs start among its sorted words.
static uint64_t RunsAt(const SimSplat* splat)
{
    return (splat->liquid ? 19u : 10u) * (uint64_t)splat->span;
}

const SimSplat* mvfxSimSplatOf(const SimGas* gas, int32_t splat)
{
    return splat == LIQUID_SPLAT ? &gas->liquid : &gas->splats[splat];
}

// Splat s of a gas's making, its liquid's past its splats.
static SimSplat* SplatAt(SimGas* gas, int32_t s)
{
    return s < gas->splatCount ? &gas->splats[s] : &gas->liquid;
}

// Its splats, and its liquid when it has one.
static int32_t SplatsAndLiquid(const SimGas* gas)
{
    return gas->splatCount + (gas->hasLiquid ? 1 : 0);
}

// A loop's workgroups, at most a row's: the keys and the sort's scatter
// step over blocks by the workgroups dispatched, along X alone.
static uint32_t Capped(uint32_t groups)
{
    return groups < GAS_ROW ? (groups > 0u ? groups : 1u) : GAS_ROW;
}

static void Push(SimGas* gas, mvfxKernel kernel, const uint32_t root[4], uint32_t groups,
                 GasBinding binds, int32_t splat)
{
    gas->dispatches[gas->count++] = (GasDispatch){.kernel = kernel,
                                                  .root = {root[0], root[1], root[2], root[3]},
                                                  .groups = groups,
                                                  .binds = binds,
                                                  .splat = splat};
}

static bool CreateBuffer(mvfxGpuSim* sim, uint64_t size, mrhiBufferUsage usage, mrhiBufferId* out)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return mrhiCreateBuffer(sim->device, &def, out) == mrhi_success;
}

// Makes a splat's buffers for emitter e: its numbers, its sorted words,
// its sort's digit counts.
static bool MakeSplat(mvfxGpuSim* sim, const SimGas* gas, SimSplat* splat, int32_t e, bool liquid)
{
    *splat =
        (SimSplat){.emitter = e, .capacity = (uint32_t)sim->emitters[e].capacity, .liquid = liquid};
    uint32_t blocks = mvfxSortBlocks(sim->emitters[e].capacity);
    splat->span = blocks * MVFX_SORT_BLOCK;
    mrhiBufferUsage written = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    return CreateBuffer(sim, 4u * SPLAT_NUMBER_WORDS,
                        mrhi_bufferUniform | mrhi_bufferCopyDestination,
                        &splat->buffers[SPLAT_NUMBERS]) &&
           CreateBuffer(sim, 4u * (uint64_t)SortedWords(splat, &gas->view), written,
                        &splat->buffers[SPLAT_SORTED]) &&
           CreateBuffer(sim, 4u * 2u * 256u * (uint64_t)blocks, mrhi_bufferStorage,
                        &splat->buffers[SPLAT_DIGITS]);
}

bool mvfxCreateSimSplats(mvfxGpuSim* sim, SimGas* gas, int32_t g)
{
    // A liquid gas's emitter, with room, is its own; nothing splats into
    // a liquid, which the core steps without its pours.
    int32_t e = gas->view.emitter;
    gas->hasLiquid = e >= 0 && sim->emitters[e].capacity > 0;
    if (gas->hasLiquid && !MakeSplat(sim, gas, &gas->liquid, e, true))
    {
        return false;
    }
    int32_t count = 0;
    for (int32_t s = 0; e < 0 && s < sim->emitterCount; ++s)
    {
        count += sim->views[s].splatGas == g && sim->emitters[s].capacity > 0 ? 1 : 0;
    }
    gas->splatCount = 0;
    gas->splatRoom = count;
    gas->writes = mvfxRhiAllocate(&sim->allocator,
                                  (size_t)(GAS_BUFFERS + 2 * (count + 1)) * sizeof(mrhiAccess),
                                  alignof(mrhiAccess));
    if (gas->writes == nullptr)
    {
        return false;
    }
    if (count == 0)
    {
        return true;
    }
    gas->splats =
        mvfxRhiAllocate(&sim->allocator, (size_t)count * sizeof(SimSplat), alignof(SimSplat));
    bool created = gas->splats != nullptr;
    for (int32_t s = 0; created && s < sim->emitterCount; ++s)
    {
        if (sim->views[s].splatGas == g && sim->emitters[s].capacity > 0)
        {
            created = MakeSplat(sim, gas, &gas->splats[gas->splatCount++], s, false);
        }
    }
    return created;
}

void mvfxDestroySimSplats(mvfxGpuSim* sim, SimGas* gas)
{
    for (int32_t s = 0; s < SplatsAndLiquid(gas); ++s)
    {
        for (int32_t b = 0; b < 3; ++b)
        {
            if (SplatAt(gas, s)->buffers[b].index1 != 0)
            {
                (void)mrhiDestroyBuffer(sim->device, SplatAt(gas, s)->buffers[b]);
            }
        }
    }
    size_t room = (size_t)gas->splatRoom;
    mvfxRhiRelease(&sim->allocator, gas->splats, room * sizeof(SimSplat), alignof(SimSplat));
    mvfxRhiRelease(&sim->allocator, gas->writes,
                   (GAS_BUFFERS + 2u * (room + 1u)) * sizeof(mrhiAccess), alignof(mrhiAccess));
    gas->splatCount = 0;
    gas->splatRoom = 0;
    gas->hasLiquid = false;
}

int32_t mvfxSimSplatPasses(int32_t splats, int32_t substeps)
{
    // A step: the keys, four passes of the sort's two, the runs; a
    // substep: the cells and the faces, and the sources' faces apart.
    return splats > 0 ? splats * (10 + 2 * substeps) + substeps : 0;
}

void mvfxPlanSimSplats(const mvfxGpuSim* sim, SimGas* gas, const mvfxGasPlan* plan)
{
    for (int32_t s = 0; s < gas->splatCount; ++s)
    {
        SimSplat* splat = &gas->splats[s];
        const mvfxEmitterView* view = &sim->views[splat->emitter];
        const mvfxPlannedPour* pour = nullptr;
        for (int32_t p = 0; p < plan->pourCount; ++p)
        {
            pour = plan->pours[p].emitter == splat->emitter ? &plan->pours[p] : pour;
        }
        uint32_t blocks = splat->span / MVFX_SORT_BLOCK;
        const uint32_t head[4] = {splat->capacity, mvfxWordOf(view->arrays.position),
                                  mvfxWordOf(view->arrays.velocity), Capped(blocks)};
        memcpy(splat->words, head, sizeof head);
        static const mvfxPlannedPour none = {.rotation = {0, 0, 0, 1}};
        pour = pour != nullptr ? pour : &none;
        mvfxFloatWords(splat->words + 4, pour->origin[0], pour->origin[1], pour->origin[2],
                       pour->sigma);
        memcpy(splat->words + 8, pour->rotation, 4 * sizeof(float));
        mvfxFloatWords(splat->words + 12, pour->rates[0], pour->rates[1], pour->rates[2], 0.0f);
        splat->drag = pour->drag;
        const uint32_t keys[4] = {0};
        Push(gas, mvfx_kernelSplatKeys, keys, Capped(blocks), GAS_BINDS_SPLAT, s);
        for (uint32_t shift = 0; shift < 32u; shift += 8u)
        {
            const uint32_t root[4] = {shift, splat->capacity, Capped(blocks), 0};
            Push(gas, mvfx_kernelSortOffsets, root, 1, GAS_BINDS_SORT, s);
            Push(gas, mvfx_kernelSortScatter, root, Capped(blocks), GAS_BINDS_SORT, s);
        }
        Push(gas, mvfx_kernelSplatRuns, keys, (splat->capacity + GAS_GROUP - 1u) / GAS_GROUP,
             GAS_BINDS_SPLAT, s);
    }
}

void mvfxSimSplatSubstep(SimGas* gas, uint32_t u, float h, uint32_t now, uint32_t cells,
                         uint32_t faces)
{
    for (int32_t s = 0; s < gas->splatCount; ++s)
    {
        SimSplat* splat = &gas->splats[s];
        // The core's pull, 1 - e^(-drag h), with the part's exponential.
        float pull = 1.0f - (float)mvfxExpMinus((double)(splat->drag * h));
        memcpy(&splat->words[16u + u], &pull, sizeof pull);
        const uint32_t root[4] = {u, now, 0, 0};
        Push(gas, mvfx_kernelSplatCells, root, cells, GAS_BINDS_SPLAT, s);
        Push(gas, mvfx_kernelSplatFaces, root, faces, GAS_BINDS_SPLAT, s);
    }
}

bool mvfxImportSimSplats(mvfxGpuSim* sim, SimGas* gas)
{
    bool imported = true;
    for (int32_t s = 0; imported && s < SplatsAndLiquid(gas); ++s)
    {
        SimSplat* splat = SplatAt(gas, s);
        const Emitter* emitter = &sim->emitters[splat->emitter];
        // Its particles as its step leaves them: the other buffer once the
        // step is added.
        int32_t after = emitter->added ? 1 - emitter->current : emitter->current;
        const mrhiBufferId buffers[SPLAT_BUFFERS] = {splat->buffers[0], splat->buffers[1],
                                                     splat->buffers[2], emitter->particles[after],
                                                     emitter->statistics};
        for (int32_t b = 0; imported && b < SPLAT_BUFFERS; ++b)
        {
            imported =
                mrhiImportBuffer(sim->device, buffers[b], &splat->resources[b]) == mrhi_success;
        }
    }
    return imported;
}

int32_t mvfxSimSplatWrites(const SimGas* gas, mrhiAccess* accesses)
{
    int32_t count = 0;
    for (int32_t s = 0; s < SplatsAndLiquid(gas); ++s)
    {
        const mrhiResourceId* r = SplatAt((SimGas*)gas, s)->resources;
        accesses[count++] =
            (mrhiAccess){.resource = r[SPLAT_NUMBERS], .kind = mrhi_accessCopyDestination};
        accesses[count++] =
            (mrhiAccess){.resource = r[SPLAT_SORTED], .kind = mrhi_accessCopyDestination};
    }
    return count;
}

int32_t mvfxSimSplatAccesses(const SimGas* gas, const GasDispatch* d, const mrhiAccess* used,
                             mrhiAccess out[GAS_BUFFERS + SPLAT_BUFFERS])
{
    int32_t count = 0;
    if (d->binds != GAS_BINDS_SORT)
    {
        for (int32_t b = 0; b < GAS_BUFFERS; ++b)
        {
            out[count++] = used[b];
        }
    }
    if (d->binds == GAS_BINDS_GAS)
    {
        return count;
    }
    const mrhiResourceId* r = mvfxSimSplatOf(gas, d->splat)->resources;
    if (d->binds == GAS_BINDS_SPLAT || d->binds == GAS_BINDS_LIQUID)
    {
        // A liquid's read back writes its particles.
        out[count++] = (mrhiAccess){.resource = r[SPLAT_NUMBERS], .kind = mrhi_accessUniform};
        out[count++] =
            (mrhiAccess){.resource = r[SPLAT_PARTICLES],
                         .kind = d->binds == GAS_BINDS_LIQUID ? mrhi_accessStorageReadWrite
                                                              : mrhi_accessStorageRead};
    }
    out[count++] = (mrhiAccess){.resource = r[SPLAT_STATISTICS], .kind = mrhi_accessStorageRead};
    out[count++] = (mrhiAccess){.resource = r[SPLAT_SORTED], .kind = mrhi_accessStorageReadWrite};
    out[count++] = (mrhiAccess){.resource = r[SPLAT_DIGITS], .kind = mrhi_accessStorageReadWrite};
    return count;
}

bool mvfxWriteSimSplats(mvfxGpuSim* sim, mrhiPassId pass, SimGas* gas)
{
    uint32_t cells = (uint32_t)gas->view.cells;
    bool written = true;
    for (int32_t s = 0; written && s < SplatsAndLiquid(gas); ++s)
    {
        SimSplat* splat = SplatAt(gas, s);
        const mrhiResourceId* r = splat->resources;
        written = mvfxUpload(sim->device, &sim->uploaded, pass, r[SPLAT_NUMBERS], 0, splat->words,
                             sizeof splat->words) &&
                  mrhiClearBuffer(sim->device, pass, r[SPLAT_SORTED], 4u * RunsAt(splat),
                                  8u * ((uint64_t)cells + 1u)) == mrhi_success;
    }
    return written;
}

int32_t mvfxSimSplatBindings(const SimGas* gas, const GasDispatch* d,
                             mrhiBinding out[GAS_BUFFERS + 4])
{
    const mrhiResourceId* r = mvfxSimSplatOf(gas, d->splat)->resources;
    if (d->binds == GAS_BINDS_SORT)
    {
        const mrhiResourceId sort[3] = {r[SPLAT_STATISTICS], r[SPLAT_SORTED], r[SPLAT_DIGITS]};
        for (uint32_t b = 0; b < 3u; ++b)
        {
            out[b] = (mrhiBinding){.slot = b, .resource = sort[b], .size = MRHI_WHOLE_SIZE};
        }
        return 3;
    }
    // A liquid's binds the solve's levels and the gas's record besides
    // (liquid_gas.wgsl).
    const mrhiResourceId splat[9] = {gas->resources[GAS_NUMBERS],
                                     r[SPLAT_NUMBERS],
                                     gas->resources[GAS_FIELDS],
                                     r[SPLAT_PARTICLES],
                                     r[SPLAT_STATISTICS],
                                     r[SPLAT_SORTED],
                                     r[SPLAT_DIGITS],
                                     gas->resources[GAS_SOLVE],
                                     gas->resources[GAS_STATISTICS]};
    uint32_t count = d->binds == GAS_BINDS_LIQUID ? 9u : 7u;
    for (uint32_t b = 0; b < count; ++b)
    {
        out[b] = (mrhiBinding){.slot = b, .resource = splat[b], .size = MRHI_WHOLE_SIZE};
    }
    return (int32_t)count;
}
