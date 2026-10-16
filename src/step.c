// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public step calls, and the task over instance slots.

#include "step.h"

#include "emitter_step.h"
#include "event.h"
#include "float_mode.h"
#include "force.h"
#include "gas_api.h"
#include "gas_liquid.h"
#include "gas_step.h"
#include "instance.h"
#include "instance_api.h"
#include "tasks.h"

#include <math.h>

// Instance slots one task range takes at least.
#define STEP_MIN_RANGE 4

// What every range of a step reads.
typedef struct StepWork
{
    mvfxSlots* instances;
    int64_t ticks;
} StepWork;

// Moves a world-space emitter's anchor to the instance when the instance
// has gone MVFX_REBASE_DISTANCE from it, shifting its particles' offsets
// by the difference once.
static void Rebase(mvfxInstance* instance, int32_t emitter)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    const double* origin = instance->to.origin;
    double square = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double d = origin[k] - state->anchor[k];
        square += d * d;
    }
    if (!(square > MVFX_REBASE_DISTANCE * MVFX_REBASE_DISTANCE))
    {
        return;
    }
    static const mvfxAttribute axes[3] = {mvfx_attributePositionX, mvfx_attributePositionY,
                                          mvfx_attributePositionZ};
    for (int32_t k = 0; k < 3; ++k)
    {
        float shift = (float)(state->anchor[k] - origin[k]);
        float* positions = mvfxAttributeArray(instance, emitter, axes[k]);
        for (int32_t i = 0; i < state->count; ++i)
        {
            positions[i] += shift;
        }
        // Pinned points' anchors move with them: their places, or for units
        // held to frames the frames' origins (the instance's space does not
        // move).
        const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
        mvfxAttribute held = compiled->unit.attach == mvfx_attachNone ? mvfx_attributeAnchorX
                                                                      : mvfx_attributeAttachX;
        if (mvfxHasAttribute(&compiled->plan, held))
        {
            float* anchors = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(held + k));
            for (int32_t i = 0; i < state->count; ++i)
            {
                anchors[i] += shift;
            }
        }
        if (mvfxHasAttribute(&instance->effect->emitters[emitter].plan, mvfx_attributeScenePointX))
        {
            // Contact planes move with them.
            float* points = mvfxAttributeArray(instance, emitter,
                                               (mvfxAttribute)(mvfx_attributeScenePointX + k));
            for (int32_t i = 0; i < state->count; ++i)
            {
                points[i] += shift;
            }
        }
        state->anchor[k] = origin[k];
    }
}

// Begins an instance's step of ticks: the distance it travels and its
// velocity along the line from where it was to where it will be.
static void BeginStep(mvfxInstance* instance, int64_t ticks)
{
    instance->stepStart = instance->clock;
    instance->clock += ticks;
    double square = 0.0;
    double d[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = instance->to.origin[k] - instance->from.origin[k];
        square += d[k] * d[k];
    }
    instance->stepDistance = sqrt(square);
    double seconds = (double)ticks / (double)MVFX_TICKS_PER_SECOND;
    for (int32_t k = 0; k < 3; ++k)
    {
        instance->velocity[k] = seconds > 0.0 ? (float)(d[k] / seconds) : 0.0f;
    }
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        if (instance->effect->emitters[e].space == mvfx_spaceWorld)
        {
            Rebase(instance, e);
        }
    }
}

// Ends the step: the instance is where it was to be.
static void EndStep(mvfxInstance* instance)
{
    instance->from = instance->to;
    instance->stepDistance = 0.0;
}

// The emitters whose live particles pour into gas g, as the step left
// them: their arrays and the map from their space into the gas's box.
static int32_t PoursInto(mvfxInstance* instance, int32_t g, mvfxGasPour pours[MVFX_MAX_EMITTERS])
{
    int32_t count = 0;
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        const mvfxCompiledEmitter* emitter = &instance->effect->emitters[e];
        const mvfxEmitterState* state = &instance->emitters[e];
        if (!emitter->gasSplat.enabled || emitter->gasSplat.gas != g || state->count == 0)
        {
            continue;
        }
        bool world = emitter->space == mvfx_spaceWorld;
        mvfxGasPour* pour = &pours[count++];
        *pour = (mvfxGasPour){
            .count = state->count,
            .map = mvfxGasMap(instance, g, world ? mvfx_spaceWorld : mvfx_spaceLocal),
            .splat = &emitter->gasSplat,
        };
        for (int32_t k = 0; k < 3; ++k)
        {
            pour->position[k] =
                mvfxAttributeArray(instance, e, (mvfxAttribute)(mvfx_attributePositionX + k));
            pour->velocity[k] =
                mvfxAttributeArray(instance, e, (mvfxAttribute)(mvfx_attributeVelocityX + k));
            // A world-space emitter's places are offsets from its anchor.
            pour->map.before[k] -= world ? state->anchor[k] : 0.0;
        }
    }
    return count;
}

// Steps an instance's due gases, each driven as the instance's last step
// left it, their passes split over tasks.
static void StepGases(mvfxInstance* instance, const mvfxTasks* tasks)
{
    for (int32_t g = 0; g < instance->effect->gasCount; ++g)
    {
        const mvfxCompiledGas* gas = &instance->effect->gases[g];
        if (gas->def.kind == mvfx_gasLiquid)
        {
            if (gas->hasObstacles)
            {
                mvfxResolveGasObstacles(instance, g);
            }
            mvfxStepLiquidGas(instance, g, tasks);
            continue;
        }
        mvfxResolvedForce* forces = instance->gasForces;
        mvfxGasDrive drive = {.forces = forces,
                              .forceCount = gas->def.forceCount,
                              .pours = instance->gasPours,
                              .tasks = tasks};
        drive.pourCount = PoursInto(instance, g, instance->gasPours);
        mvfxGasGravity(instance, g, drive.gravity);
        for (int32_t f = 0; f < gas->def.forceCount; ++f)
        {
            mvfxSpaceMap map = mvfxGasMap(instance, g, (mvfxSimulationSpace)gas->forces[f].space);
            forces[f] = mvfxResolveForce(&gas->forces[f], &map, instance->parameters,
                                         instance->seed, gas->forceGrids[f]);
        }
        if (gas->hasObstacles)
        {
            mvfxResolveGasObstacles(instance, g);
        }
        mvfxStepGas(gas, &instance->gases[g], &drive, instance->stepStart, instance->clock,
                    instance->stopTick);
    }
    instance->gasesDue = false;
}

void mvfxStepInstance(mvfxInstance* instance, int64_t ticks)
{
    // A second step in one of the context's steps: the first's gases go
    // now, serially, in this instance's task.
    if (instance->gasesDue)
    {
        static const mvfxTasks serial = {0};
        StepGases(instance, &serial);
    }
    // A finished instance holds nothing and spawns nothing; whether it is
    // is known at the step's start, before births up to the new clock are
    // owed.
    bool finished = mvfxIsInstanceFinished(instance);
    BeginStep(instance, ticks);
    for (int32_t n = 0; n < instance->effect->emitterCount; ++n)
    {
        // Sources before the emitters spawning from their events.
        int32_t e = instance->effect->stepOrder[n];
        if (finished)
        {
            // Nothing happens to it: the last step's events go.
            mvfxBeginEvents(instance, e);
            continue;
        }
        mvfxStepEmitter(instance, e);
    }
    instance->gasesDue = !finished && instance->effect->gasCount > 0;
    EndStep(instance);
}

// The most ticks one step takes.
#define MAX_STEP_TICKS ((int64_t)(MVFX_MAX_STEP_SECONDS * MVFX_TICKS_PER_SECOND))

// Whether an instance steps in this step by its off-screen policy,
// counting its unseen steps.
static bool IsDue(mvfxInstance* instance)
{
    if (instance->visible || instance->offscreen == mvfx_offscreenSimulate)
    {
        instance->unseenSteps = 0;
        return true;
    }
    if (instance->offscreen == mvfx_offscreenPause)
    {
        return false;
    }
    uint64_t interval = (uint64_t)instance->offscreenInterval;
    instance->unseenSteps += 1;
    return (instance->unseenSteps + instance->seed % interval) % interval == 0;
}

// Steps an instance by its policy: due, it steps the ticks it holds and
// these; otherwise it holds them (a throttle) or lets them go (a pause).
// A sum past the longest step steps what it holds first.
static void StepByPolicy(mvfxInstance* instance, int64_t ticks)
{
    bool due = IsDue(instance);
    bool paused = !due && instance->offscreen == mvfx_offscreenPause;
    if (!due)
    {
        // The step the events came from is over.
        for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
        {
            mvfxBeginEvents(instance, e);
        }
    }
    if (paused)
    {
        return;
    }
    if (instance->heldTicks + ticks > MAX_STEP_TICKS)
    {
        mvfxStepInstance(instance, instance->heldTicks);
        instance->heldTicks = 0;
    }
    instance->heldTicks += ticks;
    if (due)
    {
        int64_t held = instance->heldTicks;
        instance->heldTicks = 0;
        mvfxStepInstance(instance, held);
    }
}

static void StepRange(int32_t startIndex, int32_t endIndex, void* taskContext)
{
    const StepWork* work = taskContext;
    for (int32_t i = startIndex; i < endIndex; ++i)
    {
        mvfxInstance* instance = work->instances->slots[i].object;
        if (instance != nullptr)
        {
            StepByPolicy(instance, work->ticks);
        }
    }
}

void mvfxStepInstances(mvfxContext* context, int64_t ticks)
{
    context->steps += 1;
    StepWork work = {&context->instances, ticks};
    mvfxRunTasks(&context->tasks, StepRange, (int32_t)context->instances.capacity, STEP_MIN_RANGE,
                 &work);
    mvfxStepDueGases(context);
}

void mvfxStepDueGases(mvfxContext* context)
{
    // Once every instance's task is done, the gases, in slot order.
    for (uint32_t i = 0; i < context->instances.capacity; ++i)
    {
        mvfxInstance* instance = context->instances.slots[i].object;
        if (instance != nullptr && instance->gasesDue)
        {
            StepGases(instance, &context->tasks);
        }
    }
}

mvfxResult mvfxContext_StepTicks(mvfxContext* context, int64_t ticks)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (ticks < 0 || ticks > MAX_STEP_TICKS)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    mvfxStepInstances(context, ticks);
    return mvfx_success;
}

mvfxResult mvfxContext_Step(mvfxContext* context, double seconds)
{
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    if (!(seconds >= 0.0 && seconds <= MVFX_MAX_STEP_SECONDS))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    // Rounded once to the nearest tick, ties up, in the library's mode.
    mvfxFloatMode mode = mvfxEnterFloatMode();
    int64_t ticks = mvfxSecondsToTicks(seconds);
    mvfxLeaveFloatMode(mode);
    mvfxStepInstances(context, ticks);
    return mvfx_success;
}

mvfxResult mvfxInstance_Prewarm(mvfxContext* context, mvfxInstanceId instance, double seconds,
                                double stepSeconds, int32_t maxSteps, double* remainingOut)
{
    if (remainingOut != nullptr)
    {
        *remainingOut = 0.0;
    }
    if (context == nullptr)
    {
        return mvfx_errorInvalid;
    }
    mvfxInstance* live = mvfxContextInstance(context, instance);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    mvfxFloatMode mode = mvfxEnterFloatMode();
    bool valid = seconds >= 0.0 && seconds <= 1e9 && stepSeconds > 0.0 &&
                 stepSeconds <= MVFX_MAX_STEP_SECONDS && maxSteps >= 1;
    int64_t left = valid ? mvfxSecondsToTicks(seconds) : 0;
    int64_t step = valid ? mvfxSecondsToTicks(stepSeconds) : 0;
    if (!valid || step < 1)
    {
        mvfxLeaveFloatMode(mode);
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    for (int32_t i = 0; i < maxSteps && left > 0; ++i)
    {
        int64_t ticks = left < step ? left : step;
        mvfxStepInstance(live, ticks);
        left -= ticks;
    }
    if (live->gasesDue)
    {
        StepGases(live, &context->tasks);
    }
    mvfxLeaveFloatMode(mode);
    if (remainingOut != nullptr)
    {
        *remainingOut = (double)left / (double)MVFX_TICKS_PER_SECOND;
    }
    return mvfx_success;
}
