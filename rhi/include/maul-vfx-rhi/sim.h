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
        // The bytes each emitter keeps for the grids its instance's
        // colliders read (mvfxInstance_SetColliders: heightfields and
        // distance grids), their values as stored (mvfxGridView's bytes,
        // each rounded up to 4, each grid once), as
        // mvfxGpuSim_HostGridBytes counts them; 0 for none.
        int64_t hostGridBytes;
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
    // capacity each, words of them in all: the emitter's arrays as the CPU
    // path stores them (mvfxEmitterView.arrays: binary32 values, the
    // birth tick, serial, collisions, frame and mesh as words; values
    // over life not applied), then the death tick (low, high word) and a
    // word the step uses; and its statistics record,
    // MVFX_GPU_STATISTICS_WORDS words of the last recorded step: the live
    // count first, then what the step uses and the bounds, which
    // mvfxGpuSim_BoundsOf reads. A world-space emitter's places are
    // offsets from its anchor (its plan's anchor after).
    typedef struct mvfxGpuParticles
    {
        mrhiBufferId particles;
        mrhiBufferId statistics;
        int32_t capacity;
        int32_t words;
    } mvfxGpuParticles;

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

    /// Counts the bytes of the grids a set of the instance's colliders
    /// reads, the room mvfxGpuSimDef.hostGridBytes must keep for them:
    /// each heightfield's and distance grid's grid once, its bytes
    /// (mvfxGridView) rounded up to 4.
    ///
    /// @param context    The context that owns the grids.
    /// @param colliders  The colliders, as for mvfxInstance_SetColliders;
    ///                   NULL when count is 0.
    /// @param count      How many, 0 to MVFX_MAX_COLLIDERS.
    /// @param bytesOut   Receives the bytes; 0 on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL context or
    ///         bytesOut, a count out of range or NULL colliders with a
    ///         count; `mvfx_errorStale` for a grid that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_HostGridBytes(const mvfxContext* context,
                                                                    const mvfxCollider* colliders,
                                                                    int32_t count,
                                                                    int64_t* bytesOut);

    /// Gives the textures of the frame open that the next
    /// mvfxGpuSim_AddPasses binds for the emitters whose collision meets
    /// the instance's depth image (mvfxInstance_SetDepthImage, which may
    /// come without pixels for a planned instance): the depth the host
    /// rendered, a `Depth32Float` or `DepthStencil` texture of the image's
    /// size whose depth aspect is read, nearest pixel; and optionally the
    /// surface's normals in the view's space, an `Rgba32Float` or
    /// `Rgba16Float` texture of the same size, read likewise (a null id
    /// to make them from the depth). Each is read where the step's passes
    /// run; mvfxGpuSim_Record lets go of them.
    ///
    /// @param sim      The simulation.
    /// @param depth    The depth texture's id in the frame, or a null id.
    /// @param normals  The normal texture's id in the frame, or a null id
    ///                 (ignored without a depth texture).
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL simulation.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_SetDepthTextures(mvfxGpuSim* sim,
                                                                       mrhiResourceId depth,
                                                                       mrhiResourceId normals);

    /// Adds the passes of the context's last step to the frame the host
    /// builds (after mrhiBeginFrame, before mrhiCompileFrame): a transfer
    /// pass and seven compute passes an emitter. It reads the instance's
    /// plans, which must stay until mvfxGpuSim_Record. A step whose
    /// instance's grids pass the room the def keeps adds no pass: the
    /// particles stay as they were, the step's plans unsimulated.
    ///
    /// @param sim  The simulation.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL simulation;
    ///         `mvfx_errorStale` for an instance that is gone;
    ///         `mvfx_errorState` when the device refuses a pass or an
    ///         import (no frame open), or when an emitter meets the
    ///         instance's depth image and no depth texture was given
    ///         (mvfxGpuSim_SetDepthTextures); `mvfx_errorCapacity` when the
    ///         grids the instance's colliders read pass the def's
    ///         hostGridBytes.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_AddPasses(mvfxGpuSim* sim);

    /// Records the passes mvfxGpuSim_AddPasses added, after the frame is
    /// compiled: the plans' bytes written, the kernels dispatched. Nothing
    /// when it added none. The bytes are the frame's uploads: 1,512 an
    /// emitter, 432 a force and 128 a collider it has (7,016 at most), 32
    /// a planned birth (a plan holds at most twice the
    /// emitter's capacity), and the values of each grid its forces and
    /// colliders read (mvfxGridView's bytes) at the first step, after each
    /// change and, for the instance's, after each move in the room the
    /// def keeps, which the device's frameUploadBytes must hold.
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
