// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A planned instance simulated on the GPU: made once from a ready GPU
// object, a context and an instance planned in it (maul-vfx/plan.h), it
// holds each emitter's particles in device buffers. Each frame, after the
// context's step, mvfxGpuSim_AddPasses adds its passes to the frame the
// host builds, and mvfxGpuSim_Record records them once the frame is
// compiled. Nothing is read back in a step: each emitter's statistics
// record (its count and bounds) is the host's to read back, as late as its
// frames lag, and mvfxGpuSim_BoundsOf maps it to the world. What it does
// not run yet is refused when it is made.

#ifndef MAUL_VFX_RHI_SIM_H
#define MAUL_VFX_RHI_SIM_H

#include "maul-rhi/resources.h"
#include "maul-vfx-rhi/gpu.h"
#include "maul-vfx/output.h"
#include "maul-vfx/plan.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A planned instance's particles on the GPU.
    typedef struct mvfxGpuSim mvfxGpuSim;

    // How a simulation is made. Build it with mvfxDefaultGpuSimDef.
    typedef struct mvfxGpuSimDef
    {
        uint32_t cookie;
        mvfxAllocator allocator;
        // A ready GPU object, which outlives the simulation.
        mvfxGpu* gpu;
        // The context, the instance's effect, and the instance, planned.
        const mvfxContext* context;
        mvfxEffectId effect;
        mvfxInstanceId instance;
    } mvfxGpuSimDef;

// The cookie of a simulation def, which its default sets and
// mvfxCreateGpuSim checks.
#define MVFX_GPU_SIM_DEF_COOKIE 0x6D767373u

    // Why a simulation was refused: the first emitter it cannot run, the
    // features of it the part does not run yet, and whether its shape's
    // kind or its velocity's mode is one it does not run yet.
    typedef struct mvfxGpuRefusal
    {
        int32_t emitter;
        uint64_t features;
        bool shape;
        bool velocity;
    } mvfxGpuRefusal;

    // An emitter's particles: a buffer of 32-bit words, arrays of the
    // capacity each, in this order: place (x, y, z), velocity (x, y, z),
    // serial (low, high word), birth tick (low, high), death tick (low,
    // high), inverse life, collisions, a word the step uses; and its
    // statistics
    // record, MVFX_GPU_STATISTICS_WORDS words of the last recorded step:
    // the live count first, then what the step uses and the bounds, which
    // mvfxGpuSim_BoundsOf reads. A world-space emitter's places are
    // offsets from its anchor (its plan's anchor after).
    typedef struct mvfxGpuParticles
    {
        mrhiBufferId particles;
        mrhiBufferId statistics;
        int32_t capacity;
    } mvfxGpuParticles;

// The words a particle takes in mvfxGpuParticles.particles.
#define MVFX_GPU_PARTICLE_WORDS 15

// The words of an emitter's statistics record.
#define MVFX_GPU_STATISTICS_WORDS 28

    /// Returns the default simulation def: the C library's allocation, no
    /// GPU object, context or instance.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_RHI_API mvfxGpuSimDef mvfxDefaultGpuSimDef(void);

    /// Makes a simulation of a planned instance: each emitter's buffers on
    /// the GPU object's device.
    ///
    /// @param def         The def, from mvfxDefaultGpuSimDef.
    /// @param simOut      Receives the simulation; NULL on failure.
    /// @param refusalOut  Optional: receives why it was refused, an emitter
    ///                    of -1 otherwise.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         def without its cookie, a GPU object that is not ready, an
    ///         allocator with one of its two functions alone, or an
    ///         instance that is not planned; `mvfx_errorStale` for an
    ///         instance or effect that is gone; `mvfx_errorState` for an
    ///         effect it does not run yet (refusalOut says why) or when the
    ///         device refuses a buffer; `mvfx_errorCapacity` when the
    ///         allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxCreateGpuSim(const mvfxGpuSimDef* def,
                                                            mvfxGpuSim** simOut,
                                                            mvfxGpuRefusal* refusalOut);

    /// Destroys a simulation: its buffers. No frame of the device may
    /// still use them.
    ///
    /// @param sim  The simulation; NULL does nothing.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_RHI_API void mvfxDestroyGpuSim(mvfxGpuSim* sim);

    /// Adds the passes of the context's last step to the frame the host
    /// builds (after mrhiBeginFrame, before mrhiCompileFrame): a transfer
    /// pass and seven compute passes an emitter. It reads the instance's
    /// plans, which must stay until mvfxGpuSim_Record.
    ///
    /// @param sim  The simulation.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL simulation;
    ///         `mvfx_errorStale` for an instance that is gone;
    ///         `mvfx_errorState` when the device refuses a pass or an
    ///         import (no frame open), or when the instance's colliders
    ///         (mvfxInstance_SetColliders) hold a kind the part does not
    ///         run yet (heightfields and distance grids).
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_AddPasses(mvfxGpuSim* sim);

    /// Records the passes mvfxGpuSim_AddPasses added, after the frame is
    /// compiled: the plans' bytes written, the kernels dispatched. Nothing
    /// when it added none. The bytes are the frame's uploads: 4,632 an
    /// emitter, 32 a planned birth (a plan holds at most twice the
    /// emitter's capacity), and the values of each grid its forces read
    /// (mvfxGridView's bytes) at the first step and after each change,
    /// which the device's frameUploadBytes must hold.
    ///
    /// @param sim  The simulation.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL simulation;
    ///         `mvfx_errorState` when the device refuses a command, such as
    ///         an upload past the frame's.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_Record(mvfxGpuSim* sim);

    /// Returns an emitter's particles: the buffer the last recorded step
    /// left them in, which the host imports to draw or read them.
    ///
    /// @param sim           The simulation.
    /// @param emitter       The emitter's index in its effect.
    /// @param particlesOut  Receives the buffers; zeroed on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or an
    ///         emitter out of range.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_GetParticles(const mvfxGpuSim* sim,
                                                                   int32_t emitter,
                                                                   mvfxGpuParticles* particlesOut);

    /// Maps an emitter's statistics record, read back from its buffer any
    /// frames after its step, to its bounds in the world as
    /// mvfxInstance_GetBounds gives the CPU path's: its fixed box through
    /// the instance's transform at that step; else empty with no
    /// particles, or the box of its places at that step's end grown by the
    /// largest size any can have, through the map to the world at that
    /// step. The record holds that step's map, so a late read is exact
    /// for its step.
    ///
    /// @param sim        The simulation.
    /// @param emitter    The emitter's index in its effect.
    /// @param record     The record, MVFX_GPU_STATISTICS_WORDS words.
    /// @param boundsOut  Receives the bounds; empty on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument or
    ///         an emitter out of range.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_BoundsOf(const mvfxGpuSim* sim,
                                                               int32_t emitter,
                                                               const uint32_t* record,
                                                               mvfxBounds* boundsOut);

#ifdef __cplusplus
}
#endif

#endif
