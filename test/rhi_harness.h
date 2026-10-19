// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the GPU part's tests share (rhi_harness.c): a device on the first
// adapter (lavapipe on CI), the object's kernels, and runs of effects on
// planned instances simulated on the device beside the same effects on
// the CPU path, compared after every step.

#ifndef MAUL_VFX_TEST_RHI_HARNESS_H
#define MAUL_VFX_TEST_RHI_HARNESS_H

#include "maul-rhi/instance.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

#include <stdbool.h>
#include <stdint.h>

// A sprite's capacity, a run's steps and a step's ticks by default; a
// depth image's side in pixels.
#define CAPACITY   1024
#define STEPS      40
#define STEP_TICKS (MVFX_TICKS_PER_SECOND / 60)
#define DEPTH_SIDE 64

// The byte a records buffer is filled with before each step's records.
#define RECORD_FILL 0xEEu

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

// A run of an effect: a CPU instance and a planned one side by side, the
// GPU's particles compared with the CPU path's after every step. The
// instance moves and turns when moving, and is cleared and restarted at
// half time when clearing.
typedef struct Run
{
    const mvfxEmitterDef* emitters;
    int32_t emitterCount;
    bool moving;
    bool clearing;
    // Its steps, STEPS for 0; the most the first emitter must hold at
    // some step, 100 for 0, and the second, 65 for 0 (none for -1: its
    // particles never outlive a step, or it has no room).
    int32_t steps;
    int32_t least;
    int32_t secondLeast;
    // Whether the first emitter's particles must be killed on the CPU path
    // over the run (a scene that kills).
    bool killing;
    // Whether its particles must meet colliders over the run, and the
    // instance's colliders (none for NULL).
    bool colliding;
    const mvfxCollider* hostColliders;
    int32_t hostColliderCount;
    // Whether the instances stand still far from the origin, past where
    // binary32 places keep a noise's lattice.
    bool far;
    // A grid made in the run's context for its emitters' vector fields
    // (NULL for none), and whether it is given new values at half time.
    const mvfxGridDef* grid;
    bool animated;
    // How fast a moving instance goes, a share of Move's (0 for all):
    // particles a fast instance leaves behind are far from their anchor,
    // where binary32 places cannot hold a field's detail.
    double pace;
    // Grids made in the run's context for its grid colliders (a collider
    // of the run naming grid {.index1 = n} reads the nth, from 1), the
    // bytes the simulation keeps for the instance's, and the instance's
    // colliders from half time on (none for NULL).
    const mvfxGridDef* colliderGrids;
    int32_t colliderGridCount;
    int64_t hostGridBytes;
    const mvfxCollider* laterHostColliders;
    int32_t laterHostColliderCount;
    // The largest relative difference a place or a velocity may have,
    // 1e-4 for 0: a grid collider's normal, a difference across its
    // field's cells, moves with a contact a halving placed differently.
    float tolerance;
    // Whether each particle's death tick is compared too.
    bool deathTicks;
    // The defaults of the instances' parameters, four floats each (none
    // for NULL).
    const float* parameters;
    int32_t parameterCount;
    // Whether both instances' parameters are set anew before each step
    // (each one's x, y and z turning about its default), as a host aims a
    // beam.
    bool aiming;
    // A depth image both instances collide with (none for NULL): the CPU
    // instance's with its pixels, the planned one's without, its
    // simulation's rendered from the same pixels.
    const mvfxDepthImage* depth;
    // Particles the host queues for the first emitter of both instances
    // before each step, placed about the instance's origin (none for
    // NULL).
    const mvfxSpawnRequest* spawn;
    // A layout the first emitter's records are written in by both paths
    // and compared every step (none for NULL), and the most records the
    // buffer holds; whether a sort by place must keep the CPU path's
    // order exactly (every key tied), whether each record's words must
    // be the CPU path's exactly (values exact on both paths), and the
    // view (one from the origin's side for NULL).
    const mvfxLayoutDef* layout;
    int64_t recordCapacity;
    bool tied;
    bool exact;
    const mvfxView* view;
    // A mesh made in the run's context for its emitters of a mesh shape
    // (none for NULL); new positions it takes at half time, as a skinned
    // mesh does (none for NULL); a second mesh both instances' first
    // emitter is bound to at half time (none for NULL); and the bytes the
    // simulation keeps for a mesh beyond the first.
    const mvfxMeshDef* mesh;
    const float* laterPositions;
    const mvfxMeshDef* laterMesh;
    int64_t hostMeshBytes;
    // Whether the host breaks both instances' ribbons every seventh step,
    // and the ribbon breaks the GPU must make itself over the run (none
    // needed for 0).
    bool breaking;
    int32_t leastBreaks;
    // Whether the layout's records are ribbon points, written by
    // mvfxGpuSim_AddRibbons and by mvfxInstance_WriteRibbons from the
    // GPU's places and distances (the pools alike), compared with their
    // lists and counts every step, and the most ribbons the list holds.
    bool ribbons;
    int64_t ribbonCapacity;
    // Whether those ribbons are expanded into strips instead
    // (mvfxGpuSim_ExpandRibbons and mvfxInstance_ExpandRibbons), the list
    // their indices and ribbonCapacity the indices it holds; or into
    // tubes of these sides and caps (mvfxGpuSim_ExpandTubes and
    // mvfxInstance_ExpandTubes) for sides past 0.
    bool strips;
    mvfxTubeDef tube;
    // Whether the strips' buffers take the particles expanded into quads
    // instead (mvfxGpuSim_ExpandSprites and mvfxInstance_ExpandSprites;
    // strips and ribbons set too).
    bool sprites;
    // The emitter whose records are written (the first for 0).
    int32_t recordEmitter;
    // Whether the records are its lights instead (mvfxGpuSim_AddLights
    // and mvfxInstance_WriteLights, recordCapacity the most the buffer
    // holds; no layout needed), and the most lights it must have carried
    // at some step.
    bool lights;
    int32_t leastLights;
    // Whether the neighbor attributes must be the CPU path's words exactly
    // (places exact on both paths; otherwise the counts are, and the
    // offsets within the tolerance), and the queries their cap must have
    // stopped over the run (none needed for 0).
    bool exactNeighbors;
    int32_t leastCapped;
    // A quality level both instances play at (the full one for NULL): an
    // emitter it leaves out has no room on either path.
    const mvfxQualityLevel* quality;
    // The particles whose contacts passed the most the first emitter must
    // have counted over the run (none needed for 0).
    int32_t leastContactsCapped;
    // The most links the first emitter must have written at some step,
    // and the drops it must have counted over the run (none needed for
    // 0).
    int32_t leastLinks;
    int32_t leastDropped;
    // Whether the CPU path takes the GPU's places and velocities after
    // each step, which then compares one step from the same state:
    // contacts decide on places the paths hold within the tolerance, and
    // a pair that close to touching may touch on one path only, which
    // would part the paths for the rest of the run.
    bool adopting;
} Run;

// The scenes a test runs on the device.
typedef void (*SceneSuite)(Gpu* gpu, mvfxGpu* kernels);

// Runs a suite on the first adapter's device (software allowed) with the
// object's kernels: 77 without an adapter, unless MVFX_RHI_REQUIRED is set
// (1); 1 when a check of the harness's failed; 0 otherwise.
int RunSuite(SceneSuite suite);

// A sprite emitter: a sphere's shell, cone velocities, gravity and drag,
// a burst; in the world.
mvfxEmitterDef Sprite(void);

// Opens a run's context, effect and instances, the CPU one and the
// planned one, and the planned one's simulation (NULL when refused).
mvfxGpuSim* OpenRun(mvfxGpu* kernels, const Run* run, mvfxEffectId* effectOut);

// Runs an effect on both paths, compared after every step; what names it.
void Compare(Gpu* gpu, mvfxGpu* kernels, const Run* run, const char* what);

// Simulates the context's last step on the device, the frame submitted
// and waited for; submits the frame recorded and waits for it; reads an
// emitter's particles and statistics record back.
bool Simulate(Gpu* gpu, mvfxGpuSim* sim);
bool Finish(Gpu* gpu);
bool ReadBack(Gpu* gpu, const mvfxGpuSim* sim, int32_t emitter);

// The run open: its context, its planned instance, the ticks a step takes
// (STEP_TICKS until set), the statistics record last read back.
mvfxContext* RunContext(void);
mvfxInstanceId PlannedInstance(void);
int64_t StepTicks(void);
void SetStepTicks(int64_t ticks);
const uint32_t* StatisticsRecord(void);

#endif
