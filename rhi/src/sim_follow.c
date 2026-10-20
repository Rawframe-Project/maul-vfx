// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Emitters following gases: each gas an emitter's forces follow has a
// region of the emitter's grids, which its current faces are copied to
// each step (or cleared after a clear), the shared force code sampling
// them there as the core's SampleGas does.

#include "grids.h"
#include "sim_state.h"

#include "maul-rhi/encoder.h"

#include <string.h>

void mvfxLaySimFollow(mvfxGpuSim* sim, int32_t e)
{
    Emitter* emitter = &sim->emitters[e];
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

void mvfxSimFollowSources(const mvfxGpuSim* sim, Emitter* emitter)
{
    // A gas without a step recorded yet holds nothing defined.
    for (int32_t g = 0; g < sim->gasCount; ++g)
    {
        const SimGas* gas = &sim->gases[g];
        emitter->gasSources[g] =
            emitter->cleared || !gas->prepared ? UINT32_MAX : mvfxSimGasFacesAt(gas);
    }
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
    return true;
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
    return copied;
}
