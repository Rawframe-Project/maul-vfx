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
        // Its memory; its functions call none of the part's functions,
        // nor change the context the simulation reads (record mvfx-0001).
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
        // The bytes each emitter of a mesh shape keeps for its mesh beyond
        // the one it spawns from at the simulation's creation (room for
        // mvfxInstance_SetEmitterMesh to bind a larger one), as
        // mvfxGpuSim_MeshBytes counts a mesh's, at most MVFX_GPU_MESH_BYTES;
        // 0 for the mesh at creation's alone.
        int64_t hostMeshBytes;
    } mvfxGpuSimDef;

// The most bytes a mesh shape's mesh takes on the GPU.
#define MVFX_GPU_MESH_BYTES (INT64_C(1) << 30)

// The cookie of a simulation def, which its default sets and
// mvfxCreateGpuSim checks.
#define MVFX_GPU_SIM_DEF_COOKIE 0x6D767373u

    // Why a simulation was refused: the first emitter it cannot run, and
    // the features of it the part does not run yet. Contacts
    // (MVFX_FEATURE_CONTACTS) are refused beside anything that kills
    // within a step (kill rules, colliders that kill, an attractor's kill
    // radius), whose killed the CPU path's contacts still see, and beside
    // ribbons and beams, whose points the CPU path takes before contacts
    // move the births.
    typedef struct mvfxGpuRefusal
    {
        int32_t emitter;
        uint64_t features;
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
    // differ (mvfxEventSpawn). A ribbon's break by distance is decided on
    // places too: a birth whose gap to its ribbon's youngest point lies
    // that close to the break distance may break the ribbon on one path
    // only, its ribbon then joined or started apart from the CPU path's,
    // and every id the counter gives after it one apart; the ribbons are
    // the CPU path's but for their ids and at such births. Distances
    // along a ribbon are within the tolerance of the CPU path's. Its
    // plexus links of the step, when it has them (a null buffer
    // otherwise): two words each, the owner's index in the particles then
    // the other's, as many as the statistics record's word MVFX_GPU_LINKS
    // (at most linkCapacity, the def's maxLinks), in the order the CPU
    // path's links take. Links are decided on places too: a pair that
    // close to the distance may link on one path only; from the same
    // places the links are the CPU path's.
    typedef struct mvfxGpuParticles
    {
        mrhiBufferId particles;
        mrhiBufferId statistics;
        int32_t capacity;
        int32_t words;
        mrhiBufferId events;
        uint32_t eventFirst;
        int32_t eventCapacity;
        mrhiBufferId links;
        int32_t linkCapacity;
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

// For an emitter whose births join ribbons, the statistics record's word
// counting the ribbon breaks the GPU made (a birth farther than the break
// distance from its ribbon's youngest point) since the instance was last
// cleared: the emitter's ribbon counter is its plan's (the host's breaks)
// plus it.
#define MVFX_GPU_RIBBON_BREAKS 38

// For an emitter that finds its neighbors (mvfxNeighborDef), the
// statistics record's word counting the queries its cap stopped since the
// instance was last cleared (mvfxEmitterStats.neighborsCapped on the CPU
// path).
#define MVFX_GPU_NEIGHBORS_CAPPED 46

// For an emitter with particle contacts (mvfxContactDef), the statistics
// record's word counting the particles whose contacts in a step's first
// pass were more than the most, since the instance was last cleared
// (mvfxEmitterStats.contactsCapped on the CPU path).
#define MVFX_GPU_CONTACTS_CAPPED 47

// For an emitter with plexus links (mvfxPlexusDef), the statistics
// record's words holding the step's links (the first of the links
// buffer's, at most its capacity) and the links dropped past either most
// since the instance was last cleared (mvfxEmitterStats.linksDropped on
// the CPU path).
#define MVFX_GPU_LINKS         48
#define MVFX_GPU_LINKS_DROPPED 49

// The words of an emitter's statistics record (for an emitter whose
// ribbons pin a head, its words from 39 hold the head, which
// mvfxGpuSim_BoundsOf joins as the CPU path's bounds do).
#define MVFX_GPU_STATISTICS_WORDS 50

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
    /// of them for the sort (one that carries lights and does not sort, 8
    /// a block for its lights' counts), one that writes events 36 bytes a particle of
    /// its capacity, 16 a block of its scan (a block for 1,024 of three
    /// times its capacity), 112 an event of its capacity of them and 1,952
    /// for their numbers; one that spawns from another's events the same
    /// 36 and 16 where it writes none, 140 an event of its source's event
    /// capacity and 64 more for the region's counts, 96 more a particle of
    /// its capacity for its plan's births staged, and 64 a birth from
    /// events a step takes (to twice its capacity) for their records; one
    /// whose particles are queried for neighbors 24 bytes a particle of
    /// its capacity, 18,432 a block of 1,024 of them for its grid's sorted
    /// keys and digit counts, and 756 more; one that finds neighbors in
    /// another's grid, or steers, 752 for its numbers; one that keeps its
    /// particles apart 28 bytes a particle of its capacity, 18,432 a block
    /// of 1,024 of them for its contact grid's sorted keys and digit
    /// counts, and 2,016 more for its numbers and counts; one with plexus
    /// links 12 bytes a particle of its capacity, 18,432 a block of 1,024
    /// of them for its link grid's sorted keys and digit counts, 112 a
    /// link of its most (mvfxPlexusDef.maxLinks: the links, and the
    /// scratch their output takes) and 48 for its numbers.
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
    /// still use them. Called again from its allocator while it releases
    /// the simulation's memory, it does nothing.
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

    /// Counts the bytes a mesh takes on the GPU for a mesh shape's mode,
    /// the room mvfxGpuSimDef.hostMeshBytes keeps for it: 16, 24 a vertex,
    /// and for each element the mode chooses among (each vertex for
    /// mvfx_meshVertices) 4 a vertex index and 8.
    ///
    /// @param context   The context that owns the mesh.
    /// @param mesh      The mesh.
    /// @param mode      The mesh shape's mode.
    /// @param bytesOut  Receives the bytes; 0 on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL context or
    ///         bytesOut or a mode out of range; `mvfx_errorStale` for a mesh
    ///         that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_MeshBytes(const mvfxContext* context,
                                                                mvfxMeshId mesh, mvfxMeshMode mode,
                                                                int64_t* bytesOut);

    /// Binds another mesh to the simulation's instance's emitter of a mesh
    /// shape, as mvfxInstance_SetEmitterMesh does, when it fits the room
    /// the simulation keeps for the emitter's mesh (the mesh at creation's
    /// or the def's hostMeshBytes, whichever is more): a mesh too large is
    /// refused here, at setup, rather than refusing every step after.
    ///
    /// @param sim      The simulation.
    /// @param context  Its context, which the binding changes.
    /// @param emitter  The emitter's index in its effect.
    /// @param mesh     The mesh, of the elements the emitter's mode needs.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument,
    ///         another context, an emitter out of range or not of a mesh
    ///         shape, or a mesh without the elements its mode needs;
    ///         `mvfx_errorStale` for a mesh or an instance that is gone;
    ///         `mvfx_errorCapacity` for a mesh past the room (unbound);
    ///         `mvfx_errorState` while the simulation is destroyed or from
    ///         the context's host code.
    /// @par Thread safety
    /// Safe from any thread; the context and the device are each used by
    /// one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_SetEmitterMesh(mvfxGpuSim* sim,
                                                                     mvfxContext* context,
                                                                     int32_t emitter,
                                                                     mvfxMeshId mesh);

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
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL simulation;
    ///         `mvfx_errorState` while it is destroyed (a call from its
    ///         allocator).
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
    ///         (mvfxGpuSim_SetDepthTextures), or while it is destroyed (a
    ///         call from its allocator); `mvfx_errorCapacity` when the
    ///         grids the instance's colliders read pass the def's
    ///         hostGridBytes, or a mesh shape's mesh its room.
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
    /// a frame, its ribbon passes (mvfxGpuSim_AddRibbons) among them. An emitter without particles
    /// at the instance's quality level writes no record; its count, if asked for, is 0.
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
    ///         a pass or an import (no frame open), or while it is destroyed
    ///         (a call from its allocator).
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_AddRecords(mvfxGpuSim* sim,
                                                                 const mvfxGpuRecordsDef* def);

    // Where an emitter's lights go (mvfxGpuSim_AddLights). Build it with
    // mvfxDefaultGpuLightsDef.
    typedef struct mvfxGpuLightsDef
    {
        uint32_t cookie;
        int32_t emitter;
        // The view the lights' positions are made relative to (its
        // origin's coordinates each at most 2^52 in size).
        mvfxView view;
        // A storage buffer of the frame open the lights go into, as
        // mvfxLight (32 bytes each), from a byte that is a multiple of 4,
        // at most capacity of them.
        mrhiResourceId lights;
        uint64_t offset;
        int64_t capacity;
        // Optionally a storage buffer of the frame and a byte (a multiple
        // of 4) the light count goes to as a 32-bit word: every light there
        // is, as mvfxInstance_WriteLights counts them; a null id for none.
        mrhiResourceId count;
        uint64_t countOffset;
    } mvfxGpuLightsDef;

// The cookie of a lights def, which its default sets and
// mvfxGpuSim_AddLights checks.
#define MVFX_GPU_LIGHTS_DEF_COOKIE 0x6D76726Cu

    /// Returns the default lights def: emitter 0, no buffer yet.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_RHI_API mvfxGpuLightsDef mvfxDefaultGpuLightsDef(void);

    /// Adds the passes writing the lights an emitter's particles carry to
    /// the frame the host builds, as mvfxInstance_WriteLights writes them
    /// on the CPU path: every particle whose serial's low word is a
    /// multiple of its light def's every, in the pool's order, the first
    /// capacity of them: a transfer pass and three compute passes (each
    /// block's carriers counted, the counts scanned, the lights written).
    /// It is a record pass of the emitter's (MVFX_GPU_RECORD_PASSES), the
    /// same frames and order as mvfxGpuSim_AddRecords. An emitter without
    /// lights or without particles at the instance's quality level writes
    /// none; its count, if asked for, is 0.
    ///
    /// @param sim  The simulation.
    /// @param def  Where the lights go, from mvfxDefaultGpuLightsDef.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         def without its cookie, an emitter out of range, an offset
    ///         not a multiple of 4, a negative capacity, a null lights
    ///         buffer, or a view whose origin is out of range;
    ///         `mvfx_errorStale` for an instance that is gone;
    ///         `mvfx_errorLimit` past MVFX_GPU_RECORD_PASSES for the
    ///         emitter this frame; `mvfx_errorState` when the device refuses
    ///         a pass or an import (no frame open), or while it is destroyed
    ///         (a call from its allocator).
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_AddLights(mvfxGpuSim* sim,
                                                                const mvfxGpuLightsDef* def);

// The counts a ribbon pass writes, 32-bit words: the points and the
// ribbons there are, as mvfxInstance_WriteRibbons counts them, and the
// points and the ribbons written (for strips, mvfxGpuSim_ExpandRibbons,
// the vertices and the indices). What is left out is the difference, the
// room a host lacks.
#define MVFX_GPU_RIBBON_COUNTS 4

    // Where an emitter's ribbons go (mvfxGpuSim_AddRibbons). Build it with
    // mvfxDefaultGpuRibbonsDef.
    typedef struct mvfxGpuRibbonsDef
    {
        uint32_t cookie;
        // The emitter, and the layout its points take: a stride that is a
        // multiple of 4 bytes, no neighbor field.
        int32_t emitter;
        mvfxLayoutId layout;
        // The view positions are made relative to (its origin) and ribbons
        // face (its eye): each coordinate of the origin and the eye at most
        // 2^52 in size, the directions finite.
        mvfxView view;
        // A storage buffer of the frame open the points go into, from a
        // byte that is a multiple of 4, at most pointCapacity of them.
        mrhiResourceId points;
        uint64_t pointOffset;
        int64_t pointCapacity;
        // A storage buffer of the frame the list of the ribbons written
        // goes into, from a byte that is a multiple of 4, at most
        // ribbonCapacity of them, each as an mvfxRibbon on a little-endian
        // host (its first point as two 32-bit words, low first, its points,
        // its id).
        mrhiResourceId ribbons;
        uint64_t ribbonOffset;
        int64_t ribbonCapacity;
        // Optionally a storage buffer of the frame and a byte (a multiple
        // of 4) the counts go to, MVFX_GPU_RIBBON_COUNTS words; a null id
        // for none.
        mrhiResourceId counts;
        uint64_t countsOffset;
    } mvfxGpuRibbonsDef;

// The cookie of a ribbons def, which its default sets and
// mvfxGpuSim_AddRibbons checks.
#define MVFX_GPU_RIBBONS_DEF_COOKIE 0x6D767262u

    /// Returns the default ribbons def: emitter 0, no buffer yet.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_RHI_API mvfxGpuRibbonsDef mvfxDefaultGpuRibbonsDef(void);

    /// Adds the passes writing an emitter's ribbons to the frame the host
    /// builds, as mvfxInstance_WriteRibbons writes them on the CPU path: a
    /// transfer pass, a keys pass, 8 radix passes, for ribbons of more
    /// pieces than one a pass cutting each segment into its pieces, a pass
    /// in one workgroup finding the chains and one writing their points.
    /// Its live particles
    /// are chained by ribbon id, oldest first, in the ids' order; each
    /// chain of two or more points is a ribbon, and those that fit whole
    /// after the ones before are written, none after the first that does
    /// not. What a host may rely on: the chains, the counts, the list and
    /// each point's particle are the CPU path's; a point's length along
    /// its ribbon, coordinate along, frame and the values read at them are
    /// summed and turned in binary32, within a relative 1e-4 of what the
    /// CPU path's binary64 makes of the same particles (the paths'
    /// particles differ within their own tolerance, and a point's frame
    /// turns with that over its neighbours' distance, a side and a face
    /// normal more as the tangent nears the direction faced across, by
    /// the sine between them). A segment's pieces are decided by the
    /// bend's cosine in binary32: one whose cosine lies within its
    /// rounding of a limit may be cut into a count one apart, the points
    /// after it in the chain moved by that. The passes come where a record
    /// pass's would (mvfxGpuSim_AddRecords), and count among its
    /// MVFX_GPU_RECORD_PASSES an emitter a frame. An emitter without
    /// particles at the instance's quality level, or not drawn as ribbons,
    /// writes no point; its counts, if asked for, are 0. A pinned head ends
    /// the current ribbon as the plan read now gives it
    /// (mvfxEmitterPlan.ribbonHead), the youngest particle's values at its
    /// place.
    ///
    /// @param sim  The simulation.
    /// @param def  Where the ribbons go, from mvfxDefaultGpuRibbonsDef.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         def without its cookie, an emitter out of range, a layout whose stride
    ///         is not a multiple of 4 or with a neighbor field, an offset
    ///         not a multiple of 4, a negative capacity, a null points or
    ///         ribbons buffer, or a view whose origin or eye is out of range
    ///         or whose directions are not finite; `mvfx_errorStale` for a
    ///         layout or an instance that is gone; `mvfx_errorLimit` past
    ///         MVFX_GPU_RECORD_PASSES for the emitter this frame;
    ///         `mvfx_errorState` when the device refuses a pass or an
    ///         import (no frame open), or while it is destroyed (a call
    ///         from its allocator).
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_AddRibbons(mvfxGpuSim* sim,
                                                                 const mvfxGpuRibbonsDef* def);

    // Where an emitter's ribbons expanded go (mvfxGpuSim_ExpandRibbons).
    // Build it with mvfxDefaultGpuExpandDef.
    typedef struct mvfxGpuExpandDef
    {
        uint32_t cookie;
        // The emitter, the vertices' layout (as a ribbons def's) and the
        // view.
        int32_t emitter;
        mvfxLayoutId layout;
        mvfxView view;
        // A storage buffer of the frame open the vertices go into, from a
        // byte that is a multiple of 4, at most vertexCapacity of them.
        mrhiResourceId vertices;
        uint64_t vertexOffset;
        int64_t vertexCapacity;
        // A storage buffer of the frame the 32-bit indices go into (from 0
        // at the first vertex), from a byte that is a multiple of 4, at
        // most indexCapacity of them.
        mrhiResourceId indices;
        uint64_t indexOffset;
        int64_t indexCapacity;
        // Optionally a storage buffer of the frame and a byte (a multiple
        // of 4) the counts go to, MVFX_GPU_RIBBON_COUNTS words; a null id
        // for none.
        mrhiResourceId counts;
        uint64_t countsOffset;
        // For mvfxGpuSim_ExpandTubes, the tubes' sides (0 for strips, else
        // 3 to MVFX_MAX_TUBE_SIDES) and whether their ends are capped.
        mvfxTubeDef tube;
    } mvfxGpuExpandDef;

// The cookie of an expand def, which its default sets and the expanding
// calls check.
#define MVFX_GPU_EXPAND_DEF_COOKIE 0x6D767265u

    /// Returns the default expand def: emitter 0, no buffer yet.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_RHI_API mvfxGpuExpandDef mvfxDefaultGpuExpandDef(void);

    /// Adds the passes expanding an emitter's ribbons into triangle strips
    /// to the frame the host builds, as mvfxInstance_ExpandRibbons expands
    /// them on the CPU path: the passes of mvfxGpuSim_AddRibbons, each
    /// point two vertices across its width along its side (corner 0 on the
    /// side's back, 1 on it, the coordinate along as the corner's first),
    /// each segment six indices. Ribbons are taken whole while both
    /// buffers hold them; the counts are the vertices and indices there
    /// are and those written. What a host may rely on, where the passes
    /// come and what they count among: as mvfxGpuSim_AddRibbons.
    ///
    /// @param sim  The simulation.
    /// @param def  Where the strips go, from mvfxDefaultGpuExpandDef.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a
    ///         def without its cookie, or a def mvfxGpuSim_AddRibbons
    ///         refuses as invalid read with its vertices as points and its
    ///         indices as the list; else as mvfxGpuSim_AddRibbons.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_ExpandRibbons(mvfxGpuSim* sim,
                                                                    const mvfxGpuExpandDef* def);

    /// Adds the passes expanding an emitter's ribbons into tubes to the
    /// frame the host builds, as mvfxInstance_ExpandTubes expands them on
    /// the CPU path: the passes of mvfxGpuSim_AddRibbons, each point a
    /// ring of the def's sides + 1 vertices at half its width about it,
    /// turned along the ribbon by rotation-minimizing frames carried point
    /// to point (an invocation a ribbon), each segment sides quads, with
    /// caps a center and a ring at either end and their triangles. Ribbons
    /// are taken whole while both buffers hold them; the counts are the
    /// vertices and indices there are and those written. A frame is
    /// carried in binary32, so a ring's turn about its tangent may differ
    /// from the CPU path's by what binary32 gathers along the ribbon. What
    /// a host may rely on otherwise, where the passes come and what they
    /// count among: as mvfxGpuSim_AddRibbons; with 0 sides, as
    /// mvfxGpuSim_ExpandRibbons.
    ///
    /// @param sim  The simulation.
    /// @param def  Where the tubes go, from mvfxDefaultGpuExpandDef, with
    ///             its tube's sides and caps.
    /// @return As mvfxGpuSim_ExpandRibbons; `mvfx_errorInvalid` too for
    ///         sides out of range.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_ExpandTubes(mvfxGpuSim* sim,
                                                                  const mvfxGpuExpandDef* def);

    /// Adds the passes writing an emitter's plexus links of the last
    /// recorded step to the frame the host builds, as two-point ribbons,
    /// as mvfxInstance_WritePlexus writes them on the CPU path: a transfer
    /// pass, a pass in one workgroup taking the links and one writing
    /// their points. Each link the step wrote (mvfxGpuParticles.links, in
    /// their order) is a ribbon of its two particles' records, owner
    /// first, straight, the coordinate along 0 and 1, facing the eye, of
    /// the plexus width, its id the link's index; with the plexus fade its
    /// alpha is times one less its length over the distance (a sqrt and a
    /// division in binary32, within a relative 1e-4 of the CPU path's).
    /// The links that fit whole are written, none after the first that
    /// does not. What a host may rely on otherwise, where the passes come
    /// and what they count among: as mvfxGpuSim_AddRibbons. An emitter
    /// without particles at the instance's quality level, or without
    /// plexus links, writes no point; its counts, if asked for, are 0.
    ///
    /// @param sim  The simulation.
    /// @param def  Where the links go, from mvfxDefaultGpuRibbonsDef.
    /// @return As mvfxGpuSim_AddRibbons.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_AddPlexus(mvfxGpuSim* sim,
                                                                const mvfxGpuRibbonsDef* def);

    /// Adds the passes expanding an emitter's plexus links into triangle
    /// strips, as mvfxInstance_ExpandPlexus expands them on the CPU path:
    /// the passes of mvfxGpuSim_AddPlexus, each link four vertices and six
    /// indices, as mvfxGpuSim_ExpandRibbons expands a ribbon. The def's
    /// tube is not read.
    ///
    /// @param sim  The simulation.
    /// @param def  Where the strips go, from mvfxDefaultGpuExpandDef.
    /// @return As mvfxGpuSim_ExpandRibbons.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_ExpandPlexus(mvfxGpuSim* sim,
                                                                   const mvfxGpuExpandDef* def);

    /// Adds the passes expanding an emitter's particles into quads to the
    /// frame the host builds, as mvfxInstance_ExpandSprites expands them on
    /// the CPU path: four vertices a particle (left bottom, right bottom,
    /// left top, right top, at the corners' places with their corner
    /// fields) and six indices, in the order its records take, each quad
    /// turned as its sprite def's billboard says (the view's eye, forward
    /// and up its axes'). The first quads that fit both buffers are
    /// written; the counts are the strips': the vertices and indices there
    /// are (four and six a live particle) and those written. A transfer
    /// pass and a compute pass, 9
    /// more for a sort by place; a record pass of the emitter's
    /// (MVFX_GPU_RECORD_PASSES), the same frames and order as
    /// mvfxGpuSim_AddRecords. The def's tube is not read. An emitter
    /// without particles at the instance's quality level writes none; its
    /// counts, if asked for, are 0.
    ///
    /// @param sim  The simulation.
    /// @param def  Where the quads go, from mvfxDefaultGpuExpandDef.
    /// @return As mvfxGpuSim_ExpandRibbons.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_ExpandSprites(mvfxGpuSim* sim,
                                                                    const mvfxGpuExpandDef* def);

    /// Records the passes mvfxGpuSim_AddPasses added, after the frame is
    /// compiled: the plans' bytes written, the kernels dispatched. Nothing
    /// when it added none. The bytes are the frame's uploads
    /// (mvfxGpuSim_UploadBytes counts them): 1,960 an emitter (its
    /// numbers' 1,888 and its record map's 72; 28 more for one whose
    /// ribbons pin a head), 432 a force, 128 a collider
    /// and 80 a kill rule it has (8,104 at most), 96 more for one that
    /// writes events (its events' values over life: the clock and the
    /// map, all 1,952 at the first step), 96 more for a beam (its
    /// curve and noise), and 32 more for one with a neighbor grid (its
    /// clock and the anchor's shift) or 160 for one that steers or has a
    /// pair force (its targets too), and 16 a point of its path (all 752
    /// at the first step); 96 more for one with contacts (its sizes'
    /// clock and map; all 1,952, and its contact numbers' 48, at the
    /// first step), and 48 at the first step for one with plexus links
    /// (its plexus numbers); 48 a planned
    /// birth (a plan holds at most twice the emitter's capacity) and 64 a
    /// host's particle; at the first step and after a clear, 12 for its
    /// counts, 16 more for one spawning from events and 4 more for one
    /// finding neighbors; at the first step,
    /// its sources' 1,248 and the effect's baked tables its values read
    /// (1,032 a curve, 4,096 a gradient); and the values of each grid its
    /// forces and colliders read (mvfxGridView's bytes)
    /// at the first step, after each change and, for the instance's,
    /// after each move in the room the def keeps; and a mesh shape's mesh
    /// (mvfxGpuSim_MeshBytes) at the first step and after each change of
    /// it or of the mesh bound: which the device's frameUploadBytes must
    /// hold.
    ///
    /// @param sim  The simulation.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL simulation;
    ///         `mvfx_errorState` when the device refuses a command, such as
    ///         an upload past the frame's, or while it is destroyed (a call
    ///         from its allocator).
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxGpuSim_Record(mvfxGpuSim* sim);

    /// Counts the bytes the last mvfxGpuSim_Record uploaded: its step
    /// passes' as it states them, and its record passes' (1,952 a pass,
    /// lights passes' too, 4 for one of an emitter with no particles or
    /// lights) and ribbon passes' (2,096 a
    /// pass, 16 for one that writes no point), so a host can hold its
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
    /// step, joined by a ribbon's pinned head where it shows. The record
    /// holds that step's map and head, so a late read is exact for its
    /// step.
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
