// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Emitters following gases and water surfaces: each gas an emitter's
// forces follow, and the surface it is coupled to, has a region of the
// emitter's grids, which its current faces or fields are copied to each
// step (or cleared after a clear), the shared force code sampling a gas's
// there as the core's SampleGas does, sim/water.wgsl a surface's as the
// core's water_couple.c does.

#include "grids.h"
#include "sim_state.h"
#include "upload.h"

#include "maul-rhi/encoder.h"

#include <string.h>

// The floats of a surface's fields an emitter copies.
static uint32_t WaterFloats(const SimWater* water)
{
    uint32_t firsts[WATER_COPIES];
    uint32_t floats[WATER_COPIES];
    mvfxSimWaterFields(water, 0, firsts, floats);
    uint32_t sum = 0;
    for (int32_t k = 0; k < WATER_COPIES; ++k)
    {
        sum += floats[k];
    }
    return sum;
}

void mvfxLaySimFollow(mvfxGpuSim* sim, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
    const mvfxWaterCoupling* coupling = &sim->views[e].water;
    int32_t water = coupling->enabled ? coupling->water : -1;
    emitter->water = water;
    // None until its events buffer has them (mvfxCreateSimEvents).
    emitter->crossingFacts = UINT32_MAX;
    emitter->waterRoom = water >= 0
                             ? mvfxAddRoom(&emitter->gridLayout, WaterFloats(&sim->waters[water]))
                             : UINT32_MAX;
    for (int32_t g = 0; g < MVFX_MAX_GASES; ++g)
    {
        emitter->gasRooms[g] = UINT32_MAX;
    }
    for (int32_t f = 0; f < MVFX_MAX_FORCES; ++f)
    {
        int32_t g = sim->views[e].forceGases[f];
        if (g >= 0 && emitter->gasRooms[g] == UINT32_MAX)
        {
            emitter->gasRooms[g] =
                mvfxAddRoom(&emitter->gridLayout, mvfxSimGasFaces(&sim->gases[g].view));
        }
    }
}

void mvfxSimFollowGrids(const mvfxGpuSim* sim, int32_t e, uint32_t forceGrids[][5])
{
    for (int32_t f = 0; f < MVFX_MAX_FORCES; ++f)
    {
        int32_t g = sim->views[e].forceGases[f];
        if (g >= 0)
        {
            const int32_t* n = sim->gases[g].view.def.size;
            const uint32_t words[5] = {sim->emitters[e].gasRooms[g], (uint32_t)n[0], (uint32_t)n[1],
                                       (uint32_t)n[2], 0};
            memcpy(forceGrids[f], words, sizeof words);
        }
    }
}

void mvfxSimFollowWater(const mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan,
                        uint32_t words[MVFX_WATER_COUPLING])
{
    const Emitter* emitter = &sim->emitters[e];
    memset(words, 0, MVFX_WATER_COUPLING * sizeof(uint32_t));
    words[0] = emitter->waterRoom;
    words[22] = emitter->crossingFacts;
    if (emitter->water < 0)
    {
        return;
    }
    const mvfxPlannedWaterCoupling* c = &plan->water;
    const mvfxWaterDef* def = &sim->waters[emitter->water].view.def;
    words[1] = (uint32_t)def->size[0];
    words[2] = (uint32_t)def->size[1];
    words[3] = def->model == mvfx_waterShallow ? 1u : 0u;
    mvfxFloatWords(words + 4, c->origin[0], c->origin[1], c->origin[2], c->sigma);
    memcpy(words + 8, c->rotation, 4 * sizeof(float));
    mvfxFloatWords(words + 12, c->up[0], c->up[1], c->up[2], c->radius);
    mvfxFloatWords(words + 16, c->strength, c->drag, c->buoyancy, def->cellSize);
    memcpy(words + 20, &c->push, sizeof(float));
    words[21] = c->pour ? 1u : 0u;
}

void mvfxSimFollowSources(const mvfxGpuSim* sim, Emitter* emitter)
{
    // A surface without a step yet holds nothing defined.
    if (emitter->water >= 0)
    {
        const SimWater* water = &sim->waters[emitter->water];
        uint32_t floats[WATER_COPIES];
        mvfxSimWaterFields(water, water->now, emitter->waterSources, floats);
        bool undefined = emitter->cleared || !water->stepped;
        for (int32_t k = 0; undefined && k < WATER_COPIES; ++k)
        {
            emitter->waterSources[k] = UINT32_MAX;
        }
    }
    // A gas without a step recorded yet holds nothing defined.
    for (int32_t g = 0; g < sim->gasCount; ++g)
    {
        const SimGas* gas = &sim->gases[g];
        emitter->gasSources[g] =
            emitter->cleared || !gas->prepared ? UINT32_MAX : mvfxSimGasFacesAt(gas);
    }
}

bool mvfxWriteSimFollow(mvfxGpuSim* sim, mrhiPassId pass, mrhiResourceId step,
                        const Emitter* emitter)
{
    // The coupling's words, which move with the instance.
    return emitter->water < 0 ||
           mvfxUpload(sim->device, &sim->uploaded, pass, step, MVFX_WATER_HEAD * 4u,
                      emitter->words + MVFX_WATER_HEAD, MVFX_WATER_COUPLING * 4u);
}

bool mvfxImportSimFollow(mvfxGpuSim* sim, const Emitter* emitter, mrhiAccess* accesses,
                         int32_t* count)
{
    for (int32_t g = 0; g < sim->gasCount; ++g)
    {
        SimGas* gas = &sim->gases[g];
        if (emitter->gasRooms[g] == UINT32_MAX)
        {
            continue;
        }
        if (mrhiImportBuffer(sim->device, gas->buffers[GAS_FIELDS], &gas->resources[GAS_FIELDS]) !=
            mrhi_success)
        {
            return false;
        }
        accesses[*count] =
            (mrhiAccess){.resource = gas->resources[GAS_FIELDS], .kind = mrhi_accessCopySource};
        *count += 1;
    }
    if (emitter->water >= 0)
    {
        SimWater* water = &sim->waters[emitter->water];
        if (mrhiImportBuffer(sim->device, water->buffers[WATER_FIELDS],
                             &water->resources[WATER_FIELDS]) != mrhi_success)
        {
            return false;
        }
        accesses[*count] =
            (mrhiAccess){.resource = water->resources[WATER_FIELDS], .kind = mrhi_accessCopySource};
        *count += 1;
    }
    return true;
}

// Records the copies of a coupled emitter's surface's fields, or its
// region cleared.
static bool CopyWater(mvfxGpuSim* sim, mrhiPassId pass, const Emitter* emitter)
{
    const SimWater* water = &sim->waters[emitter->water];
    mrhiResourceId grids = emitter->resources[BUFFER_GRIDS];
    uint32_t firsts[WATER_COPIES];
    uint32_t floats[WATER_COPIES];
    mvfxSimWaterFields(water, 0, firsts, floats);
    uint64_t room = (uint64_t)emitter->waterRoom * 4u;
    bool copied = true;
    for (int32_t k = 0; copied && k < WATER_COPIES; ++k)
    {
        uint64_t bytes = (uint64_t)floats[k] * 4u;
        uint32_t from = emitter->waterSources[k];
        mrhiResult result = bytes == 0 ? mrhi_success
                            : from == UINT32_MAX
                                ? mrhiClearBuffer(sim->device, pass, grids, room, bytes)
                                : mrhiCopyBuffer(sim->device, pass, water->resources[WATER_FIELDS],
                                                 (uint64_t)from * 4u, grids, room, bytes);
        copied = result == mrhi_success;
        room += bytes;
    }
    return copied;
}

bool mvfxCopySimFollow(mvfxGpuSim* sim, mrhiPassId pass, const Emitter* emitter)
{
    mrhiResourceId grids = emitter->resources[BUFFER_GRIDS];
    bool copied = true;
    for (int32_t g = 0; copied && g < sim->gasCount; ++g)
    {
        const SimGas* gas = &sim->gases[g];
        if (emitter->gasRooms[g] == UINT32_MAX)
        {
            continue;
        }
        uint64_t room = (uint64_t)emitter->gasRooms[g] * 4u;
        uint64_t bytes = (uint64_t)mvfxSimGasFaces(&gas->view) * 4u;
        mrhiResult result =
            emitter->gasSources[g] == UINT32_MAX
                ? mrhiClearBuffer(sim->device, pass, grids, room, bytes)
                : mrhiCopyBuffer(sim->device, pass, gas->resources[GAS_FIELDS],
                                 (uint64_t)emitter->gasSources[g] * 4u, grids, room, bytes);
        copied = result == mrhi_success;
    }
    return copied && (emitter->water < 0 || CopyWater(sim, pass, emitter));
}
