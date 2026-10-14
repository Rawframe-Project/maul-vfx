// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public step calls, and the task over instance slots.

#include "step.h"

#include "emitter_step.h"
#include "event.h"
#include "float_mode.h"
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

void mvfxStepInstance(mvfxInstance* instance, int64_t ticks)
{
    // A finished instance holds nothing and spawns nothing; whether it is
    // is known at the step's start, before births up to the new clock are
    // owed.
    bool finished = mvfxIsInstanceFinished(instance);
    BeginStep(instance, ticks);
    for (int32_t e = 0; e < instance->effect->emitterCount; ++e)
    {
        if (finished)
        {
            // Nothing happens to it: the last step's events go.
            mvfxBeginEvents(instance, e);
            continue;
        }
        mvfxStepEmitter(instance, e);
    }
    EndStep(instance);
}

static void StepRange(int32_t startIndex, int32_t endIndex, void* taskContext)
{
    const StepWork* work = taskContext;
    for (int32_t i = startIndex; i < endIndex; ++i)
    {
        mvfxInstance* instance = work->instances->slots[i].object;
        if (instance != nullptr)
        {
            mvfxStepInstance(instance, work->ticks);
        }
    }
}

void mvfxStepInstances(mvfxContext* context, int64_t ticks)
{
    StepWork work = {&context->instances, ticks};
    mvfxRunTasks(&context->tasks, StepRange, (int32_t)context->instances.capacity, STEP_MIN_RANGE,
                 &work);
}

// The most ticks one step takes.
#define MAX_STEP_TICKS ((int64_t)(MVFX_MAX_STEP_SECONDS * MVFX_TICKS_PER_SECOND))

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
    mvfxLeaveFloatMode(mode);
    if (remainingOut != nullptr)
    {
        *remainingOut = (double)left / (double)MVFX_TICKS_PER_SECOND;
    }
    return mvfx_success;
}
