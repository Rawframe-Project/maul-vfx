// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The steps of a frame that splits (an effect with a liquid gas whose
// courant number cuts the instance's step into substeps,
// mvfxInstance_GetPlanSteps): each emitter and gas a lane a step, the
// first the simulation's own. A lane is an Emitter or a SimGas whole,
// sharing the first's device buffers and holding its own numbers,
// births and pass ids; the parts' host arrays of pass ids and dispatches
// are a lane's own too, kept in a table by step and put into the lane
// swapped in. Each step's lanes are swapped into the simulation's
// emitters and gases while the step is added and while it is recorded,
// so every part reads the step's emitters and gases as it reads one
// step's. Between frames the simulation's hold the first step's arrays.

#include "sim_gas.h"
#include "sim_state.h"

#include <string.h>

// An emitter lane's own host arrays.
typedef struct EmitterArrays
{
    mrhiPassId* liquidPasses;
} EmitterArrays;

// A gas lane's own host arrays.
typedef struct GasArrays
{
    GasDispatch* dispatches;
    mrhiPassId* passes;
    mrhiAccess* writes;
    SimSplat* splats;
} GasArrays;

struct SimLanes
{
    int32_t emitterCount;
    int32_t gasCount;
    // Each step's lanes as added (step s, emitter or gas i at
    // s * count + i), and each step's own arrays (the first step's the
    // ones the simulation made).
    Emitter* emitters;
    SimGas* gases;
    EmitterArrays* emitterArrays;
    GasArrays* gasArrays;
    // Whether a frame of more steps was added and not yet recorded.
    bool pending;
};

static size_t LiquidBytes(const Emitter* emitter)
{
    return (size_t)emitter->liquidPassCount * sizeof(mrhiPassId);
}

static size_t WritesBytes(const SimGas* gas)
{
    return (size_t)(GAS_BUFFERS + 2 * (gas->splatRoom + 1)) * sizeof(mrhiAccess);
}

// Whether the simulation's effect can split its steps: it has a liquid
// gas.
static bool Splits(const mvfxGpuSim* sim)
{
    for (int32_t g = 0; g < sim->gasCount; ++g)
    {
        if (sim->gases[g].view.def.kind == mvfx_gasLiquid)
        {
            return true;
        }
    }
    return false;
}

// Makes step s's own arrays for each emitter and gas, sized as the
// simulation's.
static bool MakeArrays(mvfxGpuSim* sim, SimLanes* lanes, int32_t s)
{
    bool made = true;
    for (int32_t e = 0; made && e < lanes->emitterCount; ++e)
    {
        const Emitter* emitter = &sim->emitters[e];
        EmitterArrays* own = &lanes->emitterArrays[s * lanes->emitterCount + e];
        own->liquidPasses = emitter->liquid ? mvfxRhiAllocate(&sim->allocator, LiquidBytes(emitter),
                                                              alignof(mrhiPassId))
                                            : nullptr;
        made = !emitter->liquid || own->liquidPasses != nullptr;
    }
    for (int32_t g = 0; made && g < lanes->gasCount; ++g)
    {
        const SimGas* gas = &sim->gases[g];
        GasArrays* own = &lanes->gasArrays[s * lanes->gasCount + g];
        size_t count = (size_t)gas->capacity;
        own->dispatches =
            mvfxRhiAllocate(&sim->allocator, count * sizeof(GasDispatch), alignof(GasDispatch));
        own->passes =
            mvfxRhiAllocate(&sim->allocator, count * sizeof(mrhiPassId), alignof(mrhiPassId));
        own->writes = mvfxRhiAllocate(&sim->allocator, WritesBytes(gas), alignof(mrhiAccess));
        own->splats =
            gas->splatRoom > 0
                ? mvfxRhiAllocate(&sim->allocator, (size_t)gas->splatRoom * sizeof(SimSplat),
                                  alignof(SimSplat))
                : nullptr;
        made = own->dispatches != nullptr && own->passes != nullptr && own->writes != nullptr &&
               (gas->splatRoom == 0 || own->splats != nullptr);
    }
    return made;
}

// Releases step s's own arrays (none for the first, the simulation's).
static void ReleaseArrays(mvfxGpuSim* sim, SimLanes* lanes, int32_t s)
{
    for (int32_t e = 0; e < lanes->emitterCount; ++e)
    {
        mvfxRhiRelease(&sim->allocator,
                       lanes->emitterArrays[s * lanes->emitterCount + e].liquidPasses,
                       LiquidBytes(&sim->emitters[e]), alignof(mrhiPassId));
    }
    for (int32_t g = 0; g < lanes->gasCount; ++g)
    {
        const SimGas* gas = &sim->gases[g];
        GasArrays* own = &lanes->gasArrays[s * lanes->gasCount + g];
        size_t count = (size_t)gas->capacity;
        mvfxRhiRelease(&sim->allocator, own->dispatches, count * sizeof(GasDispatch),
                       alignof(GasDispatch));
        mvfxRhiRelease(&sim->allocator, own->passes, count * sizeof(mrhiPassId),
                       alignof(mrhiPassId));
        mvfxRhiRelease(&sim->allocator, own->writes, WritesBytes(gas), alignof(mrhiAccess));
        mvfxRhiRelease(&sim->allocator, own->splats, (size_t)gas->splatRoom * sizeof(SimSplat),
                       alignof(SimSplat));
    }
}

// Puts step s's own arrays into the simulation's emitters and gases.
static void TakeArrays(mvfxGpuSim* sim, const SimLanes* lanes, int32_t s)
{
    for (int32_t e = 0; e < lanes->emitterCount; ++e)
    {
        sim->emitters[e].liquidPasses =
            lanes->emitterArrays[s * lanes->emitterCount + e].liquidPasses;
    }
    for (int32_t g = 0; g < lanes->gasCount; ++g)
    {
        const GasArrays* own = &lanes->gasArrays[s * lanes->gasCount + g];
        SimGas* gas = &sim->gases[g];
        gas->dispatches = own->dispatches;
        gas->passes = own->passes;
        gas->writes = own->writes;
        gas->splats = own->splats;
    }
}

bool mvfxCreateSimLanes(mvfxGpuSim* sim)
{
    sim->lanes = nullptr;
    if (!Splits(sim))
    {
        return true;
    }
    SimLanes* lanes = mvfxRhiAllocate(&sim->allocator, sizeof *lanes, alignof(SimLanes));
    if (lanes == nullptr)
    {
        return false;
    }
    *lanes = (SimLanes){.emitterCount = sim->emitterCount, .gasCount = sim->gasCount};
    sim->lanes = lanes;
    size_t steps = MVFX_MAX_PLAN_STEPS;
    lanes->emitters = mvfxRhiAllocate(
        &sim->allocator, steps * (size_t)lanes->emitterCount * sizeof(Emitter), alignof(Emitter));
    lanes->gases = mvfxRhiAllocate(
        &sim->allocator, steps * (size_t)lanes->gasCount * sizeof(SimGas), alignof(SimGas));
    lanes->emitterArrays = mvfxRhiAllocate(
        &sim->allocator, steps * (size_t)lanes->emitterCount * sizeof(EmitterArrays),
        alignof(EmitterArrays));
    lanes->gasArrays = mvfxRhiAllocate(
        &sim->allocator, steps * (size_t)lanes->gasCount * sizeof(GasArrays), alignof(GasArrays));
    // The arrays' tables zeroed, the first step's the simulation's, before
    // anything can fail: the destruction reads them whenever both are.
    if (lanes->emitterArrays != nullptr)
    {
        memset(lanes->emitterArrays, 0,
               steps * (size_t)lanes->emitterCount * sizeof(EmitterArrays));
        for (int32_t e = 0; e < lanes->emitterCount; ++e)
        {
            lanes->emitterArrays[e].liquidPasses = sim->emitters[e].liquidPasses;
        }
    }
    if (lanes->gasArrays != nullptr)
    {
        memset(lanes->gasArrays, 0, steps * (size_t)lanes->gasCount * sizeof(GasArrays));
        for (int32_t g = 0; g < lanes->gasCount; ++g)
        {
            const SimGas* gas = &sim->gases[g];
            lanes->gasArrays[g] = (GasArrays){.dispatches = gas->dispatches,
                                              .passes = gas->passes,
                                              .writes = gas->writes,
                                              .splats = gas->splats};
        }
    }
    if (lanes->emitters == nullptr || lanes->gases == nullptr || lanes->emitterArrays == nullptr ||
        lanes->gasArrays == nullptr)
    {
        return false;
    }
    bool made = true;
    for (int32_t s = 1; made && s < MVFX_MAX_PLAN_STEPS; ++s)
    {
        made = MakeArrays(sim, lanes, s);
    }
    return made;
}

void mvfxDestroySimLanes(mvfxGpuSim* sim)
{
    SimLanes* lanes = sim->lanes;
    if (lanes == nullptr)
    {
        return;
    }
    size_t steps = MVFX_MAX_PLAN_STEPS;
    if (lanes->emitterArrays != nullptr && lanes->gasArrays != nullptr)
    {
        // The simulation's own arrays back, for its destruction to release.
        TakeArrays(sim, lanes, 0);
        for (int32_t s = 1; s < MVFX_MAX_PLAN_STEPS; ++s)
        {
            ReleaseArrays(sim, lanes, s);
        }
    }
    mvfxRhiRelease(&sim->allocator, lanes->emitters,
                   steps * (size_t)lanes->emitterCount * sizeof(Emitter), alignof(Emitter));
    mvfxRhiRelease(&sim->allocator, lanes->gases, steps * (size_t)lanes->gasCount * sizeof(SimGas),
                   alignof(SimGas));
    mvfxRhiRelease(&sim->allocator, lanes->emitterArrays,
                   steps * (size_t)lanes->emitterCount * sizeof(EmitterArrays),
                   alignof(EmitterArrays));
    mvfxRhiRelease(&sim->allocator, lanes->gasArrays,
                   steps * (size_t)lanes->gasCount * sizeof(GasArrays), alignof(GasArrays));
    mvfxRhiRelease(&sim->allocator, lanes, sizeof *lanes, alignof(SimLanes));
    sim->lanes = nullptr;
}

void mvfxBeginSimSteps(mvfxGpuSim* sim)
{
    SimLanes* lanes = sim->lanes;
    if (lanes == nullptr || !lanes->pending)
    {
        return;
    }
    // A frame of more steps the host dropped: back to its first step as it
    // began, its buffers flipped by no step.
    for (int32_t e = 0; e < lanes->emitterCount; ++e)
    {
        sim->emitters[e] = lanes->emitters[e];
    }
    for (int32_t g = 0; g < lanes->gasCount; ++g)
    {
        sim->gases[g] = lanes->gases[g];
    }
    TakeArrays(sim, lanes, 0);
    lanes->pending = false;
}

bool mvfxEnterSimStep(mvfxGpuSim* sim, int32_t step)
{
    SimLanes* lanes = sim->lanes;
    if (lanes == nullptr || step < 1 || step >= MVFX_MAX_PLAN_STEPS)
    {
        return false;
    }
    // The step before kept as added; this one goes on from it, its
    // particles where that one's step leaves them.
    for (int32_t e = 0; e < lanes->emitterCount; ++e)
    {
        Emitter* emitter = &sim->emitters[e];
        lanes->emitters[(step - 1) * lanes->emitterCount + e] = *emitter;
        emitter->current = emitter->added ? 1 - emitter->current : emitter->current;
        emitter->added = false;
    }
    for (int32_t g = 0; g < lanes->gasCount; ++g)
    {
        SimGas* gas = &sim->gases[g];
        lanes->gases[(step - 1) * lanes->gasCount + g] = *gas;
        GasArrays* own = &lanes->gasArrays[step * lanes->gasCount + g];
        if (gas->splatCount > 0)
        {
            memcpy(own->splats, gas->splats, (size_t)gas->splatCount * sizeof(SimSplat));
        }
    }
    TakeArrays(sim, lanes, step);
    lanes->pending = true;
    return true;
}

void mvfxStoreSimSteps(mvfxGpuSim* sim, int32_t steps)
{
    SimLanes* lanes = sim->lanes;
    if (lanes == nullptr || steps < 2)
    {
        return;
    }
    int32_t last = steps - 1;
    for (int32_t e = 0; e < lanes->emitterCount; ++e)
    {
        lanes->emitters[last * lanes->emitterCount + e] = sim->emitters[e];
    }
    for (int32_t g = 0; g < lanes->gasCount; ++g)
    {
        lanes->gases[last * lanes->gasCount + g] = sim->gases[g];
    }
}

void mvfxRecallSimStep(mvfxGpuSim* sim, int32_t step)
{
    SimLanes* lanes = sim->lanes;
    for (int32_t e = 0; e < lanes->emitterCount; ++e)
    {
        sim->emitters[e] = lanes->emitters[step * lanes->emitterCount + e];
    }
    for (int32_t g = 0; g < lanes->gasCount; ++g)
    {
        sim->gases[g] = lanes->gases[step * lanes->gasCount + g];
    }
    TakeArrays(sim, lanes, step);
}

void mvfxEndSimSteps(mvfxGpuSim* sim)
{
    SimLanes* lanes = sim->lanes;
    if (lanes == nullptr || !lanes->pending)
    {
        return;
    }
    // The last step's state goes on into the next frame, with the first
    // step's arrays.
    TakeArrays(sim, lanes, 0);
    lanes->pending = false;
}

// The step whose plan the simulation reads: the one added, or the last
// the instance's plans hold when they hold fewer.
static int32_t PlanStep(const mvfxGpuSim* sim, mvfxResult* result)
{
    int32_t count = 0;
    *result = mvfxInstance_GetPlanSteps(sim->context, sim->instance, &count);
    return count > 0 && sim->step >= count ? count - 1 : sim->step;
}

mvfxResult mvfxSimEmitterPlan(const mvfxGpuSim* sim, int32_t e, mvfxEmitterPlan* plan)
{
    mvfxResult result = mvfx_success;
    int32_t step = PlanStep(sim, &result);
    return result == mvfx_success
               ? mvfxInstance_GetEmitterStepPlan(sim->context, sim->instance, e, step, plan)
               : result;
}

mvfxResult mvfxSimGasPlan(const mvfxGpuSim* sim, int32_t g, mvfxGasPlan* plan)
{
    mvfxResult result = mvfx_success;
    int32_t step = PlanStep(sim, &result);
    return result == mvfx_success
               ? mvfxInstance_GetGasStepPlan(sim->context, sim->instance, g, step, plan)
               : result;
}
