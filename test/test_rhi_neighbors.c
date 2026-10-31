// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Neighbor grids on a real device (lavapipe on CI) beside the CPU path,
// compared after every step (rhi_harness.h): each particle's neighbor
// count, and the offsets to its neighbors' mean place and velocity, and
// the queries their cap stopped. Covered: an emitter's own neighbors at
// rest (the words exact) and moving, another emitter's, a cap that binds
// (and a clear emptying its count), another emitter left out by a quality
// level, steps whose births do not all fit,
// places on cell borders and far from the origin on both sides, where a
// binary32 product rounds up to the next cell, and grids of several sort
// blocks. Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// An emitter of particles that stay where they are born: born in a box
// at rest, no gravity or drag, finding its own neighbors.
static mvfxEmitterDef AtRest(float radius, int32_t most)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = CAPACITY;
    emitter.rate = 1500.0f;
    emitter.lifetime = mvfxMakeRangeSource(0.3f, 0.6f);
    emitter.shape = (mvfxShape){
        .kind = mvfx_shapeBox, .rotation = {0, 0, 0, 1}, .halfExtents = {1.0f, 0.5f, 1.0f}};
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone, .direction = {0, 1, 0}};
    emitter.neighbors =
        (mvfxNeighborDef){.enable = true, .source = -1, .radius = radius, .maxNeighbors = most};
    return emitter;
}

// A sprite emitter (gravity, drag, a burst) finding its own neighbors.
static mvfxEmitterDef Moving(float radius, int32_t most)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.neighbors =
        (mvfxNeighborDef){.enable = true, .source = -1, .radius = radius, .maxNeighbors = most};
    return emitter;
}

static void TestOwn(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = AtRest(0.2f, MVFX_MAX_NEIGHBORS);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .exactNeighbors = true},
            "an emitter's own neighbors, at rest");
    emitter = Moving(0.15f, 64);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true},
            "an emitter's own neighbors, moving");
    // A cap of 3 among dense particles: most queries stop at it. Cleared
    // at half time, which empties the caps' count.
    emitter = AtRest(0.3f, 3);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .clearing = true,
                   .exactNeighbors = true,
                   .leastCapped = 100},
            "a cap that binds, cleared at half time");
}

static void TestAnother(Gpu* gpu, mvfxGpu* kernels)
{
    // The second finds the first's particles; the first finds none.
    static mvfxEmitterDef emitters[2];
    emitters[0] = Moving(0.0f, 1);
    emitters[0].neighbors = (mvfxNeighborDef){0};
    emitters[1] = Moving(0.25f, 32);
    emitters[1].neighbors.source = 0;
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2, .moving = true},
            "another emitter's neighbors");
    // A quality level leaving the first out: the second finds none.
    static const mvfxQualityLevel lower = {.capacityScale = 1.0f, .rateScale = 1.0f, .omitted = 1};
    Compare(gpu, kernels,
            &(Run){.emitters = emitters,
                   .emitterCount = 2,
                   .moving = true,
                   .least = -1,
                   .quality = &lower},
            "another emitter's neighbors, the other left out");
}

static void TestNotFitting(Gpu* gpu, mvfxGpu* kernels)
{
    // Room for about the particles a lifetime holds: from the eleventh
    // step on the births do not all fit at the step's start, and take
    // the room of those that end within it.
    mvfxEmitterDef emitter = AtRest(0.2f, 16);
    emitter.rate = 2400.0f;
    emitter.lifetime = mvfxMakeRangeSource(0.25f, 0.25f);
    emitter.capacity = 620;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .exactNeighbors = true},
            "births that do not all fit");
}

// Places on the cell borders of cells of 0.3 (the radius): k times the
// cell in binary32 for k from -32 to 31, on each axis in turn. For most,
// the binary32 product with the inverse cell rounds to k while the exact
// one lies below it, in cell k - 1.
static double s_borders[3 * 64];

static void TestBorders(Gpu* gpu, mvfxGpu* kernels)
{
    for (int32_t i = 0; i < 64; ++i)
    {
        float x = (float)(i - 32) * 0.3f;
        s_borders[3 * i] = (double)x;
        s_borders[3 * i + 1] = (double)(i % 3 == 0 ? x : 0.0f);
        s_borders[3 * i + 2] = (double)(i % 3 == 1 ? -x : 0.0f);
    }
    mvfxEmitterDef emitter = AtRest(0.3f, MVFX_MAX_NEIGHBORS);
    emitter.rate = 0.0f;
    emitter.lifetime = mvfxMakeRangeSource(10.0f, 10.0f);
    emitter.shape = (mvfxShape){.kind = mvfx_shapePoint, .rotation = {0, 0, 0, 1}};
    emitter.hostSpawnCapacity = 64;
    const mvfxSpawnRequest spawn = {.count = 64, .positions = s_borders};
    Compare(
        gpu, kernels,
        &(Run){.emitters = &emitter, .emitterCount = 1, .spawn = &spawn, .exactNeighbors = true},
        "places on cell borders");
    // Far from the origin, on both sides: thousands of cells out.
    for (int32_t i = 0; i < 64; ++i)
    {
        float side = i % 2 == 0 ? 1.0f : -1.0f;
        s_borders[3 * i] = (double)(side * (3000.0f + 0.1f * (float)i));
        s_borders[3 * i + 1] = (double)(side * -1700.0f + 0.05f * (float)(i % 5));
        s_borders[3 * i + 2] = (double)(4500.0f - 0.07f * (float)(i % 7));
    }
    Compare(
        gpu, kernels,
        &(Run){.emitters = &emitter, .emitterCount = 1, .spawn = &spawn, .exactNeighbors = true},
        "places far from the origin");
}

static void TestBlocks(Gpu* gpu, mvfxGpu* kernels)
{
    // About 3000 particles: three sort blocks and keys of every digit.
    mvfxEmitterDef emitter = AtRest(0.1f, 64);
    emitter.capacity = 4 * CAPACITY;
    emitter.rate = 9000.0f;
    emitter.shape.halfExtents[0] = 3.0f;
    emitter.shape.halfExtents[2] = 3.0f;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .exactNeighbors = true, .least = 2500},
            "grids of several sort blocks");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestOwn(gpu, kernels);
    TestAnother(gpu, kernels);
    TestNotFitting(gpu, kernels);
    TestBorders(gpu, kernels);
    TestBlocks(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
