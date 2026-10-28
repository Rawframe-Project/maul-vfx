// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a frame of the simulation binds and places before its steps: the
// host's depth and normal textures, or its own; every emitter's grids of
// the instance's colliders and its mesh.

#include "grids.h"
#include "sim_state.h"

#include "maul-rhi/frame.h"
#include "maul-vfx-rhi/sim.h"

mvfxResult mvfxGpuSim_SetDepthTextures(mvfxGpuSim* sim, mrhiResourceId depth,
                                       mrhiResourceId normals)
{
    if (sim == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (sim->ending)
    {
        return mvfx_errorBusy;
    }
    sim->hostDepth = depth;
    sim->hostNormals = depth.index1 != 0 ? normals : (mrhiResourceId){0};
    return mvfx_success;
}

// Brings the depth and normal textures the step binds into the frame: the
// host's, or the simulation's own.
static bool BindTextures(mvfxGpuSim* sim)
{
    const mrhiResourceId host[2] = {sim->hostDepth, sim->hostNormals};
    const mrhiTextureId own[2] = {sim->ownDepth, sim->ownNormals};
    bool bound = true;
    for (int32_t t = 0; bound && t < 2; ++t)
    {
        sim->bound[t] = host[t];
        bound = host[t].index1 != 0 ||
                mrhiImportTexture(sim->device, own[t], &sim->bound[t]) == mrhi_success;
    }
    return bound;
}

// Places every emitter's grids of the instance's colliders for the step,
// clearing the passes a frame the host dropped left behind.
static mvfxResult PlaceGrids(mvfxGpuSim* sim)
{
    mvfxEmitterPlan plan;
    for (int32_t e = 0; e < sim->emitterCount; ++e)
    {
        Emitter* emitter = &sim->emitters[e];
        emitter->added = false;
        emitter->records.count = 0;
        mvfxResult result = mvfxSimEmitterPlan(sim, e, &plan);
        if (result != mvfx_success)
        {
            return result;
        }
        if (emitter->capacity > 0 && plan.depth.enabled && sim->hostDepth.index1 == 0)
        {
            return mvfx_errorState;
        }
        if (emitter->capacity > 0 &&
            !mvfxPlaceHostGrids(sim->context, plan.colliders, plan.colliderCount,
                                plan.ownColliderCount, &emitter->gridLayout))
        {
            return mvfx_errorCapacity;
        }
        result = emitter->meshImage != nullptr ? mvfxPlaceSimMesh(sim, e) : mvfx_success;
        if (result != mvfx_success)
        {
            return result;
        }
    }
    return mvfx_success;
}

mvfxResult mvfxPrepareSimFrame(mvfxGpuSim* sim)
{
    // The instance's colliders change at any step: their grids go in the
    // room the def sets aside, every emitter's placed before a pass is
    // added, so a step refused adds none.
    mvfxResult result = PlaceGrids(sim);
    return result == mvfx_success && !BindTextures(sim) ? mvfx_errorState : result;
}
