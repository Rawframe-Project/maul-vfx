// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles meeting each other on a real device (lavapipe on CI)
// beside the CPU path, compared after every step (rhi_harness.h), the
// emitter's substeps in one workgroup: spheres and boxes thrown down on a
// floor, meeting it and each other, their pairs' contacts coloured as the
// core's and solved colour by colour, their pair strikes' events; bodies
// ending within the run (pairs' priorities of the bodies' ranks); boxes of
// sizes apart (the pair grid's levels); a pool too small for them, its
// contacts past the room dropped and counted; capsules by their cores and
// hulls as polytopes; sleeping by islands and waking by touch; spheres,
// capsules and boxes meeting each other with the scene at 2^-40 and
// density 1e-30, in the lighter body's units.
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "rhi_pair_budgets.h"
#include "test_harness.h"

#include <string.h>

// Sprites as rigid bodies of a shape thrown down on a floor, sizes
// unequal along the axes it sets, random orientations and angular velocities,
// four substeps, meeting each other with body contacts a body each. A
// pile's friction holds or lets go by its loads, so a body's difference
// within the tolerance parts the bodies about it: the CPU path against
// itself, places moved by 2e-6 of themselves each step, parts 183 of 420
// boxes in 40 steps where the GPU parts 204. Fewer bodies (150 a second)
// keep the piles small and the bounds tight.
static mvfxEmitterDef Thrown(mvfxRigidShape shape, const mvfxCollider* floor, int32_t bodyContacts)
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
    emitter.rate = 150.0f;
    emitter.lifetime = mvfxRangeSource(0.8f, 1.2f);
    emitter.velocity.direction[1] = -1.0f;
    emitter.colliders = floor;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enable = true};
    emitter.rigid = (mvfxRigidDef){.enable = true,
                                   .shape = shape,
                                   .density = 500.0f,
                                   .substeps = 4,
                                   .restitution = 0.4f,
                                   .staticFriction = 0.5f,
                                   .dynamicFriction = 0.4f,
                                   .bodyContacts = bodyContacts};
    emitter.events = mvfx_eventMaskBirth | mvfx_eventMaskDeath | mvfx_eventMaskCollision;
    emitter.eventCapacity = 4096;
    return emitter;
}

// A lopsided hull: a wedge with a spike, and its mesh.
static const float s_lopsided[21] = {0,    0, 0,    1, 0, 0,    0, 0.6f, 0,    0,   0,
                                     0.3f, 1, 0.1f, 0, 0, 0.4f, 0, 0.7f, 0.8f, 1.1f};
static const mvfxMeshDef s_hull = {
    .cookie = MVFX_MESH_DEF_COOKIE, .positions = s_lopsided, .vertexCount = 7};

static const mvfxCollider s_floor = {
    .kind = mvfx_colliderPlane, .center = {0, -0.7f, 0}, .normal = {0, 1, 0.2f}};
static const mvfxCollider s_level = {
    .kind = mvfx_colliderPlane, .center = {0, -0.7f, 0}, .normal = {0, 1, 0}};

// A scene's bound on the bodies apart (rhi_pair_budgets.h), 0 for one
// it does not list.
static int32_t BoundOf(const char* what)
{
    for (size_t i = 0; i < sizeof s_pairBudgets / sizeof s_pairBudgets[0]; ++i)
    {
        if (strcmp(s_pairBudgets[i].scene, what) == 0)
        {
            return s_pairBudgets[i].mostApart;
        }
    }
    return 0;
}

// Compares bodies a step at a time from the same state (rhi_harness.h's
// adopting): a body near the floor or another body may go apart, at most
// the scene's bound. The bounds bound chaotic divergence under
// perturbation, not a correctness count: tools/pair_budgets.py derives
// them, and is run again when the arithmetic changes. Its margin, times 1.5
// plus 4, lets a new run step past the ensemble's largest count as far
// again as the widest step between its two largest (1.57 times, the pool's
// 58 against 37) or by a few bodies for a small count.
static void Met(Gpu* gpu, mvfxGpu* kernels, const Run* scene, const char* what)
{
    Run run = *scene;
    run.mostApart = BoundOf(what);
    CHECK(run.mostApart > 0, "a scene's bound on the bodies apart");
    run.emitterCount = 1;
    run.colliding = true;
    run.adopting = true;
    run.tolerance = 1e-3f;
    Compare(gpu, kernels, &run, what);
}

static void TestMeeting(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Thrown(mvfx_rigidSphere, &s_floor, 8);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "spheres meeting the floor and each other");
    emitter = Thrown(mvfx_rigidBox, &s_floor, 8);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "boxes meeting the floor and each other");
    // Living a third to two thirds of a second, bodies end within the run:
    // a pair's priority is of the bodies' ranks among those the step moves.
    emitter = Thrown(mvfx_rigidSphere, &s_floor, 8);
    emitter.lifetime = mvfxRangeSource(0.3f, 0.6f);
    Met(gpu, kernels, &(Run){.least = 50, .emitters = &emitter},
        "spheres meeting each other, some ending within the run");
    // Sizes from 0.1 to 1: the pair grid has levels. Masses up to 500
    // times one another's part more bodies (the CPU path against itself,
    // each body's size moved by a binary32 unit each step, parts 12).
    emitter = Thrown(mvfx_rigidBox, &s_floor, 8);
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.1f, 0.1f}, .max = {1.0f, 0.6f, 0.8f}};
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "boxes of sizes apart meeting each other");
    // Capsules by their cores (the CPU path against itself, sizes moved
    // by a binary32 unit each step, parts 17).
    emitter = Thrown(mvfx_rigidCapsule, &s_floor, 8);
    Met(gpu, kernels, &(Run){.emitters = &emitter}, "capsules meeting the floor and each other");
    // Hulls as polytopes, over the first 15 steps: a hull's face against
    // another's turns on rounding (a cube's hull parts 99 of 119 over 40
    // steps on the CPU path against itself, places moved by 2e-6, where
    // boxes part 10; 7 over 15).
    emitter = Thrown(mvfx_rigidHull, &s_floor, 8);
    Met(gpu, kernels, &(Run){.steps = 15, .least = 30, .emitters = &emitter, .mesh = &s_hull},
        "hulls meeting the floor and each other");
    // Sleeping, over two seconds on a level floor: 15 boxes a second,
    // spread over 3 and living 3 seconds, sleep by islands once still a
    // tenth of a second, and those landing on them wake them by touch (on
    // lavapipe 31 asleep at most, 176 woken; the CPU path against itself
    // parts none, 2 without sleeping).
    emitter = Thrown(mvfx_rigidBox, &s_level, 8);
    emitter.shape.radius = 3.0f;
    emitter.rate = 15.0f;
    emitter.lifetime = mvfxRangeSource(3.0f, 3.5f);
    emitter.rigid.sleepSpeed = 2.0f;
    emitter.rigid.sleepTime = 0.1f;
    Met(gpu, kernels,
        &(Run){
            .steps = 120, .least = 45, .emitters = &emitter, .leastAsleep = 10, .leastWoken = 20},
        "boxes meeting each other, sleeping by islands and woken by touch");
    // Three contacts a body: those past the pool's room are dropped and
    // counted.
    emitter = Thrown(mvfx_rigidBox, &s_floor, 3);
    emitter.events = 0;
    Met(gpu, kernels, &(Run){.emitters = &emitter, .leastRigidCapped = 1},
        "boxes meeting each other, the pool's contacts dropped");
}

static void TestUnits(Gpu* gpu, mvfxGpu* kernels)
{
    // Bodies meeting each other with the whole scene's lengths (sizes,
    // places, speeds, gravity) times 2^-40 at density 1e-30: a pair's
    // masses, in the particles' units, passed binary32's range; in the
    // lighter body's units they are the scene at 1's, on both paths.
    static const mvfxCollider floor = {
        .kind = mvfx_colliderPlane, .center = {0, -0.7f * 0x1p-40f, 0}, .normal = {0, 1, 0.2f}};
    static const char* const names[3] = {"spheres meeting each other at 2^-40",
                                         "capsules meeting each other at 2^-40",
                                         "boxes meeting each other at 2^-40"};
    for (int32_t shape = 0; shape < 3; ++shape)
    {
        mvfxEmitterDef emitter = Thrown((mvfxRigidShape)shape, &floor, 8);
        for (int32_t k = 0; k < 3; ++k)
        {
            emitter.size.min[k] *= 0x1p-40f;
            emitter.size.max[k] *= 0x1p-40f;
        }
        emitter.rigid.density = 1e-30f;
        emitter.shape.radius *= 0x1p-40f;
        emitter.velocity.speed.min *= 0x1p-40f;
        emitter.velocity.speed.max *= 0x1p-40f;
        emitter.motion.gravity[1] *= 0x1p-40f;
        Met(gpu, kernels, &(Run){.emitters = &emitter}, names[shape]);
    }
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestMeeting(gpu, kernels);
    TestUnits(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
