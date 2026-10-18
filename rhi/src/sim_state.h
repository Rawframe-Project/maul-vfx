// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A simulation's state, which its files share (sim.c, sim_records.c): an
// emitter's buffers, passes and numbers, and the simulation's own.

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
    PASS_REDUCE = 4,
    PASS_OFFSETS = 5,
    PASS_SCATTER = 6,
    PASS_MEASURE = 7,
    // An emitter's events, when it writes any.
    PASS_EVENT_COUNT = 8,
    PASS_EVENT_OFFSETS = 9,
    PASS_EVENT_WRITE = 10,
    PASS_EVENT_VALUES = 11,
    // Its births from events, when it spawns from them, after the plan's
    // bytes: the source's events copied in, then the kernels.
    PASS_SPAWN_COPY = 12,
    PASS_SPAWN_COUNT = 13,
    PASS_SPAWN_SORT = 14,
    PASS_SPAWN_SCAN = 15,
    PASS_SPAWN_MERGE = 16,
    PASS_COUNT = 17
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
    const mvfxPlannedBirth* plannedBirths;
    int32_t birthCount;
    // Its record passes of the frame open, and its sort's keys and indices
    // and digit counts when it sorts by place.
    mvfxRecordPasses records;
    mrhiBufferId sort;
    mrhiBufferId digits;
    // Its host births in the kernels' form, room for its host queue's.
    uint32_t* hostWords;
    int32_t hostCount;
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
    // writes none.
    mrhiBufferId ownEvents;
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

#endif
