// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Plans: an instance whose particles are simulated elsewhere, on the GPU
// part or the host's own GPU path. A planned instance holds no particles;
// each step advances its clocks, counters, cursors and anchors and
// writes each emitter's plan: the births the step owes, with their
// serials, the ticks they are born at and the ticks they die at, all
// from the CPU path's own arithmetic, so a simulation elsewhere numbers,
// draws and ends its particles as the CPU path does. Which births have
// room is for the simulation to decide: only it knows the particles it
// holds.

#ifndef MAUL_VFX_PLAN_H
#define MAUL_VFX_PLAN_H

#include "maul-vfx/instance.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A birth a step owes an emitter: 32 bytes, as a GPU buffer holds it.
    typedef struct mvfxPlannedBirth
    {
        // Its serial, the count of the emitter's births owed before it.
        uint32_t serialLow;
        uint32_t serialHigh;
        // The tick it is born at, from the plan's start.
        uint32_t tick;
        // The tick it dies at, from the plan's start, in two words: the
        // first tick at which its age times its inverse life reaches 1.
        uint32_t deathLow;
        uint32_t deathHigh;
        // One over its lifetime in seconds.
        float inverseLife;
        // Its place in its batch, the births one source owes at its tick
        // (a burst's firing, or the rate's), and the batch's size: what a
        // shape's spread of a burst reads.
        uint32_t batchIndex;
        uint32_t batchCount;
    } mvfxPlannedBirth;

    // An emitter's plan for the context's last step.
    typedef struct mvfxEmitterPlan
    {
        // The first tick the plan covers and how many: every step the
        // instance took in the context's last step (none when it was
        // paused or held).
        int64_t start;
        int64_t ticks;
        // A world-space emitter's anchor before the plan and after it:
        // particles are offsets from it, and those held move by the
        // difference, rounded to binary32, when it moves.
        double anchorBefore[3];
        double anchorAfter[3];
        // The births owed, in tick order. Past the plan's room (the
        // capacity's births at one tick, twice the capacity in a plan) a
        // birth is refused here, still owed; refused counts them.
        const mvfxPlannedBirth* births;
        int32_t birthCount;
        uint64_t refused;
        // Whether the instance was cleared before the plan (stopped with
        // mvfx_stopAndClear, restarted, or just created): the simulation
        // drops every particle it holds first.
        bool cleared;
    } mvfxEmitterPlan;

    /// Returns an emitter's plan for the context's last step. The births
    /// stay valid until the context's next step or the instance's
    /// destruction.
    ///
    /// @param context   The context.
    /// @param instance  A planned instance.
    /// @param emitter   The emitter's index in its effect.
    /// @param planOut   Receives the plan; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or
    ///         an emitter out of range; `mvfx_errorStale` for an instance
    ///         that is gone; `mvfx_errorState` for an instance that is not
    ///         planned.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_API mvfxResult mvfxInstance_GetEmitterPlan(const mvfxContext* context,
                                                                   mvfxInstanceId instance,
                                                                   int32_t emitter,
                                                                   mvfxEmitterPlan* planOut);

#ifdef __cplusplus
}
#endif

#endif
