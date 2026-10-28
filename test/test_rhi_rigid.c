// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles on a real device (lavapipe on CI) beside the CPU path,
// compared after every step (rhi_harness.h): their places, velocities,
// orientations, angular velocities and sleep within the tolerance (masses
// and the free flow take square roots and division). Covered: spheres,
// capsules, boxes and hulls of sizes unequal along their axes tumbling
// under gravity and drag over several substeps (the free flow turning
// those of unequal inertias), boxes spun near an axis for 10 s and spun
// at up to 800 rad/s, rods down to 1e-6 of their length across spun
// about it and tumbling, and spun for 10 s, newborns taking the rest of
// their step's substeps, sizes over life by age and by speed, in the
// world's space under a moving instance, and bodies coming to rest in a
// drag and falling asleep alone, held in the air under gravity; spheres
// and capsules meeting planes, spheres and boxes from outside and in, the
// emitter's and the instance's, bouncing, rubbing and rolling, their
// strikes' events, counts and life lost (killing at once, past their age
// or in the rest of the step), newborns striking in their first step, and
// the contacts past the most dropped and counted, newborns' too; boxes
// meeting the same, a box collider face to face, edge to edge or on an
// edge, and a sphere's center within them; hulls meeting planes and what
// keeps them within; spheres, capsules, boxes and hulls on heightfields
// and distance grids, the emitter's and the instance's, and on a depth
// image's terraces (and boxes at its wall); hulls on a sphere and a box
// they are kept out of, and born into a box; capsules of the largest
// size on grids; bodies near the ends of sizes and densities, in their
// own units, tumbling and thrown on a floor; spheres and capsules sized
// by a parameter of unequal axes or inheriting unequal sizes, their axes
// the shape does not set taking X; what is still refused (kills
// within a step; bodies meeting each other are rhi_pairs'). Skips (77)
// without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

#include <math.h>
#include <stdio.h>

// A lopsided hull: a wedge with a spike.
static const float s_lopsided[21] = {0,    0, 0,    1, 0, 0,    0, 0.6f, 0,    0,   0,
                                     0.3f, 1, 0.1f, 0, 0, 0.4f, 0, 0.7f, 0.8f, 1.1f};

// Sprites as rigid bodies of a shape, sizes unequal along the axes it
// sets, random orientations and fast angular velocities, four substeps.
static mvfxEmitterDef Bodies(mvfxRigidShape shape)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.3f, 0.2f}, .max = {0.2f, 0.5f, 0.3f}};
    // A sphere's Y and Z and a capsule's Z are its X (mvfxRigidShape).
    for (int32_t k = shape == mvfx_rigidSphere ? 1 : 2; shape <= mvfx_rigidCapsule && k < 3; ++k)
    {
        emitter.size.min[k] = emitter.size.min[0];
        emitter.size.max[k] = emitter.size.max[0];
    }
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
    // A step at a time from the same state, within 1e-3: the inertias and
    // the free flow are binary32 here and binary64 on the CPU path, and
    // the flow carries their rounding into the angular velocity a substep
    // at a time, which a body tumbling about its middle axis grows from
    // step to step (the Dzhanibekov effect).
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
// at most the scene's bound, half again the count measured on lavapipe
// (at least 4): a change to one feature of the solve parts the bodies at
// it, past the bound, though most bodies never meet it.
static void Met(Gpu* gpu, mvfxGpu* kernels, const Run* scene, const char* what)
{
    CHECK(scene->mostApart > 0, "a scene's bound on the bodies apart");
    Run run = *scene;
    run.emitterCount = 1;
    run.colliding = !run.killing;
    run.adopting = true;
    run.tolerance = 1e-3f;
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
    Met(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter},
        "spheres bounced off a sloped floor");
    emitter = Thrown(mvfx_rigidCapsule, &floor, 1);
    Met(gpu, kernels, &(Run){.mostApart = 8, .emitters = &emitter},
        "capsules bounced off a sloped floor");
    emitter = Thrown(mvfx_rigidBox, &floor, 1);
    Met(gpu, kernels, &(Run){.mostApart = 11, .emitters = &emitter},
        "boxes bounced off a sloped floor");
    emitter = Thrown(mvfx_rigidHull, &floor, 1);
    Met(gpu, kernels, &(Run){.mostApart = 18, .emitters = &emitter, .mesh = &s_hull},
        "hulls bounced off a sloped floor");
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
    Met(gpu, kernels, &(Run){.mostApart = 9, .emitters = &emitter, .leastRigidCapped = 1},
        "capsules lying on five floors, contacts dropped");
    // A box lying on them has twenty.
    emitter.rigid.shape = mvfx_rigidBox;
    Met(gpu, kernels, &(Run){.mostApart = 21, .emitters = &emitter, .leastRigidCapped = 1},
        "boxes lying on five floors, contacts dropped");
    // Capsules born lying on them, 5 mm in, drop two of their ten
    // contacts in their first step; living a fifth of a second, a tenth
    // of the bodies are newborns, their drops counted with the rest.
    emitter = Thrown(mvfx_rigidCapsule, floors, 5);
    emitter.events = 0;
    emitter.lifetime = mvfxRangeSource(0.15f, 0.25f);
    emitter.shape = (mvfxShape){.kind = mvfx_shapeBox,
                                .rotation = {0, 0, 0, 1},
                                .offset = {0, -0.605f, 0},
                                .halfExtents = {0.5f, 0, 0.5f}};
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.2f, 0.6f, 0.2f}, .max = {0.2f, 0.6f, 0.2f}};
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0, 0, 1.5707963f}, .max = {0, 0, 1.5707963f}};
    emitter.angularVelocity = (mvfxVectorSource){0};
    emitter.velocity.speed = (mvfxRange){0.1f, 0.2f};
    Met(gpu, kernels, &(Run){.mostApart = 36, .emitters = &emitter, .leastRigidCapped = 1},
        "capsules born lying on five floors, contacts dropped");
}

static void TestColliders(Gpu* gpu, mvfxGpu* kernels)
{
    // Kept within a sphere and within a turned box, each face of it its
    // own patch.
    static const mvfxCollider bowl = {.kind = mvfx_colliderSphere, .inside = true, .radius = 0.9f};
    mvfxEmitterDef emitter = Thrown(mvfx_rigidCapsule, &bowl, 1);
    Met(gpu, kernels, &(Run){.mostApart = 15, .emitters = &emitter}, "capsules within a sphere");
    emitter = Thrown(mvfx_rigidBox, &bowl, 1);
    Met(gpu, kernels, &(Run){.mostApart = 15, .emitters = &emitter}, "boxes within a sphere");
    emitter = Thrown(mvfx_rigidHull, &bowl, 1);
    Met(gpu, kernels, &(Run){.mostApart = 12, .emitters = &emitter, .mesh = &s_hull},
        "hulls within a sphere");
    static const mvfxCollider crate = {.kind = mvfx_colliderBox,
                                       .inside = true,
                                       .halfExtents = {0.8f, 0.8f, 0.8f},
                                       .rotation = {0, 0.19866933f, 0, 0.98006658f}};
    emitter = Thrown(mvfx_rigidSphere, &crate, 1);
    Met(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter}, "spheres within a turned box");
    emitter = Thrown(mvfx_rigidCapsule, &crate, 1);
    Met(gpu, kernels, &(Run){.mostApart = 9, .emitters = &emitter}, "capsules within a turned box");
    emitter = Thrown(mvfx_rigidBox, &crate, 1);
    Met(gpu, kernels, &(Run){.mostApart = 15, .emitters = &emitter}, "boxes within a turned box");
    emitter = Thrown(mvfx_rigidHull, &crate, 1);
    Met(gpu, kernels, &(Run){.mostApart = 24, .emitters = &emitter, .mesh = &s_hull},
        "hulls within a turned box");
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
    Met(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter},
        "spheres on a sphere and a box");
    emitter = Thrown(mvfx_rigidCapsule, obstacles, 3);
    Met(gpu, kernels, &(Run){.mostApart = 48, .emitters = &emitter},
        "capsules on a sphere and a box");
    // Boxes meet the box face to face (a face clipped by the other's),
    // edge to edge or on an edge, and the sphere at their nearest point.
    emitter = Thrown(mvfx_rigidBox, obstacles, 3);
    Met(gpu, kernels, &(Run){.mostApart = 15, .emitters = &emitter}, "boxes on a sphere and a box");
    // Hulls meet the sphere at their nearest point (GJK) and the box by
    // the polytopes' search (GJK, EPA) and the faces' manifold.
    emitter = Thrown(mvfx_rigidHull, obstacles, 3);
    Met(gpu, kernels, &(Run){.mostApart = 26, .emitters = &emitter, .mesh = &s_hull},
        "hulls on a sphere and a box");
    // Born into a turned box they are kept out of: pushed out from their
    // first substep without the push becoming velocity, a capsule's core
    // point within the box leaving by its nearest face.
    static const mvfxCollider core[2] = {
        {.kind = mvfx_colliderPlane, .center = {0, -0.9f, 0}, .normal = {0, 1, 0}},
        {.kind = mvfx_colliderBox,
         .halfExtents = {0.25f, 0.25f, 0.25f},
         .rotation = {0.0499792f, 0.0998334f, 0, 0.99374f}}};
    emitter = Thrown(mvfx_rigidCapsule, core, 2);
    Met(gpu, kernels, &(Run){.mostApart = 45, .emitters = &emitter}, "capsules born into a box");
    emitter = Thrown(mvfx_rigidBox, core, 2);
    Met(gpu, kernels, &(Run){.mostApart = 21, .emitters = &emitter}, "boxes born into a box");
    emitter = Thrown(mvfx_rigidHull, core, 2);
    Met(gpu, kernels, &(Run){.mostApart = 15, .emitters = &emitter, .mesh = &s_hull},
        "hulls born into a box");
    // Spheres the host places at rest well within it, each near one face
    // (a tenth from it, a quarter from those beside): each leaves by that
    // face. (A capsule there lies nearly flat on the face, its deepest
    // point anywhere along its core, and its push out turns it apart.)
    static const double within[12] = {0.15, 0, 0, -0.15, 0, 0, 0, 0.15, 0, 0, 0, -0.15};
    static const float still[12] = {0};
    static const mvfxSpawnRequest placed = {.count = 4, .positions = within, .velocities = still};
    emitter = Thrown(mvfx_rigidSphere, core, 2);
    emitter.rate = 0.0f;
    emitter.burstCount = 0;
    emitter.hostSpawnCapacity = 8;
    Met(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter, .spawn = &placed, .least = -1},
        "spheres placed within a box");
    // Capsules placed at rest tilted through the box's top face, one end of
    // the core within and one without: the core's depth changes along it,
    // so its deepest point is its inner end, and where it crosses the face
    // holds it too.
    static const double across[12] = {0, 0.25, 0, 0.1, 0.25, 0.1, -0.1, 0.25, 0, 0, 0.25, -0.1};
    static const mvfxSpawnRequest tilted = {.count = 4, .positions = across, .velocities = still};
    emitter = Thrown(mvfx_rigidCapsule, core, 2);
    emitter.rate = 0.0f;
    emitter.burstCount = 0;
    emitter.hostSpawnCapacity = 8;
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.4f, 0.1f}, .max = {0.1f, 0.4f, 0.1f}};
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {0, 0, 0.6f}, .max = {0, 0, 0.6f}};
    emitter.angularVelocity = (mvfxVectorSource){0};
    Met(gpu, kernels, &(Run){.mostApart = 36, .emitters = &emitter, .spawn = &tilted, .least = -1},
        "capsules placed through a box's face");
    // Boxes placed around a small ball, its center within each, a tenth
    // and a half nearer one face than the others: pushed out through that
    // face, away from the center.
    static const mvfxCollider ball[2] = {
        {.kind = mvfx_colliderPlane, .center = {0, -0.9f, 0}, .normal = {0, 1, 0}},
        {.kind = mvfx_colliderSphere, .radius = 0.05f}};
    emitter = Thrown(mvfx_rigidBox, ball, 2);
    emitter.rate = 0.0f;
    emitter.burstCount = 0;
    emitter.hostSpawnCapacity = 8;
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.6f, 0.6f, 0.6f}, .max = {0.6f, 0.6f, 0.6f}};
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation = (mvfxVectorSource){.kind = mvfx_sourceRange};
    emitter.angularVelocity = (mvfxVectorSource){0};
    Met(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter, .spawn = &placed, .least = -1},
        "boxes placed around a ball");
    // The instance's floor too, in the world's space under a moving
    // instance.
    static const mvfxCollider ground = {
        .kind = mvfx_colliderPlane, .center = {0, -1.0f, 0}, .normal = {0, 1, 0}};
    emitter = Thrown(mvfx_rigidCapsule, obstacles + 1, 2);
    emitter.space = mvfx_spaceWorld;
    emitter.collision.hostColliders = true;
    Met(gpu, kernels,
        &(Run){.mostApart = 14,
               .emitters = &emitter,
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
    Met(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter, .killing = true},
        "spheres losing life at each strike");
    // A strike scales the life: born just above the floor, living a
    // second, they strike at 15 to 50 ms of age, left 40 ms of life; many
    // die in the rest of the strike's step, their deaths written the next
    // step at their ticks, before its start.
    emitter = Thrown(mvfx_rigidSphere, &floor, 1);
    emitter.lifetime = mvfxRangeSource(1.0f, 1.0f);
    emitter.shape.radius = 0.1f;
    emitter.shape.offset[1] = -0.45f;
    emitter.collision.lifeLoss = 0.96f;
    Met(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter, .killing = true, .least = 10},
        "spheres dying in the rest of a strike's step");
    // A strike taking all of it kills at once; newborns thrown fast from
    // just above the floor strike in their first step.
    emitter = Thrown(mvfx_rigidCapsule, &floor, 1);
    emitter.collision.lifeLoss = 1.0f;
    emitter.shape.radius = 0.1f;
    emitter.shape.offset[1] = -0.35f;
    emitter.velocity.speed = (mvfxRange){20, 30};
    Met(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter, .killing = true, .least = -1},
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
    // Under gravity the drag holds them at a third of a meter a second,
    // below the sleep speed: asleep, they are held in the air.
    emitter.rigid.shape = mvfx_rigidCapsule;
    emitter.motion.gravity[1] = -9.8f;
    emitter.rigid.sleepSpeed = 0.5f;
    Settled(gpu, kernels, &emitter, "capsules falling asleep in a drag, held in the air");
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
    Met(gpu, kernels, &(Run){.mostApart = 32, .emitters = &emitter},
        "capsules falling asleep on a floor");
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
    mvfxEmitterDef emitter = Bodies(mvfx_rigidCapsule);
    emitter.collision.enabled = true;
    emitter.collision.depthImage = true;
    emitter.collision.depthThickness = 0.3f;
    emitter.collision.depthSamples = 16;
    emitter.collision.killOutsideView = true;
    CHECK(RefusedForRigid(kernels, &emitter), "bodies killed out of the view refused");
    emitter.collision.depthImage = false;
    emitter.collision.killOutsideView = false;
    emitter.collision.killOnCollision = true;
    CHECK(RefusedForRigid(kernels, &emitter), "bodies a contact kills refused");
    static const mvfxKillRule rule = {.kind = mvfx_killPastPlane, .normal = {0, 1, 0}};
    emitter = Bodies(mvfx_rigidBox);
    emitter.killRules = &rule;
    emitter.killRuleCount = 1;
    CHECK(RefusedForRigid(kernels, &emitter), "rigid particles killed within a step refused");
}

// Heights over 9 by 9 points, a gentle swell; signed distances over 8
// points each way to a sphere of radius 0.25 at the middle of a box of
// half size 0.4.
static float s_heights[81];
static float s_distances[512];

static void MakeGrids(mvfxGridDef grids[2])
{
    for (int32_t p = 0; p < 81; ++p)
    {
        s_heights[p] = 0.15f * sinf(1.3f * (float)(p % 9)) + 0.1f * cosf(0.9f * (float)(p / 9));
    }
    for (int32_t p = 0; p < 512; ++p)
    {
        float x = -0.4f + 0.8f * (float)(p % 8) / 7.0f;
        float y = -0.4f + 0.8f * (float)(p / 8 % 8) / 7.0f;
        float z = -0.4f + 0.8f * (float)(p / 64) / 7.0f;
        s_distances[p] = sqrtf(x * x + y * y + z * z) - 0.25f;
    }
    grids[0] = mvfxDefaultGridDef();
    grids[0].size[0] = 9;
    grids[0].size[2] = 9;
    grids[0].values = s_heights;
    grids[1] = mvfxDefaultGridDef();
    grids[1].size[0] = 8;
    grids[1].size[1] = 8;
    grids[1].size[2] = 8;
    grids[1].values = s_distances;
}

static void TestGrids(Gpu* gpu, mvfxGpu* kernels)
{
    // A heightfield floor and a turned ball of a distance grid in the
    // bodies' way, met by samples: a sphere's center, a capsule's core, a
    // box's corners, its faces' centers and its edges where the surface
    // bulges under them.
    mvfxGridDef grids[2];
    MakeGrids(grids);
    static const mvfxCollider own[2] = {{.kind = mvfx_colliderHeightfield,
                                         .center = {0, -0.8f, 0},
                                         .halfExtents = {1, 0.1f, 1},
                                         .rotation = {0, 0, 0, 1},
                                         .grid = {.index1 = 1}},
                                        {.kind = mvfx_colliderDistance,
                                         .center = {-0.2f, -0.5f, 0.1f},
                                         .halfExtents = {0.4f, 0.4f, 0.4f},
                                         .rotation = {0, 0.19509032f, 0, 0.98078528f},
                                         .grid = {.index1 = 2}}};
    static const char* const names[4] = {"spheres on a heightfield and a distance grid",
                                         "capsules on a heightfield and a distance grid",
                                         "boxes on a heightfield and a distance grid",
                                         "hulls on a heightfield and a distance grid"};
    static const int32_t bounds[4] = {4, 12, 27, 18};
    for (int32_t shape = 0; shape < 4; ++shape)
    {
        mvfxEmitterDef emitter = Thrown((mvfxRigidShape)shape, own, 2);
        Met(gpu, kernels,
            &(Run){.mostApart = bounds[shape],
                   .emitters = &emitter,
                   .colliderGrids = grids,
                   .colliderGridCount = 2,
                   .mesh = shape == mvfx_rigidHull ? &s_hull : NULL},
            names[shape]);
    }
    // The instance's, swapped at half time.
    static const mvfxCollider swapped[2] = {own[1], own[0]};
    mvfxEmitterDef emitter = Thrown(mvfx_rigidBox, NULL, 0);
    emitter.collision.hostColliders = true;
    Met(gpu, kernels,
        &(Run){.mostApart = 29,
               .emitters = &emitter,
               .colliderGrids = grids,
               .colliderGridCount = 2,
               .hostColliders = own,
               .hostColliderCount = 2,
               .hostGridBytes = 4 * 81 + 4 * 512,
               .laterHostColliders = swapped,
               .laterHostColliderCount = 2},
        "boxes on the instance's grids, swapped at half time");
}

static void TestDepth(Gpu* gpu, mvfxGpu* kernels)
{
    // Terraces of an orthographic depth image looking down, a raised
    // square on a floor, met by the same samples along their way over the
    // substep, within twice the shape's radius; then a wall of reversed
    // depth before a camera turned 10 degrees about Y, normals given.
    static const float orthographic[16] = {0.5f, 0,           0, 0, 0, 0.5f,         0, 0, 0,
                                           0,    -0.1010101f, 0, 0, 0, -0.01010101f, 1};
    mvfxDepthImage image = Terraces(orthographic, -3.6f, -3.3f, NULL, NULL);
    image.origin[1] = 3.0;
    image.rotation[0] = -0.70710678f;
    image.rotation[3] = 0.70710678f;
    static const char* const names[4] = {
        "spheres on terraces of a depth image", "capsules on terraces of a depth image",
        "boxes on terraces of a depth image", "hulls on terraces of a depth image"};
    static const int32_t bounds[4] = {4, 26, 62, 95};
    for (int32_t shape = 0; shape < 4; ++shape)
    {
        mvfxEmitterDef emitter = Thrown((mvfxRigidShape)shape, NULL, 0);
        emitter.collision.depthImage = true;
        emitter.collision.depthThickness = 0.3f;
        emitter.collision.depthSamples = 16;
        Met(gpu, kernels,
            &(Run){.mostApart = bounds[shape],
                   .emitters = &emitter,
                   .depth = &image,
                   .mesh = shape == mvfx_rigidHull ? &s_hull : NULL},
            names[shape]);
    }
    static const float reversed[16] = {3, 0, 0,         0,  0, 3, 0,          0,
                                       0, 0, 0.002004f, -1, 0, 0, 0.1002004f, 0};
    static const float tilted[3] = {0, 0.6f, 0.8f};
    static const float square[3] = {0, 0, 1};
    image = Terraces(reversed, -5.5f, -5.0f, tilted, square);
    image.reversed = true;
    image.origin[0] = 0.7;
    image.origin[1] = 0.5;
    image.origin[2] = 4.0;
    image.rotation[1] = 0.08715574f;
    image.rotation[3] = 0.99619470f;
    mvfxEmitterDef emitter = Thrown(mvfx_rigidBox, NULL, 0);
    emitter.velocity.direction[1] = 0;
    emitter.velocity.direction[2] = -1;
    emitter.velocity.speed = (mvfxRange){2, 3};
    emitter.collision.depthImage = true;
    emitter.collision.depthThickness = 0.3f;
    emitter.collision.depthSamples = 16;
    Met(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter, .depth = &image},
        "boxes at a wall of reversed depth, normals given");
}

static void TestLargestSizes(Gpu* gpu, mvfxGpu* kernels)
{
    // Capsules of the largest size an effect may give (a size of 1 over a
    // size over life of MVFX_MAX_LIFE_SCALE, MVFX_MAX_EXTENT) on a
    // heightfield and a distance grid, met by samples along the core:
    // every step runs on the device, and as many bodies live there as on
    // the CPU path, within the emitter's capacity.
    mvfxGridDef grids[2];
    MakeGrids(grids);
    static const mvfxCollider own[2] = {{.kind = mvfx_colliderHeightfield,
                                         .center = {0, -0.8f, 0},
                                         .halfExtents = {1, 0.1f, 1},
                                         .rotation = {0, 0, 0, 1},
                                         .grid = {.index1 = 1}},
                                        {.kind = mvfx_colliderDistance,
                                         .center = {-0.2f, -0.5f, 0.1f},
                                         .halfExtents = {0.4f, 0.4f, 0.4f},
                                         .rotation = {0, 0.19509032f, 0, 0.98078528f},
                                         .grid = {.index1 = 2}}};
    mvfxEmitterDef emitter = Thrown(mvfx_rigidCapsule, own, 2);
    emitter.overLife.size =
        (mvfxSizeOverLife){.enabled = true, .size = {{.value = MVFX_MAX_LIFE_SCALE}}};
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(kernels,
                              &(Run){.emitters = &emitter,
                                     .emitterCount = 1,
                                     .colliderGrids = grids,
                                     .colliderGridCount = 2},
                              &effect);
    bool bounded = sim != NULL;
    int32_t steps = 0;
    for (int32_t s = 0; bounded && s < 20; ++s)
    {
        mvfxEmitterStats stats;
        bounded =
            mvfxContext_StepTicks(RunContext(), StepTicks()) == mvfx_success &&
            Simulate(gpu, sim) && ReadBack(gpu, sim, 0) &&
            mvfxInstance_GetEmitterStats(RunContext(), CpuInstance(), 0, &stats) == mvfx_success &&
            StatisticsRecord()[0] == (uint32_t)stats.live &&
            StatisticsRecord()[0] <= (uint32_t)emitter.capacity;
        steps += bounded ? 1 : 0;
    }
    CHECK(bounded && steps == 20, "capsules of the largest size, as many as on the CPU path");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
}

// An emitter's sizes times 2^exponent, its density another.
static void ScaleBodies(mvfxEmitterDef* emitter, int32_t exponent, float density)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        emitter->size.min[k] = (float)ldexp((double)emitter->size.min[k], exponent);
        emitter->size.max[k] = (float)ldexp((double)emitter->size.max[k], exponent);
    }
    emitter->rigid.density = density;
}

static void TestUnits(Gpu* gpu, mvfxGpu* kernels)
{
    // Bodies near the ends of sizes and densities, their mass and inertia
    // in their own units on both paths: boxes and hulls tumbling, up to
    // half MVFX_MAX_EXTENT across at its density, where the gyroscopic
    // term's momentum passed binary32's range, and 2^-100 of the sizes
    // above at density 1e-30, where their mass and inertia fell below it.
    static const struct
    {
        int32_t exponent;
        float density;
        const char* what;
    } ends[2] = {{20, MVFX_MAX_EXTENT, "the largest"}, {-100, 1e-30f, "the smallest"}};
    for (int32_t end = 0; end < 2; ++end)
    {
        for (int32_t hull = 0; hull < 2; ++hull)
        {
            mvfxEmitterDef emitter = Bodies(hull == 1 ? mvfx_rigidHull : mvfx_rigidBox);
            ScaleBodies(&emitter, ends[end].exponent, ends[end].density);
            char what[64];
            (void)snprintf(what, sizeof what, "%s tumbling at %s sizes", hull ? "hulls" : "boxes",
                           ends[end].what);
            Tumbled(gpu, kernels, &emitter, false, what);
        }
    }
    // Bodies thrown on a sloped floor, the whole scene's lengths (sizes,
    // places, speeds, gravity) times 2^-40 at density 1e-30, where their
    // mass and inertia fell below binary32's range: the same scene as at 1
    // in their units, as near on both paths and as few apart (their
    // orientations and angular velocities, which do not scale, compared
    // as at 1). Scaled up instead, it parts no more, but the harness's
    // tolerance (relative past 1, which the contact scenes at 1 are
    // within) no longer covers its slow bodies' velocities.
    static const mvfxCollider floor = {
        .kind = mvfx_colliderPlane, .center = {0, -0.7f * 0x1p-40f, 0}, .normal = {0, 1, 0.2f}};
    static const int32_t mostApart[3] = {4, 8, 11};
    for (int32_t shape = 0; shape < 3; ++shape)
    {
        mvfxEmitterDef emitter = Thrown((mvfxRigidShape)shape, &floor, 1);
        ScaleBodies(&emitter, -40, 1e-30f);
        emitter.shape.radius *= 0x1p-40f;
        emitter.velocity.speed.min *= 0x1p-40f;
        emitter.velocity.speed.max *= 0x1p-40f;
        emitter.motion.gravity[1] *= 0x1p-40f;
        char what[64];
        (void)snprintf(what, sizeof what, "%s on a sloped floor at 2^-40",
                       shape == 0   ? "spheres"
                       : shape == 1 ? "capsules"
                                    : "boxes");
        Met(gpu, kernels, &(Run){.mostApart = mostApart[shape], .emitters = &emitter}, what);
    }
}

static void TestFreeBoxes(Gpu* gpu, mvfxGpu* kernels)
{
    // Boxes 0.5 by 1 by 1.5 spun at 20 rad/s near their largest moment's
    // axis for 10 s at 8 substeps, each path on its own, within 1e-4: the
    // residual turns keep the momentum's length, which a turn's binary32
    // sine and cosine, the same each substep at one angle, would otherwise
    // grow or shrink. Then boxes spun at 500 to 800 rad/s, up to 1.7
    // radians a substep, a step at a time within 1e-3: their residual
    // turns taken in pieces.
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox);
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.5f, 1.0f, 1.5f}, .max = {0.5f, 1.0f, 1.5f}};
    emitter.lifetime = mvfxRangeSource(20.0f, 20.0f);
    emitter.motion.angularDrag = 0.0f;
    emitter.rigid.substeps = 8;
    emitter.orientationMode = mvfx_orientationNone;
    emitter.angularVelocity = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {19.0f, 1.0f, 1.0f}, .max = {20.0f, 2.0f, 2.0f}};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 600, .tolerance = 1e-4f},
            "boxes spun near their largest moment's axis for 10 s");
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.3f, 0.6f, 1.2f}, .max = {0.6f, 1.6f, 1.9f}};
    emitter.lifetime = mvfxRangeSource(1.0f, 1.0f);
    emitter.orientationMode = mvfx_orientationRandom;
    emitter.angularVelocity = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {-500, -500, -500}, .max = {500, 500, 500}};
    Tumbled(gpu, kernels, &emitter, false, "boxes spun at up to 800 rad/s");
}

static void TestRods(Gpu* gpu, mvfxGpu* kernels)
{
    // Rods 0.4 long and 1e-2, 1e-4 and 1e-6 of that across, capsules and
    // boxes, their moments about their length 1e-4 to 1e-12 of the
    // others' (the last held at 2^-40 in their units), free: spun at up to
    // 3 rad/s about their length, then tumbling about every axis, a step
    // at a time within 1e-3 of the CPU path's binary64 flow. Read back by
    // its momentum's size, a rod's spin about its length drained in
    // binary32: the rounding across it, at its large moments, outweighed
    // its momentum about its length.
    static const float across[3] = {4e-3f, 4e-5f, 4e-7f};
    for (int32_t shape = 1; shape < 3; ++shape)
    {
        for (int32_t r = 0; r < 3; ++r)
        {
            mvfxEmitterDef emitter = Bodies((mvfxRigidShape)shape);
            emitter.size = (mvfxVectorSource){.kind = mvfx_sourceRange,
                                              .min = {across[r], 0.4f, across[r]},
                                              .max = {across[r], 0.4f, across[r]}};
            emitter.orientationMode = mvfx_orientationEuler;
            emitter.orientation = (mvfxVectorSource){
                .kind = mvfx_sourceRange, .min = {0, 0, 1.5707964f}, .max = {0, 0, 1.5707964f}};
            emitter.angularVelocity =
                (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {-3, 0, 0}, .max = {3, 0, 0}};
            char what[96];
            const char* name = shape == 1 ? "capsules" : "boxes";
            (void)snprintf(what, sizeof what, "%s %g across spun about their length", name,
                           (double)across[r]);
            Tumbled(gpu, kernels, &emitter, false, what);
            emitter.orientationMode = mvfx_orientationRandom;
            emitter.angularVelocity =
                (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {-3, -3, -3}, .max = {3, 3, 3}};
            (void)snprintf(what, sizeof what, "%s %g across tumbling", name, (double)across[r]);
            Tumbled(gpu, kernels, &emitter, false, what);
        }
        // The thinnest spun about their length for 10 s, each path on its
        // own, within 1e-3: their spin stays along their length, which the
        // spin's read back keeps. Without it their spin leaned off their
        // length by 2.6e-3 of itself in that time.
        mvfxEmitterDef emitter = Bodies((mvfxRigidShape)shape);
        emitter.size = (mvfxVectorSource){
            .kind = mvfx_sourceRange, .min = {4e-7f, 0.4f, 4e-7f}, .max = {4e-7f, 0.4f, 4e-7f}};
        emitter.lifetime = mvfxRangeSource(20.0f, 20.0f);
        emitter.motion.angularDrag = 0.0f;
        emitter.orientationMode = mvfx_orientationEuler;
        emitter.orientation = (mvfxVectorSource){
            .kind = mvfx_sourceRange, .min = {0, 0, 1.5707964f}, .max = {0, 0, 1.5707964f}};
        emitter.angularVelocity =
            (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {-3, 0, 0}, .max = {3, 0, 0}};
        Compare(gpu, kernels,
                &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 600, .tolerance = 1e-3f},
                shape == 1 ? "capsules 4e-07 across spun about their length for 10 s"
                           : "boxes 4e-07 across spun about their length for 10 s");
    }
}

static void TestTiedSizes(Gpu* gpu, mvfxGpu* kernels)
{
    // A size the effect does not fix takes X for the axes a sphere or a
    // capsule does not set, as on the CPU path: a parameter's of unequal
    // axes, and one inherited from sprites of unequal sizes ending.
    static const float sized[4] = {0.15f, 0.4f, 0.25f, 0.0f};
    for (int32_t shape = 0; shape < 2; ++shape)
    {
        mvfxEmitterDef emitter = Bodies((mvfxRigidShape)shape);
        emitter.size = (mvfxVectorSource){
            .kind = mvfx_sourceParameter, .min = {1, 1, 1}, .max = {1, 1, 1}, .parameter = 0};
        char what[64];
        const char* name = shape == 0 ? "spheres" : "capsules";
        (void)snprintf(what, sizeof what, "%s sized by a parameter of unequal axes", name);
        Compare(gpu, kernels,
                &(Run){.emitters = &emitter,
                       .emitterCount = 1,
                       .parameters = sized,
                       .parameterCount = 1,
                       .pace = 0.001,
                       .adopting = true,
                       .tolerance = 1e-3f},
                what);
        mvfxEmitterDef emitters[2] = {Sprite(), Bodies((mvfxRigidShape)shape)};
        emitters[0].events = mvfx_eventMaskDeath;
        emitters[0].eventCapacity = 4096;
        emitters[0].size = (mvfxVectorSource){
            .kind = mvfx_sourceRange, .min = {0.1f, 0.3f, 0.2f}, .max = {0.2f, 0.5f, 0.3f}};
        emitters[1].rate = 0.0f;
        emitters[1].burstCount = 0;
        emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                                  .source = 0,
                                                  .kind = mvfx_eventDeath,
                                                  .minCount = 1,
                                                  .maxCount = 1,
                                                  .inheritSize = true,
                                                  .capacity = 512};
        (void)snprintf(what, sizeof what, "%s inheriting unequal sizes", name);
        Compare(gpu, kernels,
                &(Run){.emitters = emitters,
                       .emitterCount = 2,
                       .pace = 0.001,
                       .adopting = true,
                       .tolerance = 1e-3f},
                what);
    }
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestRefused(kernels);
    TestShapes(gpu, kernels);
    TestSizes(gpu, kernels);
    TestSleep(gpu, kernels);
    TestFloor(gpu, kernels);
    TestColliders(gpu, kernels);
    TestStrikes(gpu, kernels);
    TestGrids(gpu, kernels);
    TestDepth(gpu, kernels);
    TestLargestSizes(gpu, kernels);
    TestUnits(gpu, kernels);
    TestFreeBoxes(gpu, kernels);
    TestRods(gpu, kernels);
    TestTiedSizes(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
