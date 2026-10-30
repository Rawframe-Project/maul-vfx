// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a host sizes its device and its simulation by: the passes an
// emitter's step adds at most (its own, sim.c's PASS_*, then those of each
// part it has) and the bytes the instance's colliders' grids take.

#include "grids.h"
#include "sim_state.h"

int32_t mvfxGpuSim_GetEmitterPasses(const mvfxGpuSim* sim, int32_t emitter)
{
    if (sim == nullptr || emitter < 0 || emitter >= sim->emitterCount)
    {
        return 0;
    }
    const Emitter* e = &sim->emitters[emitter];
    if (e->capacity == 0)
    {
        return 0;
    }
    return PASS_COUNT + (e->gridFlags != 0 ? GRID_PASSES : 0) + (e->contacts ? CONTACT_PASSES : 0) +
           (e->plexus ? PLEXUS_PASSES : 0) + (e->selfCollides ? SELF_PASSES : 0) +
           (e->writesEvents ? 1 : 0) + e->liquidPassCount;
}

mvfxResult mvfxContext_GetGpuHostGridBytes(const mvfxContext* context,
                                           const mvfxCollider* colliders, int32_t count,
                                           int64_t* bytesOut)
{
    if (bytesOut != nullptr)
    {
        *bytesOut = 0;
    }
    if (context == nullptr || bytesOut == nullptr || count < 0 ||
        (colliders == nullptr && count > 0))
    {
        return mvfx_errorInvalid;
    }
    if (count > MVFX_MAX_COLLIDERS)
    {
        return mvfx_errorLimit;
    }
    return mvfxHostGridBytes(context, colliders, count, bytesOut) ? mvfx_success : mvfx_errorStale;
}
