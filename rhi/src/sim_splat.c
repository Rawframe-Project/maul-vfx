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
// outside's last).
static uint32_t SortedWords(const SimSplat* splat, uint32_t cells)
{
    return 10u * splat->span + 2u * (cells + 1u);
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

bool mvfxCreateSimSplats(mvfxGpuSim* sim, SimGas* gas, int32_t g)
{
    int32_t count = 0;
    for (int32_t e = 0; e < sim->emitterCount; ++e)
    {
        count += sim->views[e].splatGas == g && sim->emitters[e].capacity > 0 ? 1 : 0;
    }
    gas->splatCount = 0;
    gas->splatRoom = count;
    if (count == 0)
    {
        return true;
    }
    gas->splats =
        mvfxRhiAllocate(&sim->allocator, (size_t)count * sizeof(SimSplat), alignof(SimSplat));
    gas->writes =
        mvfxRhiAllocate(&sim->allocator, (size_t)(GAS_BUFFERS + 2 * count) * sizeof(mrhiAccess),
                        alignof(mrhiAccess));
    if (gas->splats == nullptr || gas->writes == nullptr)
    {
        return false;
    }
    uint32_t cells = (uint32_t)gas->view.cells;
    mrhiBufferUsage written = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    bool created = true;
    for (int32_t e = 0; created && e < sim->emitterCount; ++e)
    {
        if (sim->views[e].splatGas != g || sim->emitters[e].capacity == 0)
        {
            continue;
        }
        SimSplat* splat = &gas->splats[gas->splatCount++];
        *splat = (SimSplat){.emitter = e, .capacity = (uint32_t)sim->emitters[e].capacity};
        uint32_t blocks = mvfxSortBlocks(sim->emitters[e].capacity);
        splat->span = blocks * MVFX_SORT_BLOCK;
        created = CreateBuffer(sim, 4u * SPLAT_NUMBER_WORDS,
                               mrhi_bufferUniform | mrhi_bufferCopyDestination,
                               &splat->buffers[SPLAT_NUMBERS]) &&
                  CreateBuffer(sim, 4u * (uint64_t)SortedWords(splat, cells), written,
                               &splat->buffers[SPLAT_SORTED]) &&
                  CreateBuffer(sim, 4u * 2u * 256u * (uint64_t)blocks, mrhi_bufferStorage,
                               &splat->buffers[SPLAT_DIGITS]);
    }
    return created;
}

void mvfxDestroySimSplats(mvfxGpuSim* sim, SimGas* gas)
{
    for (int32_t s = 0; s < gas->splatCount; ++s)
    {
        for (int32_t b = 0; b < 3; ++b)
        {
            if (gas->splats[s].buffers[b].index1 != 0)
            {
                (void)mrhiDestroyBuffer(sim->device, gas->splats[s].buffers[b]);
            }
        }
    }
    size_t room = (size_t)gas->splatRoom;
    mvfxRhiRelease(&sim->allocator, gas->splats, room * sizeof(SimSplat), alignof(SimSplat));
    mvfxRhiRelease(&sim->allocator, gas->writes, (GAS_BUFFERS + 2u * room) * sizeof(mrhiAccess),
                   alignof(mrhiAccess));
    gas->splatCount = 0;
    gas->splatRoom = 0;
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
    for (int32_t s = 0; imported && s < gas->splatCount; ++s)
    {
        SimSplat* splat = &gas->splats[s];
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
    for (int32_t s = 0; s < gas->splatCount; ++s)
    {
        const mrhiResourceId* r = gas->splats[s].resources;
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
    const mrhiResourceId* r = gas->splats[d->splat].resources;
    if (d->binds == GAS_BINDS_SPLAT)
    {
        out[count++] = (mrhiAccess){.resource = r[SPLAT_NUMBERS], .kind = mrhi_accessUniform};
        out[count++] = (mrhiAccess){.resource = r[SPLAT_PARTICLES], .kind = mrhi_accessStorageRead};
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
    for (int32_t s = 0; written && s < gas->splatCount; ++s)
    {
        SimSplat* splat = &gas->splats[s];
        const mrhiResourceId* r = splat->resources;
        written = mvfxUpload(sim->device, &sim->uploaded, pass, r[SPLAT_NUMBERS], 0, splat->words,
                             sizeof splat->words) &&
                  mrhiClearBuffer(sim->device, pass, r[SPLAT_SORTED], 40u * (uint64_t)splat->span,
                                  8u * ((uint64_t)cells + 1u)) == mrhi_success;
    }
    return written;
}

int32_t mvfxSimSplatBindings(const SimGas* gas, const GasDispatch* d,
                             mrhiBinding out[GAS_BUFFERS + 2])
{
    const mrhiResourceId* r = gas->splats[d->splat].resources;
    if (d->binds == GAS_BINDS_SORT)
    {
        const mrhiResourceId sort[3] = {r[SPLAT_STATISTICS], r[SPLAT_SORTED], r[SPLAT_DIGITS]};
        for (uint32_t b = 0; b < 3u; ++b)
        {
            out[b] = (mrhiBinding){.slot = b, .resource = sort[b], .size = MRHI_WHOLE_SIZE};
        }
        return 3;
    }
    const mrhiResourceId splat[7] = {gas->resources[GAS_NUMBERS],
                                     r[SPLAT_NUMBERS],
                                     gas->resources[GAS_FIELDS],
                                     r[SPLAT_PARTICLES],
                                     r[SPLAT_STATISTICS],
                                     r[SPLAT_SORTED],
                                     r[SPLAT_DIGITS]};
    for (uint32_t b = 0; b < 7u; ++b)
    {
        out[b] = (mrhiBinding){.slot = b, .resource = splat[b], .size = MRHI_WHOLE_SIZE};
    }
    return 7;
}
