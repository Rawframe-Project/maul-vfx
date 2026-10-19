// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A simulation's state, which its files share (sim.c, sim_records.c,
// sim_spawn.c, sim_mesh.c, sim_events.c, sim_grid.c, sim_contacts.c,
// sim_plexus.c): an emitter's buffers, passes and numbers, and the
// simulation's own.

#ifndef MAUL_VFX_RHI_SRC_SIM_STATE_H
#define MAUL_VFX_RHI_SRC_SIM_STATE_H

#include "allocator.h"
#include "grids.h"
#include "kernels.h"
#include "numbers.h"
#include "record.h"
#include "records.h"

#include "maul-rhi/frame.h"
#include "maul-vfx-rhi/sim.h"

// The buffers of the step an emitter has: its numbers, its births, its
// particles in the buffer they are in and the other, its statistics
// record, which births have room, the scan's blocks with their boxes and
// growths, the grids its forces read, its events.
enum
{
    BUFFER_STEP = 0,
    BUFFER_BIRTHS = 1,
    BUFFER_CURRENT = 2,
    BUFFER_OTHER = 3,
    BUFFER_STATISTICS = 4,
    BUFFER_ACCEPTED = 5,
    BUFFER_BLOCKS = 6,
    BUFFER_GRIDS = 7,
    BUFFER_EVENTS = 8,
    BUFFER_COUNT = 9
};

// The passes of an emitter's step: the plan's bytes, then the kernels.
enum
{
    PASS_WRITE = 0,
    PASS_ROOM = 1,
    PASS_UPDATE = 2,
    PASS_KILL = 3,
    // Its units moved by substeps and its births of units killed whole
    // (sim/units.wgsl), when it has units.
    PASS_UNITS = 4,
    PASS_REDUCE = 5,
    PASS_OFFSETS = 6,
    PASS_SCATTER = 7,
    // Its blocks' boxes taken again, when its contacts moved its particles.
    PASS_REBOX = 8,
    // Its births' ribbons, when it has ribbons.
    PASS_RIBBONS = 9,
    // Its points placed, when it is a beam.
    PASS_BEAM = 10,
    PASS_MEASURE = 11,
    // An emitter's events, when it writes any.
    PASS_EVENT_COUNT = 12,
    PASS_EVENT_OFFSETS = 13,
    PASS_EVENT_WRITE = 14,
    PASS_EVENT_VALUES = 15,
    // Its births from events, when it spawns from them, after the plan's
    // bytes: the source's events copied in, then the kernels.
    PASS_SPAWN_COPY = 16,
    PASS_SPAWN_COUNT = 17,
    PASS_SPAWN_SORT = 18,
    PASS_SPAWN_SCAN = 19,
    PASS_SPAWN_MERGE = 20,
    PASS_COUNT = 21
};

// The passes of an emitter's neighbor grid (sim_grid.c), after its room
// pass: its numbers' write, its keys, the sort's four radix passes
// (offsets and scatter each), its values, the query.
#define GRID_PASSES 12

// The passes of an emitter's particle contacts (sim_contacts.c), after
// its scatter: its numbers' write, its radii, its keys, the sort's four
// radix passes, a projection and its commit for each of the most
// iterations, its impulses and its kick.
#define CONTACT_PASSES (11 + 2 * MVFX_MAX_CONTACT_ITERATIONS + 2)

// The words of an emitter's contact numbers (contacts.wgsl's Contacts).
#define CONTACT_WORDS 12

// The buffers its contact passes bind (with its particles, statistics and
// grids): its record numbers (for its sizes over life), its contact
// numbers, its scratch words, its keys and indices, its digit counts and
// its counts.
enum
{
    CONTACT_RECORD = 0,
    CONTACT_NUMBERS = 1,
    CONTACT_SCRATCH = 2,
    CONTACT_SORT = 3,
    CONTACT_DIGITS = 4,
    CONTACT_COUNT = 5,
    CONTACT_BUFFERS = 6
};

// The passes of an emitter's plexus links (sim_plexus.c), after its
// bounds: its numbers' write (at the first step), its keys, the sort's
// four radix passes, its counts, its slots and its links.
#define PLEXUS_PASSES 13

// The words of an emitter's plexus numbers (plexus.wgsl's Plexus).
#define PLEXUS_WORDS 12

// The buffers its plexus passes bind (with its particles and statistics):
// its numbers, its keys and indices, its digit counts, its counts and
// slots, its links.
enum
{
    PLEXUS_NUMBERS = 0,
    PLEXUS_SORT = 1,
    PLEXUS_DIGITS = 2,
    PLEXUS_COUNTS = 3,
    PLEXUS_LINKS = 4,
    PLEXUS_BUFFERS = 5
};

// The buffers of an emitter's grid the grid passes bind (with its
// particles and statistics after the numbers): its numbers, its words,
// digit counts and count, its source's words and count.
enum
{
    GRID_NUMBERS = 0,
    GRID_WORDS = 1,
    GRID_DIGITS = 2,
    GRID_COUNT = 3,
    GRID_SOURCE_WORDS = 4,
    GRID_SOURCE_COUNT = 5,
    GRID_BUFFERS = 6
};

typedef struct Emitter
{
    // Its capacity, and how many particles its host queue holds; the
    // emitter whose events it spawns from (-1 for none), and the host
    // records its births buffer holds (its host queue's and its event
    // births').
    int32_t capacity;
    int32_t hostCapacity;
    int32_t spawnSource;
    int32_t hostRecords;
    // The words a particle takes: its arrays and the GPU's own.
    int32_t particleWords;
    mrhiBufferId particles[2];
    mrhiBufferId births;
    mrhiBufferId step;
    mrhiBufferId statistics;
    mrhiBufferId accepted;
    mrhiBufferId blocks;
    mrhiBufferId grids;
    // Its events' buffer, when it writes any or spawns from another's
    // (whose spawn region follows its own events), whether it writes any,
    // and the numbers the records kernel gives their values over life by
    // (a record pass's), with the frame's ids of what that pass binds
    // beside the step's.
    mrhiBufferId events;
    bool writesEvents;
    // Whether its births join ribbons (the ribbons pass, and its words
    // after the births' marks), and whether they pin a head (its words in
    // the record).
    bool ribbons;
    bool pinsHead;
    // Whether it is a beam, its points placed after the step.
    bool beam;
    // Its neighbor grid (sim_grid.c): its mask (0 for none: no emitter
    // queries it), what it does with one (MVFX_GRID_*, 0 for nothing) and
    // the emitter whose grid it queries; the grid's numbers, its keys and
    // indices with the values after them, its digit counts and the count
    // it holds; whether its numbers were all written and its count
    // emptied; the frame's ids of what its passes bind, its numbers, its
    // passes.
    uint32_t gridMask;
    uint32_t gridFlags;
    int32_t gridSource;
    mrhiBufferId gridNumbers;
    mrhiBufferId gridWords;
    mrhiBufferId gridDigits;
    mrhiBufferId gridCount;
    bool gridNumbersWritten;
    bool gridEmptied;
    mrhiResourceId gridResources[GRID_BUFFERS];
    uint32_t gridNumberWords[MVFX_GRID_WORDS];
    mrhiPassId gridPasses[GRID_PASSES];
    // Its particle contacts (sim_contacts.c): whether it has them and its
    // projections a step; its buffers; whether its numbers were all
    // written; the frame's ids of its buffers, its numbers, its passes.
    // Whether its particles are units (sim/units.wgsl), and the unit
    // pass's workgroups.
    bool units;
    uint32_t unitGroups;
    bool contacts;
    int32_t contactIterations;
    mrhiBufferId contactBuffers[CONTACT_BUFFERS];
    bool contactWritten;
    mrhiResourceId contactResources[CONTACT_BUFFERS];
    uint32_t contactRecordWords[MVFX_RECORD_WORDS];
    uint32_t contactWords[CONTACT_WORDS];
    mrhiPassId contactPasses[CONTACT_PASSES];
    // Its plexus links (sim_plexus.c): whether it has them; its buffers;
    // whether its numbers were written, and whether this step writes
    // them; the frame's ids of its buffers, its numbers, its passes.
    bool plexus;
    mrhiBufferId plexusBuffers[PLEXUS_BUFFERS];
    bool plexusWritten;
    bool plexusWriting;
    mrhiResourceId plexusResources[PLEXUS_BUFFERS];
    uint32_t plexusWords[PLEXUS_WORDS];
    mrhiPassId plexusPasses[PLEXUS_PASSES];
    mrhiBufferId eventNumbers;
    uint32_t eventWords[MVFX_RECORD_WORDS];
    // Whether all its event numbers were written: after, only the words a
    // plan changes are.
    bool eventNumbersWritten;
    mrhiResourceId eventResources[4];
    mvfxGpuGrids gridLayout;
    // The particle buffer the last recorded step left them in.
    int32_t current;
    // The step added and not yet recorded: its passes, the buffers as the
    // frame has them, its numbers, its map for the record and the plan's
    // births.
    bool added;
    mrhiPassId passes[PASS_COUNT];
    mrhiResourceId resources[BUFFER_COUNT];
    uint32_t words[MVFX_STEP_WORDS];
    mvfxStepSpans spans;
    // Where the baked tables are in the grids buffer, and whether the
    // sources' block is uploaded.
    mvfxStepTables tables;
    bool sourcesWritten;
    uint32_t map[MVFX_RECORD_MAP_WORDS];
    uint32_t head[MVFX_RECORD_HEAD_WORDS];
    const mvfxPlannedBirth* plannedBirths;
    int32_t birthCount;
    // Its record passes of the frame open, and its sort's keys and indices
    // and digit counts when it sorts by place.
    mvfxRecordPasses records;
    mrhiBufferId sort;
    mrhiBufferId digits;
    // A mesh shape's region: its room in the grids buffer in bytes, the
    // host's copy the region is written from, the serial of the mesh
    // written there (0 for none yet), and the serial and bytes of one to
    // write at the next record (0 for none).
    uint32_t meshBytes;
    uint32_t* meshImage;
    uint64_t meshSerial;
    uint64_t meshPending;
    uint32_t meshPendingBytes;
    // Its host births in the kernels' form, room for its host queue's.
    uint32_t* hostWords;
    int32_t hostCount;
    // For units held to the host's frames, the frames in the kernels' form
    // (room for the unit's attachFrameCapacity), how many the step has,
    // and their first byte in the births buffer.
    uint32_t* frameWords;
    int32_t frameCapacity;
    int32_t frameCount;
    uint64_t framesAt;
    bool cleared;
} Emitter;

struct mvfxGpuSim
{
    mvfxAllocator allocator;
    mvfxGpu* gpu;
    mrhiDevice* device;
    const mvfxContext* context;
    mvfxInstanceId instance;
    int32_t emitterCount;
    mvfxEmitterView views[MVFX_MAX_EMITTERS];
    // The order its emitters step in: each after the one it spawns from.
    int32_t order[MVFX_MAX_EMITTERS];
    Emitter emitters[MVFX_MAX_EMITTERS];
    // Its own 1-pixel depth and normal textures, bound where the host
    // gives none; the host's for the frame open (null ids for none); the
    // frame's ids of the two the step binds.
    mrhiTextureId ownDepth;
    mrhiTextureId ownNormals;
    mrhiResourceId hostDepth;
    mrhiResourceId hostNormals;
    mrhiResourceId bound[2];
    // The sRGB steps records write bytes by, and a word of its own a
    // record pass's count goes to where the host asks for none; words of
    // its own a record pass binds for the sort of an emitter that does
    // not sort.
    float srgb[MVFX_SRGB_STEPS];
    mrhiBufferId ownCount;
    mrhiBufferId ownSort;
    mrhiBufferId ownDigits;
    // A word of its own the step binds for the events of an emitter that
    // writes none; a grid's three of its own the grid passes bind for an
    // emitter that builds none.
    mrhiBufferId ownEvents;
    mrhiBufferId ownGrid[3];
    // The bytes the last mvfxGpuSim_Record uploaded.
    uint64_t uploaded;
    // Whether it is being destroyed: its changing calls from its
    // allocator meanwhile are refused (record mvfx-0001).
    bool ending;
};

// Records a kernel's pass over an emitter's buffers (sim.c).
bool mvfxRecordSimKernel(mvfxGpuSim* sim, const Emitter* emitter, int32_t p, mvfxKernel kernel,
                         uint32_t groups);

// The emitters' step order, each after the one it spawns from
// (sim_spawn.c); the pass copying a spawning emitter's source's events
// into its spawn region, added and recorded; its births from events
// recorded; its numbers for them written beside its step's.
void mvfxOrderSimEmitters(mvfxGpuSim* sim);
bool mvfxAddSpawnCopyPass(mvfxGpuSim* sim, Emitter* emitter);
bool mvfxRecordSpawn(mvfxGpuSim* sim, const Emitter* emitter, uint32_t groups);
mvfxResult mvfxSpawnStepNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan);

// An emitter's units held to the host's frames (sim_attach.c): the bytes
// the frames take in the births buffer; the step's frames staged and
// their numbers written for a plan; their upload in the write pass.
uint64_t mvfxSimFrameBytes(const Emitter* emitter);
void mvfxSimFrameNumbers(Emitter* emitter, const mvfxEmitterPlan* plan);
bool mvfxUploadSimFrames(mvfxGpuSim* sim, Emitter* emitter, mrhiPassId pass);

// An emitter's events (sim_events.c): the numbers of their values over
// life written for a step's plan; what the pass giving them binds beside
// the step's buffers imported; that pass added; its numbers' writes
// recorded into the step's write pass; its kernels recorded, when it
// writes events.
void mvfxSimEventNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan);
bool mvfxImportEventValues(mvfxGpuSim* sim, Emitter* emitter);
bool mvfxAddEventValuesPass(mvfxGpuSim* sim, Emitter* emitter);
bool mvfxWriteEventNumbers(mvfxGpuSim* sim, mrhiPassId pass, Emitter* emitter);
bool mvfxRecordSimEvents(mvfxGpuSim* sim, const Emitter* emitter, uint32_t blocks);

// An emitter's neighbor grid (sim_grid.c): what it does and its buffers
// made at the simulation's creation (false when the device refuses), and
// destroyed; its numbers written for a step's plan; its passes added
// after its room pass, the source's grid's ids taken from the source,
// which stepped first; recorded.
bool mvfxCreateSimGrid(mvfxGpuSim* sim, int32_t e);
void mvfxDestroySimGrid(mvfxGpuSim* sim, Emitter* emitter);
void mvfxSimGridNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan);
bool mvfxAddSimGridPasses(mvfxGpuSim* sim, Emitter* emitter);
bool mvfxRecordSimGrid(mvfxGpuSim* sim, Emitter* emitter);

// An emitter's particle contacts (sim_contacts.c): its buffers made at
// the simulation's creation (false when the device refuses), and
// destroyed; its numbers written for a step's plan; its passes added
// after its scatter; recorded.
bool mvfxCreateSimContacts(mvfxGpuSim* sim, int32_t e);
void mvfxDestroySimContacts(mvfxGpuSim* sim, Emitter* emitter);
void mvfxSimContactNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan);
bool mvfxAddSimContactPasses(mvfxGpuSim* sim, Emitter* emitter);
bool mvfxRecordSimContacts(mvfxGpuSim* sim, Emitter* emitter);

// An emitter's plexus links (sim_plexus.c): its buffers made at the
// simulation's creation (false when the device refuses), and destroyed;
// its numbers taken from its plan; its passes added after its bounds;
// recorded.
bool mvfxCreateSimPlexus(mvfxGpuSim* sim, int32_t e);
void mvfxDestroySimPlexus(mvfxGpuSim* sim, Emitter* emitter);
void mvfxSimPlexusNumbers(mvfxGpuSim* sim, int32_t e, const mvfxEmitterPlan* plan);
bool mvfxAddSimPlexusPasses(mvfxGpuSim* sim, Emitter* emitter);
bool mvfxRecordSimPlexus(mvfxGpuSim* sim, Emitter* emitter);

// A mesh shape's region (sim_mesh.c): laid out after emitter e's tables,
// with the host's copy, at the simulation's creation (mvfx_errorStale for
// a mesh gone, mvfx_errorCapacity past MVFX_GPU_MESH_BYTES or when the
// allocator fails); readied for a step, written into the copy when the
// mesh's serial is not the region's (mvfx_errorCapacity past the room);
// its upload recorded into the step's write pass.
mvfxResult mvfxLaySimMesh(mvfxGpuSim* sim, const mvfxGpuSimDef* def, int32_t e);
mvfxResult mvfxPlaceSimMesh(mvfxGpuSim* sim, int32_t e);
bool mvfxWriteSimMesh(mvfxGpuSim* sim, mrhiPassId pass, Emitter* emitter);

#endif
