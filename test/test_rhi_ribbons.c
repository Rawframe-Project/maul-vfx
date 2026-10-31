// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Ribbons' births on a real device (lavapipe on CI) beside the CPU path's,
// compared after every step (rhi_harness.h): each particle's ribbon and
// its distance along it, and the emitter's ribbon counter, by the counter
// with the GPU's breaks and the host's, by batch and by source, with the
// killed and the ended about. Skips (77) without an adapter, unless
// MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// A sprite whose births join ribbons by ids, tiled by distance (so each
// point stores its distance), breaking past breakDistance.
static mvfxEmitterDef Ribboned(mvfxRibbonIds ids, float breakDistance)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.ribbon.ids = ids;
    emitter.ribbon.texture = mvfx_ribbonTileByDistance;
    emitter.ribbon.tileLength = 0.25f;
    emitter.ribbon.breakDistance = breakDistance;
    return emitter;
}

static void TestCounter(Gpu* gpu, mvfxGpu* kernels)
{
    // One ribbon at a time from a sphere of radius 0.5: a birth farther
    // than 0.6 from the ribbon's youngest point (a birth before it moved,
    // or a particle of an earlier step) starts the next, under a moving
    // instance; the host breaks it too, every seventh step.
    mvfxEmitterDef emitter = Ribboned(mvfx_ribbonOne, 0.6f);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .breaking = true,
                   .leastBreaks = 20},
            "ribbons by the counter, broken by distance and by the host");
    // Then in the world's space, cleared at half time: the counter starts
    // again from 0 on both paths.
    emitter.space = mvfx_spaceWorld;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .clearing = true,
                   .leastBreaks = 20},
            "ribbons by the counter in the world's space, cleared");
    // No break distance: one ribbon, its distance growing along it.
    emitter = Ribboned(mvfx_ribbonOne, 0.0f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true},
            "one ribbon, never broken by distance");
}

static void TestBatches(Gpu* gpu, mvfxGpu* kernels)
{
    // A ribbon a tick's births (the rate's, and the sprite's burst), and
    // one for the host's particles queued each step.
    static const double positions[6] = {0.1, 0.2, 0.0, -0.3, 0.0, 0.1};
    const mvfxSpawnRequest spawn = {.count = 2, .positions = positions};
    mvfxEmitterDef emitter = Ribboned(mvfx_ribbonPerBatch, 0.0f);
    emitter.hostSpawnCapacity = 8;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .spawn = &spawn},
            "ribbons by batch, the host's particles one");
}

static void TestSources(Gpu* gpu, mvfxGpu* kernels)
{
    // Births from another emitter's deaths in their event's particle's
    // ribbon, beside births of their own by the counter.
    mvfxEmitterDef emitters[2] = {Sprite(), Ribboned(mvfx_ribbonPerSource, 0.3f)};
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 4096;
    emitters[1].rate = 120.0f;
    emitters[1].burstCount = 0;
    emitters[1].lifetime = mvfxMakeRangeSource(0.1f, 0.3f);
    emitters[1].velocity.speed = (mvfxRange){0.5f, 1.0f};
    emitters[1].eventSpawn = (mvfxEventSpawn){.enable = true,
                                              .source = 0,
                                              .kind = mvfx_eventDeath,
                                              .minCount = 1,
                                              .maxCount = 3,
                                              .inheritVelocity = 0.5f,
                                              .capacity = 512};
    Compare(gpu, kernels,
            &(Run){.emitters = emitters, .emitterCount = 2, .least = 20, .moving = true},
            "ribbons by source, from another's deaths");
}

static void TestSourcesFarApart(Gpu* gpu, mvfxGpu* kernels)
{
    // By source, some 700 births from another emitter's deaths a step
    // between births of the emitter's own, 100 a second: an own birth's
    // ribbon's point before (a survivor, or the step's other own birth)
    // lies past the look-back's 256, so it starts the ribbon afresh, as
    // the CPU path finds none.
    mvfxEmitterDef emitters[2] = {Sprite(), Ribboned(mvfx_ribbonPerSource, 0.3f)};
    emitters[0].capacity = 16384;
    emitters[0].rate = 60000.0f;
    emitters[0].burstCount = 0;
    emitters[0].lifetime = mvfxMakeRangeSource(0.1f, 0.2f);
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 4096;
    emitters[1].capacity = 16384;
    emitters[1].rate = 100.0f;
    emitters[1].burstCount = 0;
    emitters[1].lifetime = mvfxMakeRangeSource(0.1f, 0.3f);
    emitters[1].eventSpawn = (mvfxEventSpawn){.enable = true,
                                              .source = 0,
                                              .kind = mvfx_eventDeath,
                                              .minCount = 1,
                                              .maxCount = 1,
                                              .capacity = 2048};
    Compare(gpu, kernels,
            &(Run){.emitters = emitters,
                   .emitterCount = 2,
                   .steps = 30,
                   .least = 1000,
                   .secondLeast = 1000,
                   .moving = true},
            "by source, the own births past the look-back");
    // Never broken by distance: an own birth whose point before lies past
    // the look-back starts at a distance of 0, where one found would
    // carry the distance on.
    emitters[1].ribbon.breakDistance = 0.0f;
    Compare(gpu, kernels,
            &(Run){.emitters = emitters,
                   .emitterCount = 2,
                   .steps = 30,
                   .least = 1000,
                   .secondLeast = 1000,
                   .moving = true},
            "by source, never broken, the own births past the look-back");
}

static void TestKilled(Gpu* gpu, mvfxGpu* kernels)
{
    // Births killed on their way and particles killed in their move stay
    // points a birth looks back at until the step's end, at their moved
    // places; those whose lives ended before the moves do not.
    const mvfxKillRule rule = {
        .kind = mvfx_killInsideSphere, .center = {0.1f, 0.4f, 0}, .radius = 0.3f};
    mvfxEmitterDef emitter = Ribboned(mvfx_ribbonOne, 0.6f);
    emitter.killRules = &rule;
    emitter.killRuleCount = 1;
    emitter.lifetime = mvfxMakeRangeSource(0.03f, 0.3f);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .least = 20,
                   .moving = true,
                   .pace = 0.001,
                   .killing = true,
                   .leastBreaks = 20},
            "ribbons with the killed and the ended about");
}

static void TestEnded(Gpu* gpu, mvfxGpu* kernels)
{
    // Three births in four steps living one to five steps: the ribbon's
    // youngest particle often ends before the step's moves, and the
    // step's first birth looks back past it to the one before, as the CPU
    // path takes the ended out first.
    mvfxEmitterDef emitter = Ribboned(mvfx_ribbonOne, 0.0f);
    emitter.rate = 45.0f;
    emitter.burstCount = 0;
    emitter.lifetime = mvfxMakeRangeSource(0.02f, 0.08f);
    Compare(
        gpu, kernels,
        &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 120, .least = 2, .moving = true},
        "a sparse ribbon whose youngest particles end");
}

static void TestMany(Gpu* gpu, mvfxGpu* kernels)
{
    // Hundreds of births a step, past a workgroup's chunk of the scans and
    // the look-back's 256: one ribbon broken often, its distances summed
    // along chains of hundreds; then bursts of 768 a firing by batch, a
    // ribbon each past the chunk and the look-back, the scans' last run
    // full (768 is 256 runs of 3).
    mvfxEmitterDef emitter = Ribboned(mvfx_ribbonOne, 0.05f);
    emitter.capacity = 16384;
    emitter.rate = 36000.0f;
    emitter.lifetime = mvfxMakeRangeSource(0.1f, 0.3f);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .steps = 20,
                   .least = 4000,
                   .moving = true,
                   .leastBreaks = 1000},
            "many births a step by the counter");
    static const mvfxBurst bursts[1] = {{.time = 0.0f,
                                         .minCount = 768,
                                         .maxCount = 768,
                                         .cycles = 0,
                                         .interval = 0.05f,
                                         .probability = 1.0f}};
    emitter = Ribboned(mvfx_ribbonPerBatch, 0.0f);
    emitter.capacity = 16384;
    emitter.rate = 0.0f;
    emitter.bursts = bursts;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxMakeRangeSource(0.1f, 0.3f);
    Compare(
        gpu, kernels,
        &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 20, .least = 2000, .moving = true},
        "bursts of 768 by batch");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestCounter(gpu, kernels);
    TestBatches(gpu, kernels);
    TestSources(gpu, kernels);
    TestSourcesFarApart(gpu, kernels);
    TestKilled(gpu, kernels);
    TestEnded(gpu, kernels);
    TestMany(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
