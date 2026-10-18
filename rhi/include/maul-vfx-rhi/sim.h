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
    // offsets from its anchor (its plan's anchor after). Its events of the
    // step, when it writes any (a null buffer otherwise): from word
    // eventFirst of the events buffer, MVFX_GPU_EVENT_WORDS words each, as
    // many as the statistics record's word MVFX_GPU_EVENTS_WRITTEN (at
    // most eventCapacity; those past it in the next word): kind | killed
    // << 8, serial, tick (low, high word), place (binary32 offsets from the
    // plan's anchor after, in the world), velocity, normal, colour, size,
    // then eight words the part uses; in the order the CPU path's
    // mvfxInstance_GetEvents gives them. Contacts are decided on places,
    // which the GPU holds to a tolerance of the CPU path's: a particle
    // whose path passes that close to a surface may touch it on one path
    // only, and with it its collision event, the life or the kill the
    // contact takes, and the births a spawner gives for it; contacts
    // clear of a surface are the CPU path's. A contact's tick is found from
    // its share of the move in binary32, within a thousandth of a step of
    // the CPU path's: births a spawner gives for a contact whose tick
    // differs may differ in count and values, those for every other event
    // are the CPU path's but for their serials, moved by the births that
    // differ (mvfxEventSpawn).
    typedef struct mvfxGpuParticles
    {
        mrhiBufferId particles;
        mrhiBufferId statistics;
        int32_t capacity;
        int32_t words;
        mrhiBufferId events;
        uint32_t eventFirst;
        int32_t eventCapacity;
    } mvfxGpuParticles;

// The words of an event in an events buffer, and the statistics record's
// word holding the step's count of events written (the next, those
// dropped).
#define MVFX_GPU_EVENT_WORDS    28
#define MVFX_GPU_EVENTS_WRITTEN 28

// For an emitter that spawns from events, the statistics record's words
// holding the serials its event births took (two words), then the step's
// event births refused (past its spawning's capacity) and births past its
// births' room (twice its capacity).
#define MVFX_GPU_EVENT_SERIALS 34

// The words of an emitter's statistics record.
#define MVFX_GPU_STATISTICS_WORDS 38

    /// Returns the default simulation def: the C library's allocation, no
    /// GPU object, context or instance.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_RHI_API mvfxGpuSimDef mvfxDefaultGpuSimDef(void);

    /// Makes a simulation of a planned instance: each emitter's buffers on
    /// the GPU object's device; an emitter that sorts its records by place
    /// keeps 16 bytes a particle of its capacity and 2,048 a block of 1,024
    /// of them for the sort, one that writes events 36 bytes a particle of
    /// its capacity, 16 a block of its scan (a block for 1,024 of three
    /// times its capacity), 112 an event of its capacity of them and 1,952
    /// for their numbers; one that spawns from another's events the same
    /// 36 and 16 where it writes none, 140 an event of its source's event
    /// capacity and 64 more for the region's counts, 96 more a particle of
    /// its capacity for its plan's births staged, and 64 a birth from
    /// events a step takes (to twice its capacity) for their records.
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
    /// pass and seven compute passes an emitter, four more for one that
    /// writes events (mvfxGpuParticles). It reads the instance's
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

// The record passes an emitter takes in a frame.
#define MVFX_GPU_RECORD_PASSES 4

    // Where an emitter's records go (mvfxGpuSim_AddRecords). Build it with
    // mvfxDefaultGpuRecordsDef.
    typedef struct mvfxGpuRecordsDef
    {
        uint32_t cookie;
        // The emitter, and the layout its records take: a stride that is a
        // multiple of 4 bytes, no ribbon or neighbor field.
        int32_t emitter;
        mvfxLayoutId layout;
        // The view positions are made relative to (its origin) and a sort
        // by place measures from (its eye and forward direction): each
        // coordinate of the origin and the eye at most 2^52 in size, the
        // directions finite.
        mvfxView view;
        // A storage buffer of the frame open the records go into, from a
        // byte that is a multiple of 4, at most capacity of them.
        mrhiResourceId records;
        uint64_t offset;
        int64_t capacity;
        // Optionally a storage buffer of the frame and a byte (a multiple
        // of 4) the record count goes to as a 32-bit word: the live count
        // held to the capacity; a null id for none.
        mrhiResourceId count;
        uint64_t countOffset;
    } mvfxGpuRecordsDef;

// The cookie of a records def, which its default sets and
// mvfxGpuSim_AddRecords checks.
#define MVFX_GPU_RECORDS_DEF_COOKIE 0x6D767264u

    /// Returns the default records def: emitter 0, no buffer yet.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_RHI_API mvfxGpuRecordsDef mvfxDefaultGpuRecordsDef(void);

    /// Adds the passes writing an emitter's records to the frame the host
    /// builds, as mvfxInstance_WriteParticles writes them on the CPU path,
    /// in the emitter's order: a transfer pass and a compute pass, and 9
    /// compute passes more for a sort by place (the view's eye and forward
    /// direction its measure's). What a host may rely on of a sorted
    /// emitter: the records are the CPU path's by serial (a cut's border
    /// may differ by particles within a tolerance of its measure), each
    /// matching its serial's, and through the CPU path's measures the
    /// order never falls by more than a relative 1e-5; the CPU path's
    /// order is exact, so the two paths' frames may swap particles whose
    /// measures lie that close, and no more. The passes come after
    /// the step's passes of the frame (mvfxGpuSim_AddPasses) or, without them,
    /// reading the particles the last recorded step left; mvfxGpuSim_Record
    /// records them, and the next mvfxGpuSim_AddPasses lets go of those of
    /// a frame the host dropped. At most MVFX_GPU_RECORD_PASSES an emitter
    /// a frame. An emitter without particles at the instance's quality
    /// level writes no record; its count, if asked for, is 0.
    ///
    /// @param sim  The simulation.
    /// @param def  Where the records go, from mvfxDefaultGpuRecordsDef.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         def without its cookie, an emitter out of range, a layout
    ///         whose stride is not a multiple of 4 or with a field the part
    ///         does not write, an offset not a multiple of 4, a negative
    ///         capacity, a null records buffer, or a view whose origin or
    ///         eye is out of range or whose directions are not finite;
    ///         `mvfx_errorStale` for a layout or an instance that is gone;
    ///         `mvfx_errorLimit` past MVFX_GPU_RECORD_PASSES for the
    ///         emitter this frame; `mvfx_errorState` when the device refuses
    ///         a pass or an import (no frame open).
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_AddRecords(mvfxGpuSim* sim,
                                                                 const mvfxGpuRecordsDef* def);

    /// Records the passes mvfxGpuSim_AddPasses added, after the frame is
    /// compiled: the plans' bytes written, the kernels dispatched. Nothing
    /// when it added none. The bytes are the frame's uploads
    /// (mvfxGpuSim_UploadBytes counts them): 1,944 an emitter (its
    /// numbers' 1,872 and its record map's 72), 432 a force, 128 a collider
    /// and 80 a kill rule it has (8,088 at most), and 1,952 more for one
    /// that writes events (its events' values over life); 48 a planned
    /// birth (a plan holds at most twice the emitter's capacity) and 64 a
    /// host's particle; at the first step and after a clear, 12 for its
    /// counts and 16 more for one spawning from events; at the first step,
    /// its sources' 1,184 and the effect's baked tables its values read
    /// (1,032 a curve, 4,096 a gradient); and the values of each grid its
    /// forces and colliders read (mvfxGridView's bytes)
    /// at the first step, after each change and, for the instance's,
    /// after each move in the room the def keeps, which the device's
    /// frameUploadBytes must hold.
    ///
    /// @param sim  The simulation.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL simulation;
    ///         `mvfx_errorState` when the device refuses a command, such as
    ///         an upload past the frame's.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_Record(mvfxGpuSim* sim);

    /// Counts the bytes the last mvfxGpuSim_Record uploaded: its step
    /// passes' as it states them, and its record passes' (1,952 a pass, 4 for
    /// one of an emitter with no particles), so a host can hold its
    /// frameUploadBytes to what its effects take.
    ///
    /// @param sim       The simulation.
    /// @param bytesOut  Receives the bytes; 0 before a record and on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_UploadBytes(const mvfxGpuSim* sim,
                                                                  int64_t* bytesOut);

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
