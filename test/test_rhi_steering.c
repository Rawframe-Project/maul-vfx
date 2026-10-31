// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Steering and the pair force on a real device (lavapipe on CI) beside the
// CPU path, compared after every step (rhi_harness.h): places and
// velocities within the tolerance (WGSL's sqrt and division round
// differently), the neighbor attributes and the caps. Covered: flocking
// within a view cone and a full view, the force held, seeking with
// arrival and fleeing within a radius toward targets in the world and in
// the emitter's space under a moving instance, open and closed paths (one
// shorter than the lookahead's distance),
// steering with no neighbors, and the pair force pulling and pushing by a
// curve. Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// A sprite emitter with its own neighbors.
static mvfxEmitterDef WithNeighbors(float radius, int32_t most)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.neighbors =
        (mvfxNeighborDef){.enable = true, .source = -1, .radius = radius, .maxNeighbors = most};
    return emitter;
}

static void Steered(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, const char* what)
{
    Compare(gpu, kernels, &(Run){.emitters = emitter, .emitterCount = 1, .moving = true}, what);
}

// Flocking with separation: its push from a close neighbor turns by the
// places' difference over their distance, as the pair force's pull does,
// so velocities part faster than the others': within 1e-3.
static void Flocked(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, const char* what)
{
    Compare(gpu, kernels,
            &(Run){.emitters = emitter, .emitterCount = 1, .moving = true, .tolerance = 1e-3f},
            what);
}

static void TestFlocking(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = WithNeighbors(0.3f, 32);
    emitter.steering = (mvfxSteeringDef){.enable = true,
                                         .separation = 0.5f,
                                         .separationRadius = 0.15f,
                                         .alignment = 0.4f,
                                         .alignmentRadius = 0.3f,
                                         .cohesion = 0.3f,
                                         .cohesionRadius = 0.25f,
                                         .viewAngle = 2.0f};
    Flocked(gpu, kernels, &emitter, "flocking within a view cone");
    emitter.steering.viewAngle = 3.14159274f;
    emitter.steering.maxForce = 2.0f;
    Flocked(gpu, kernels, &emitter, "flocking within a full view, the force held");
}

static void TestTargets(Gpu* gpu, mvfxGpu* kernels)
{
    // Seeking a world point with arrival, fleeing one of the emitter's
    // space within a radius.
    mvfxEmitterDef emitter = WithNeighbors(0.2f, 16);
    emitter.steering = (mvfxSteeringDef){
        .enable = true,
        .cruiseSpeed = 3.0f,
        .seek = {.weight = 1.0f, .space = 0, .point = {1.0f, 2.0f, 0.5f}, .radius = 0.8f},
        .flee = {.weight = 0.5f, .space = 1, .point = {0.0f, 0.2f, 0.0f}, .radius = 1.0f}};
    Steered(gpu, kernels, &emitter, "seeking with arrival and fleeing within a radius");
    // With no neighbors.
    emitter.neighbors = (mvfxNeighborDef){0};
    emitter.space = mvfx_spaceLocal;
    Steered(gpu, kernels, &emitter,
            "seeking and fleeing with no neighbors, in the instance's space");
    // Seeking a point the particles pass, within its arrival radius.
    emitter.steering.seek =
        (mvfxSteerPoint){.weight = 2.0f, .space = 1, .point = {0.0f, 0.8f, 0.0f}, .radius = 1.0f};
    emitter.steering.flee = (mvfxSteerPoint){0};
    Steered(gpu, kernels, &emitter, "seeking a point within its arrival radius");
}

static void TestPaths(Gpu* gpu, mvfxGpu* kernels)
{
    static const float points[15] = {0, 0, 0, 1, 1, 0, 0, 2, 1, -1, 1, 1, -0.5f, 0.5f, -1};
    mvfxEmitterDef emitter = Sprite();
    emitter.steering = (mvfxSteeringDef){.enable = true,
                                         .cruiseSpeed = 2.5f,
                                         .maxForce = 6.0f,
                                         .path = {.weight = 1.5f,
                                                  .space = 1,
                                                  .points = points,
                                                  .pointCount = 5,
                                                  .radius = 0.1f,
                                                  .lookahead = 0.3f}};
    Steered(gpu, kernels, &emitter, "following an open path");
    emitter.steering.path.closed = true;
    emitter.steering.path.space = 0;
    Steered(gpu, kernels, &emitter, "following a closed path in the world");
    // A closed path shorter than the lookahead's distance: around it more
    // than once.
    static const float small[9] = {0, 0.5f, 0, 0.1f, 0.6f, 0, -0.1f, 0.6f, 0.05f};
    emitter.steering.path = (mvfxSteerPath){.weight = 1.5f,
                                            .space = 1,
                                            .points = small,
                                            .pointCount = 3,
                                            .closed = true,
                                            .radius = 0.05f,
                                            .lookahead = 2.0f};
    Steered(gpu, kernels, &emitter, "following a closed path shorter than the lookahead");
}

static void TestPairForce(Gpu* gpu, mvfxGpu* kernels)
{
    // Pulling near, pushing far: a curve from -1 to 2 over the radius.
    static const mvfxCurveKey keys[2] = {
        {.time = 0, .value = -1, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 2, .interpolation = mvfx_interpolationLinear}};
    mvfxEmitterDef emitter = WithNeighbors(0.25f, 32);
    emitter.pairForce =
        (mvfxPairForceDef){.enable = true,
                           .radius = 0.2f,
                           .strength = {.value = 3.0f, .curve = {.keys = keys, .keyCount = 2}}};
    // A pull's direction from a close neighbor turns by the places'
    // difference over their distance, so velocities part faster than the
    // others': within 1e-3.
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .tolerance = 1e-3f},
            "a pair force by a curve");
    emitter.pairForce.strength = (mvfxLifeCurve){.value = -2.0f};
    emitter.steering = (mvfxSteeringDef){
        .enable = true, .cohesion = 0.6f, .cohesionRadius = 0.2f, .viewAngle = 3.14159274f};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .tolerance = 1e-3f},
            "a constant pair force with cohesion");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestFlocking(gpu, kernels);
    TestTargets(gpu, kernels);
    TestPaths(gpu, kernels);
    TestPairForce(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
