// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles on a real device (lavapipe on CI) beside the CPU path,
// compared after every step (rhi_harness.h): their places, velocities,
// orientations, angular velocities and sleep within the tolerance (masses
// and the gyroscopic turn take square roots and division). Covered:
// spheres, capsules, boxes and hulls of sizes unequal along their axes
// tumbling under gravity and drag over several substeps (the gyroscopic
// term turning those of unequal inertias), newborns taking the rest of
// their step's substeps, sizes over life by age and by speed, in the
// world's space under a moving instance, and bodies coming to rest in a
// drag and falling asleep alone; spheres and capsules meeting planes,
// spheres and boxes from outside and in, the emitter's and the
// instance's, bouncing, rubbing and rolling, their strikes' events,
// counts and life lost (killing at once or past their age), newborns
// striking in their first step, and the contacts past the most dropped
// and counted; boxes meeting the same, a box collider face to face, edge
// to edge or on an edge; what is still refused (hulls colliding, grids,
// depth images, bodies meeting each other, kills within a step). Skips
// (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// A lopsided hull: a wedge with a spike.
static const float s_lopsided[21] = {0,    0, 0,    1, 0, 0,    0, 0.6f, 0,    0,   0,
                                     0.3f, 1, 0.1f, 0, 0, 0.4f, 0, 0.7f, 0.8f, 1.1f};

// Sprites as rigid bodies of a shape, sizes unequal along their axes,
// random orientations and fast angular velocities, four substeps.
static mvfxEmitterDef Bodies(mvfxRigidShape shape)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.3f, 0.2f}, .max = {0.2f, 0.5f, 0.3f}};
    emitter.orientationMode = mvfx_orientationRandom;
    emitter.angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {-20, -20, -20}, .max = {20, 20, 20}};
    emitter.motion.angularDrag = 0.3f;
    emitter.rigid = (mvfxRigidDef){.enabled = true,
                                   .shape = shape,
                                   .density = 500.0f,
                                   .substeps = 4,
                                   .staticFriction = 0.5f,
                                   .dynamicFriction = 0.4f};
    return emitter;
}

// The lopsided hull's mesh.
static const mvfxMeshDef s_hull = {
    .cookie = MVFX_MESH_DEF_COOKIE, .positions = s_lopsided, .vertexCount = 7};

static void Tumbled(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, bool moving,
                    const char* what)
{
    // A step at a time from the same state, within 1e-3: the inertias are
    // binary32 here and binary64 on the CPU path, and the gyroscopic turn
    // carries their rounding into the angular velocity a substep at a
    // time, which a body tumbling about its middle axis grows from step
    // to step (the Dzhanibekov effect).
    Compare(gpu, kernels,
            &(Run){.emitters = emitter,
                   .emitterCount = 1,
                   .moving = moving,
                   .pace = 0.001,
                   .adopting = true,
                   .tolerance = 1e-3f,
                   .mesh = emitter->rigid.shape == mvfx_rigidHull ? &s_hull : NULL},
            what);
}

// Bodies of a shape thrown down at colliders, living a second, bouncing
// back a share of their speed, their births, deaths and collisions
// written.
static mvfxEmitterDef Thrown(mvfxRigidShape shape, const mvfxCollider* colliders, int32_t count)
{
    mvfxEmitterDef emitter = Bodies(shape);
    emitter.lifetime = mvfxRangeSource(0.8f, 1.2f);
    emitter.velocity.direction[1] = -1.0f;
    emitter.colliders = colliders;
    emitter.colliderCount = count;
    emitter.collision = (mvfxCollision){.enabled = true};
    emitter.rigid.restitution = 0.4f;
    emitter.events = mvfx_eventMaskBirth | mvfx_eventMaskDeath | mvfx_eventMaskCollision;
    emitter.eventCapacity = 4096;
    return emitter;
}

// Compares colliding bodies a step at a time from the same state: a
// contact's friction holds or lets go, and a strike bounces or not, by its
// load and speed, which the paths' masses (binary32 here) part by their
// rounding, and a capsule lying flat on a face has its deepest point
// anywhere along it; a body near a collider may go apart (rhi_apart.h),
// at most 48 of the scene's, about 400 near at a step: an error in the
// solve parts most of them.
static void Met(Gpu* gpu, mvfxGpu* kernels, const Run* scene, const char* what)
{
    Run run = *scene;
    run.emitterCount = 1;
    run.colliding = !run.killing;
    run.adopting = true;
    run.tolerance = 1e-3f;
    run.mostApart = 48;
    Compare(gpu, kernels, &run, what);
}

static void TestShapes(Gpu* gpu, mvfxGpu* kernels)
{
    static const char* const names[4] = {"spheres tumbling", "capsules tumbling", "boxes tumbling",
                                         "hulls tumbling"};
    for (int32_t shape = 0; shape < 4; ++shape)
    {
        mvfxEmitterDef emitter = Bodies((mvfxRigidShape)shape);
        Tumbled(gpu, kernels, &emitter, false, names[shape]);
    }
    // Eight substeps in the world's space under a moving instance.
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox);
    emitter.rigid.substeps = 8;
    emitter.space = mvfx_spaceWorld;
    Tumbled(gpu, kernels, &emitter, true, "boxes in the world, the instance moving");
}

static void TestSizes(Gpu* gpu, mvfxGpu* kernels)
{
    // Sizes over life by age (one curve, then one an axis), then by speed:
    // the mass a body turns by is its size's at the step's end, by speed
    // its speed's at each substep.
    static const mvfxCurveKey grow[2] = {
        {.time = 0, .value = 0.5f, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 2.0f, .interpolation = mvfx_interpolationLinear}};
    static const mvfxCurveKey shrink[2] = {
        {.time = 0, .value = 1.5f, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 0.5f, .interpolation = mvfx_interpolationLinear}};
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox);
    emitter.overLife.size = (mvfxSizeOverLife){
        .enabled = true, .size = {{.value = 1.0f, .curve = {.keys = grow, .keyCount = 2}}}};
    Tumbled(gpu, kernels, &emitter, false, "boxes growing over life");
    emitter.overLife.size.perAxis = true;
    emitter.overLife.size.size[1] =
        (mvfxLifeCurve){.value = 1.0f, .curve = {.keys = shrink, .keyCount = 2}};
    emitter.overLife.size.size[2] = (mvfxLifeCurve){.value = 0.7f};
    Tumbled(gpu, kernels, &emitter, false, "boxes sized over life an axis each");
    emitter.overLife.size.input = mvfx_overSpeed;
    emitter.overLife.size.speedRange[0] = 1.0f;
    emitter.overLife.size.speedRange[1] = 4.0f;
    Tumbled(gpu, kernels, &emitter, false, "boxes sized by their speed");
}

static void TestFloor(Gpu* gpu, mvfxGpu* kernels)
{
    // Spheres and capsules thrown down on a sloped floor: they bounce,
    // then roll or slide down it, held by friction where it holds.
    static const mvfxCollider floor = {
        .kind = mvfx_colliderPlane, .center = {0, -0.7f, 0}, .normal = {0, 1, 0.2f}};
    mvfxEmitterDef emitter = Thrown(mvfx_rigidSphere, &floor, 1);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "spheres bounced off a sloped floor");
    emitter = Thrown(mvfx_rigidCapsule, &floor, 1);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "capsules bounced off a sloped floor");
    emitter = Thrown(mvfx_rigidBox, &floor, 1);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "boxes bounced off a sloped floor");
    // Five floors through one line, each its own patch: a capsule lying on
    // them has ten contacts, two past the most.
    static const mvfxCollider floors[5] = {
        {.kind = mvfx_colliderPlane, .center = {0, -0.7f, 0}, .normal = {0, 1, 0}},
        {.kind = mvfx_colliderPlane, .center = {0, -0.7f, 0}, .normal = {0.002f, 1, 0}},
        {.kind = mvfx_colliderPlane, .center = {0, -0.7f, 0}, .normal = {-0.002f, 1, 0}},
        {.kind = mvfx_colliderPlane, .center = {0, -0.7f, 0}, .normal = {0, 1, 0.002f}},
        {.kind = mvfx_colliderPlane, .center = {0, -0.7f, 0}, .normal = {0, 1, -0.002f}}};
    emitter = Thrown(mvfx_rigidCapsule, floors, 5);
    emitter.events = 0;
    Met(gpu, kernels, &(Run){.emitters = &emitter, .leastRigidCapped = 1},
        "capsules lying on five floors, contacts dropped");
    // A box lying on them has twenty.
    emitter.rigid.shape = mvfx_rigidBox;
    Met(gpu, kernels, &(Run){.emitters = &emitter, .leastRigidCapped = 1},
        "boxes lying on five floors, contacts dropped");
}

static void TestColliders(Gpu* gpu, mvfxGpu* kernels)
{
    // Kept within a sphere and within a turned box, each face of it its
    // own patch.
    static const mvfxCollider bowl = {.kind = mvfx_colliderSphere, .inside = true, .radius = 0.9f};
    mvfxEmitterDef emitter = Thrown(mvfx_rigidCapsule, &bowl, 1);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "capsules within a sphere");
    emitter = Thrown(mvfx_rigidBox, &bowl, 1);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "boxes within a sphere");
    static const mvfxCollider crate = {.kind = mvfx_colliderBox,
                                       .inside = true,
                                       .halfExtents = {0.8f, 0.8f, 0.8f},
                                       .rotation = {0, 0.19866933f, 0, 0.98006658f}};
    emitter = Thrown(mvfx_rigidSphere, &crate, 1);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "spheres within a turned box");
    emitter = Thrown(mvfx_rigidCapsule, &crate, 1);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "capsules within a turned box");
    emitter = Thrown(mvfx_rigidBox, &crate, 1);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "boxes within a turned box");
    // Kept out of a sphere and a turned box over a floor: capsules landing
    // across the box's edges rest on the lines where their cores cross
    // its faces.
    static const mvfxCollider obstacles[3] = {
        {.kind = mvfx_colliderPlane, .center = {0, -0.9f, 0}, .normal = {0, 1, 0}},
        {.kind = mvfx_colliderSphere, .center = {0.3f, -0.6f, 0}, .radius = 0.25f},
        {.kind = mvfx_colliderBox,
         .center = {-0.25f, -0.7f, 0.1f},
         .halfExtents = {0.3f, 0.15f, 0.25f},
         .rotation = {0.0499792f, 0.0998334f, 0, 0.99374f}}};
    emitter = Thrown(mvfx_rigidSphere, obstacles, 3);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "spheres on a sphere and a box");
    emitter = Thrown(mvfx_rigidCapsule, obstacles, 3);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "capsules on a sphere and a box");
    // Boxes meet the box face to face (a face clipped by the other's),
    // edge to edge or on an edge, and the sphere at their nearest point.
    emitter = Thrown(mvfx_rigidBox, obstacles, 3);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "boxes on a sphere and a box");
    // The instance's floor too, in the world's space under a moving
    // instance.
    static const mvfxCollider ground = {
        .kind = mvfx_colliderPlane, .center = {0, -1.0f, 0}, .normal = {0, 1, 0}};
    emitter = Thrown(mvfx_rigidCapsule, obstacles + 1, 2);
    emitter.space = mvfx_spaceWorld;
    emitter.collision.hostColliders = true;
    Met(gpu, kernels,
        &(Run){.emitters = &emitter,
               .moving = true,
               .pace = 0.001,
               .hostColliders = &ground,
               .hostColliderCount = 1},
        "capsules in the world on the instance's floor, the instance moving");
}

static void TestStrikes(Gpu* gpu, mvfxGpu* kernels)
{
    // Each strike takes a share of the life left: bodies bouncing on a
    // floor die past their age, their sizes over life at the life left.
    static const mvfxCollider floor = {
        .kind = mvfx_colliderPlane, .center = {0, -0.7f, 0}, .normal = {0, 1, 0}};
    static const mvfxCurveKey grow[2] = {
        {.time = 0, .value = 0.5f, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 1.5f, .interpolation = mvfx_interpolationLinear}};
    mvfxEmitterDef emitter = Thrown(mvfx_rigidSphere, &floor, 1);
    emitter.collision.lifeLoss = 0.9f;
    emitter.rigid.restitution = 0.8f;
    emitter.overLife.size = (mvfxSizeOverLife){
        .enabled = true, .size = {{.value = 1.0f, .curve = {.keys = grow, .keyCount = 2}}}};
    Met(gpu, kernels, &(Run){.emitters = &emitter, .killing = true},
        "spheres losing life at each strike");
    // A strike taking all of it kills at once; newborns thrown fast from
    // just above the floor strike in their first step.
    emitter = Thrown(mvfx_rigidCapsule, &floor, 1);
    emitter.collision.lifeLoss = 1.0f;
    emitter.shape.radius = 0.1f;
    emitter.shape.offset[1] = -0.35f;
    emitter.velocity.speed = (mvfxRange){20, 30};
    Met(gpu, kernels, &(Run){.emitters = &emitter, .killing = true, .least = -1},
        "capsules killed by their first strike, newborns too");
}

// Compares bodies coming to rest over the whole run, neither path taking
// the other's state: a settling scene is not chaotic, so the masses'
// rounding (binary32 here) must not carry them apart.
static void Settled(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, const char* what)
{
    Compare(gpu, kernels, &(Run){.emitters = emitter, .emitterCount = 1, .tolerance = 1e-4f}, what);
}

static void TestSleep(Gpu* gpu, mvfxGpu* kernels)
{
    // No gravity and a strong drag: bodies come to rest within their life
    // and fall asleep alone, held still, their velocities none.
    mvfxEmitterDef emitter = Bodies(mvfx_rigidCapsule);
    emitter.motion.gravity[1] = 0.0f;
    emitter.motion.drag = 30.0f;
    emitter.motion.angularDrag = 30.0f;
    emitter.lifetime = mvfxRangeSource(1.0f, 1.5f);
    emitter.rigid.sleepSpeed = 0.05f;
    emitter.rigid.sleepTime = 0.1f;
    Settled(gpu, kernels, &emitter, "capsules falling asleep in a drag, the whole run");
    emitter.rigid.shape = mvfx_rigidBox;
    Settled(gpu, kernels, &emitter, "boxes falling asleep in a drag, the whole run");
    // Capsules coming to rest on a floor fall asleep there, held in place
    // against gravity.
    static const mvfxCollider floor = {
        .kind = mvfx_colliderPlane, .center = {0, -0.7f, 0}, .normal = {0, 1, 0}};
    emitter = Thrown(mvfx_rigidCapsule, &floor, 1);
    emitter.rigid.restitution = 0.0f;
    emitter.rigid.sleepSpeed = 0.1f;
    emitter.rigid.sleepTime = 0.1f;
    emitter.lifetime = mvfxRangeSource(2.0f, 3.0f);
    emitter.events = 0;
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "capsules falling asleep on a floor");
}

// Whether a planned instance of an emitter is refused on the GPU for its
// rigid particles (a hull's the lopsided one).
static bool RefusedForRigid(mvfxGpu* kernels, const mvfxEmitterDef* emitter)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef def = *emitter;
    if (def.rigid.shape == mvfx_rigidHull)
    {
        CHECK(mvfxCreateMesh(context, &s_hull, &def.rigid.hull) == mvfx_success, "the hull");
    }
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &def;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.planned = true;
    mvfxInstanceId planned = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success;
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = planned;
    mvfxGpuSim* sim = NULL;
    mvfxGpuRefusal refusal;
    bool refused = made && mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_errorState &&
                   sim == NULL && refusal.emitter == 0 && refusal.features == MVFX_FEATURE_RIGID;
    mvfxDestroyContext(context);
    return refused;
}

static void TestRefused(mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox);
    emitter.collision.enabled = true;
    emitter.rigid.shape = mvfx_rigidHull;
    CHECK(RefusedForRigid(kernels, &emitter), "colliding hulls refused");
    emitter = Bodies(mvfx_rigidCapsule);
    emitter.collision.enabled = true;
    emitter.rigid.bodyContacts = 4;
    CHECK(RefusedForRigid(kernels, &emitter), "bodies meeting each other refused");
    emitter.rigid.bodyContacts = 0;
    emitter.collision.depthImage = true;
    emitter.collision.depthThickness = 0.3f;
    emitter.collision.depthSamples = 16;
    CHECK(RefusedForRigid(kernels, &emitter), "bodies meeting a depth image refused");
    emitter.collision.depthImage = false;
    emitter.collision.killOnCollision = true;
    CHECK(RefusedForRigid(kernels, &emitter), "bodies a contact kills refused");
    static const mvfxKillRule rule = {.kind = mvfx_killPastPlane, .normal = {0, 1, 0}};
    emitter = Bodies(mvfx_rigidBox);
    emitter.killRules = &rule;
    emitter.killRuleCount = 1;
    CHECK(RefusedForRigid(kernels, &emitter), "rigid particles killed within a step refused");
}

static void TestHostGrid(Gpu* gpu, mvfxGpu* kernels)
{
    // The instance's heightfield, which bodies do not meet on the GPU yet:
    // the step refused, no pass added.
    static const float heights[4] = {0};
    mvfxEmitterDef emitter = Thrown(mvfx_rigidSphere, NULL, 0);
    emitter.collision.hostColliders = true;
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(
        kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .hostGridBytes = 4096}, &effect);
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 2;
    def.size[2] = 2;
    def.values = heights;
    mvfxCollider ground = {.kind = mvfx_colliderHeightfield,
                           .center = {0, -0.5f, 0},
                           .halfExtents = {1, 0.1f, 1},
                           .rotation = {0, 0, 0, 1}};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mvfxCreateGrid(RunContext(), &def, &ground.grid) == mvfx_success &&
              mvfxInstance_SetColliders(RunContext(), PlannedInstance(), &ground, 1) ==
                  mvfx_success &&
              mvfxContext_StepTicks(RunContext(), StepTicks()) == mvfx_success &&
              mrhiBeginFrame(gpu->device, &frame) == mrhi_success &&
              mvfxGpuSim_AddPasses(sim) == mvfx_errorState,
          "bodies meeting the instance's heightfield refused at the step");
    (void)mrhiDropFrame(gpu->device);
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestRefused(kernels);
    TestHostGrid(gpu, kernels);
    TestShapes(gpu, kernels);
    TestSizes(gpu, kernels);
    TestSleep(gpu, kernels);
    TestFloor(gpu, kernels);
    TestColliders(gpu, kernels);
    TestStrikes(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
