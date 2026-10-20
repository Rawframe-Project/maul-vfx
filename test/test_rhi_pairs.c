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
// hulls as polytopes; sleeping by islands and waking by touch.
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// Sprites as rigid bodies of a shape thrown down on a floor, sizes
// unequal along their axes, random orientations and angular velocities,
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
    emitter.orientationMode = mvfx_orientationRandom;
    emitter.angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {-20, -20, -20}, .max = {20, 20, 20}};
    emitter.motion.angularDrag = 0.3f;
    emitter.rate = 150.0f;
    emitter.lifetime = mvfxRangeSource(0.8f, 1.2f);
    emitter.velocity.direction[1] = -1.0f;
    emitter.colliders = floor;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enabled = true};
    emitter.rigid = (mvfxRigidDef){.enabled = true,
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

// Compares bodies a step at a time from the same state (rhi_harness.h's
// adopting): a body near the floor or another body may go apart, at most
// the scene's bound.
static void Met(Gpu* gpu, mvfxGpu* kernels, const Run* scene, const char* what)
{
    CHECK(scene->mostApart > 0, "a scene's bound on the bodies apart");
    Run run = *scene;
    run.emitterCount = 1;
    run.colliding = true;
    run.adopting = true;
    run.tolerance = 1e-3f;
    Compare(gpu, kernels, &run, what);
}

static void TestMeeting(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Thrown(mvfx_rigidSphere, &s_floor, 8);
    Met(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter},
        "spheres meeting the floor and each other");
    emitter = Thrown(mvfx_rigidBox, &s_floor, 8);
    Met(gpu, kernels, &(Run){.mostApart = 6, .emitters = &emitter},
        "boxes meeting the floor and each other");
    // Living a third to two thirds of a second, bodies end within the run:
    // a pair's priority is of the bodies' ranks among those the step moves.
    emitter = Thrown(mvfx_rigidSphere, &s_floor, 8);
    emitter.lifetime = mvfxRangeSource(0.3f, 0.6f);
    Met(gpu, kernels, &(Run){.mostApart = 4, .least = 50, .emitters = &emitter},
        "spheres meeting each other, some ending within the run");
    // Sizes from 0.1 to 1: the pair grid has levels. Masses up to 500
    // times one another's part more bodies (the CPU path against itself,
    // each body's size moved by a binary32 unit each step, parts 12).
    emitter = Thrown(mvfx_rigidBox, &s_floor, 8);
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.1f, 0.1f}, .max = {1.0f, 0.6f, 0.8f}};
    Met(gpu, kernels, &(Run){.mostApart = 66, .emitters = &emitter},
        "boxes of sizes apart meeting each other");
    // Capsules by their cores (the CPU path against itself, sizes moved
    // by a binary32 unit each step, parts 17).
    emitter = Thrown(mvfx_rigidCapsule, &s_floor, 8);
    Met(gpu, kernels, &(Run){.mostApart = 38, .emitters = &emitter},
        "capsules meeting the floor and each other");
    // Hulls as polytopes, over the first 15 steps: a hull's face against
    // another's turns on rounding (a cube's hull parts 99 of 119 over 40
    // steps on the CPU path against itself, places moved by 2e-6, where
    // boxes part 10; 7 over 15).
    emitter = Thrown(mvfx_rigidHull, &s_floor, 8);
    Met(gpu, kernels,
        &(Run){.mostApart = 4, .steps = 15, .least = 30, .emitters = &emitter, .mesh = &s_hull},
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
        &(Run){.mostApart = 5,
               .steps = 120,
               .least = 45,
               .emitters = &emitter,
               .leastAsleep = 10,
               .leastWoken = 20},
        "boxes meeting each other, sleeping by islands and woken by touch");
    // Three contacts a body: those past the pool's room are dropped and
    // counted.
    emitter = Thrown(mvfx_rigidBox, &s_floor, 3);
    emitter.events = 0;
    Met(gpu, kernels, &(Run){.mostApart = 5, .emitters = &emitter, .leastRigidCapped = 1},
        "boxes meeting each other, the pool's contacts dropped");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestMeeting(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
