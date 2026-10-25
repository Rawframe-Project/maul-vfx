// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Emitters spawning from another's events, planned instances on a real
// device (lavapipe on CI) beside the CPU path's, compared after every step
// (rhi_harness.h): births from deaths, births and collisions, in both
// spaces, numbered with the emitter's own and the host's births, bounded
// and refused, replayed in a full emitter. Skips (77) without an adapter,
// unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

#include "maul-vfx/modules.h"

// A sub-emitter of emitter source's events of a kind: no births of its own,
// one to three from each event, with half the velocity and the colour.
static mvfxEmitterDef Child(int32_t source, mvfxEventKind kind)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.rate = 0.0f;
    emitter.burstCount = 0;
    emitter.lifetime = mvfxRangeSource(0.1f, 0.3f);
    emitter.velocity.speed = (mvfxRange){0.5f, 1.0f};
    emitter.eventSpawn = (mvfxEventSpawn){.enabled = true,
                                          .source = source,
                                          .kind = kind,
                                          .minCount = 1,
                                          .maxCount = 3,
                                          .inheritVelocity = 0.5f,
                                          .inheritColor = true,
                                          .capacity = 512};
    return emitter;
}

static void TestDeaths(Gpu* gpu, mvfxGpu* kernels)
{
    // Births from deaths by age, in the world's space and the instance's,
    // under a moving instance: the event's place and velocity mapped into
    // the emitter's terms at its tick.
    mvfxEmitterDef emitters[2] = {Sprite(), Child(0, mvfx_eventDeath)};
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 4096;
    emitters[0].color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.2f, 0.2f, 0.2f, 1}, .max = {1, 1, 1, 1}};
    emitters[0].space = mvfx_spaceWorld;
    emitters[1].space = mvfx_spaceWorld;
    // Sizes and angles drawn in the step, by the births' event keys.
    emitters[1].size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.2f, 0.3f}, .max = {0.4f, 0.5f, 0.6f}};
    emitters[1].angle = mvfxRangeSource(0.0f, 6.0f);
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2, .moving = true},
            "births from deaths, in the world's space");
    emitters[0].space = mvfx_spaceLocal;
    emitters[1].space = mvfx_spaceLocal;
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2, .moving = true},
            "births from deaths, in the instance's space");
}

static void TestBirthsMerged(Gpu* gpu, mvfxGpu* kernels)
{
    // Births from births beside the emitter's own at its rate: their
    // serials interleave by tick, events' first at a tick.
    mvfxEmitterDef emitters[2] = {Sprite(), Child(0, mvfx_eventBirth)};
    emitters[0].events = mvfx_eventMaskBirth;
    emitters[0].eventCapacity = 4096;
    emitters[0].rate = 120.0f;
    emitters[1].rate = 300.0f;
    emitters[1].eventSpawn.maxCount = 2;
    Compare(gpu, kernels,
            &(Run){.emitters = emitters, .emitterCount = 2, .least = 20, .moving = true},
            "births from births, merged with its own");
}

static void TestCollisions(Gpu* gpu, mvfxGpu* kernels)
{
    // Births from collisions with a ceiling the particles rise into, two
    // a contact: a contact's tick rounds a share the GPU finds in binary32
    // (within a thousandth of the step).
    static const mvfxCollider ceiling = {
        .kind = mvfx_colliderPlane, .center = {0, 0.55f, 0}, .normal = {0, -1, 0}};
    mvfxEmitterDef emitters[2] = {Sprite(), Child(0, mvfx_eventCollision)};
    emitters[1].eventSpawn.minCount = 2;
    emitters[1].eventSpawn.maxCount = 2;
    emitters[0].events = mvfx_eventMaskCollision;
    emitters[0].eventCapacity = 4096;
    emitters[0].colliders = &ceiling;
    emitters[0].colliderCount = 1;
    emitters[0].collision = (mvfxCollision){.enabled = true, .restitution = 0.5f};
    emitters[0].space = mvfx_spaceWorld;
    emitters[1].space = mvfx_spaceWorld;
    Compare(gpu, kernels,
            &(Run){.emitters = emitters, .emitterCount = 2, .moving = true, .colliding = true},
            "births from collisions");
    // Then one to three a contact, drawn: a contact whose tick differs on
    // the two paths may give a different count, so the births of such
    // contacts are left out on both, bounded (rhi_harness.c).
    emitters[1].eventSpawn.minCount = 1;
    emitters[1].eventSpawn.maxCount = 3;
    Compare(gpu, kernels,
            &(Run){.emitters = emitters, .emitterCount = 2, .moving = true, .colliding = true},
            "births from collisions, a count drawn");
}

static void TestBounds(Gpu* gpu, mvfxGpu* kernels)
{
    // More births from events than a step takes: the latest by tick
    // refused, their serials spent; then a child too small for its births,
    // which replays them with its deaths, the last refused.
    mvfxEmitterDef emitters[2] = {Sprite(), Child(0, mvfx_eventDeath)};
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 4096;
    emitters[1].eventSpawn.capacity = 6;
    emitters[1].lifetime = mvfxRangeSource(0.4f, 0.6f);
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2, .moving = true},
            "births from events past the step's bound refused");
    emitters[1].eventSpawn.capacity = 512;
    emitters[1].capacity = 96;
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2, .moving = true},
            "a full emitter replaying births from events");
    // Then lives shorter than a step (5 to 30 ms): births die within the
    // step that takes them, freeing room by their drawn deaths.
    emitters[1].capacity = 24;
    emitters[1].lifetime = mvfxRangeSource(0.005f, 0.03f);
    Compare(gpu, kernels,
            &(Run){.emitters = emitters, .emitterCount = 2, .moving = true, .secondLeast = 20},
            "a full emitter of births living less than a step");
}

static void TestManyAndDelayed(Gpu* gpu, mvfxGpu* kernels)
{
    // Over a thousand deaths a step, more than one block of the sort's,
    // for an emitter whose start delay (0.15 s) leaves the first deaths'
    // events giving none.
    mvfxEmitterDef emitters[2] = {Sprite(), Child(0, mvfx_eventDeath)};
    emitters[0].capacity = 16384;
    emitters[0].rate = 120000.0f;
    emitters[0].lifetime = mvfxRangeSource(0.1f, 0.2f);
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 4096;
    emitters[1].capacity = 4096;
    emitters[1].startDelay = 0.15f;
    emitters[1].eventSpawn.minCount = 1;
    emitters[1].eventSpawn.maxCount = 1;
    emitters[1].eventSpawn.capacity = 2048;
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2, .moving = true},
            "over a thousand events a step, for a delayed emitter");
}

static void TestShortestLife(Gpu* gpu, mvfxGpu* kernels)
{
    // Event births drawn a life of 0 (a curve's: a range's lives are above
    // 0) live the shortest life, 2^-20 s, as the core clamps it: none
    // outlives its step, and their deaths' ticks are compared.
    static const mvfxCurveKey zero = {.time = 0, .value = 0};
    mvfxEmitterDef emitters[2] = {Sprite(), Child(0, mvfx_eventDeath)};
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 4096;
    emitters[1].lifetime = (mvfxScalarSource){
        .kind = mvfx_sourceCurve, .curve = {.keys = &zero, .keyCount = 1}, .range = {1, 1}};
    emitters[1].events = mvfx_eventMaskDeath;
    emitters[1].eventCapacity = 4096;
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2, .secondLeast = -1},
            "event births drawn no life");
}

static void TestLoopingAge(Gpu* gpu, mvfxGpu* kernels)
{
    // Event births of a looping emitter draw their lives over the loop's
    // share, which starts again at each loop (0.1 s a loop, forever).
    static const mvfxCurveKey keys[2] = {
        {.time = 0, .value = 0.1f, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 0.6f, .interpolation = mvfx_interpolationLinear}};
    mvfxEmitterDef emitters[2] = {Sprite(), Child(0, mvfx_eventDeath)};
    emitters[0].events = mvfx_eventMaskDeath;
    emitters[0].eventCapacity = 4096;
    emitters[1].duration = 0.1f;
    emitters[1].loops = 0;
    emitters[1].lifetime = (mvfxScalarSource){
        .kind = mvfx_sourceCurve, .curve = {.keys = keys, .keyCount = 2}, .range = {1, 1}};
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 2},
            "event births' lives over a looping emitter's age");
}

static void TestOrderAndHost(Gpu* gpu, mvfxGpu* kernels)
{
    // The spawning emitter first and its source after (the source steps
    // first), the host's particles queued for it: numbered before the
    // births from events, their velocities drawn at those serials.
    static const double positions[6] = {0.1, 0.2, 0.0, -0.3, 0.0, 0.1};
    const mvfxSpawnRequest spawn = {.count = 2, .positions = positions};
    mvfxEmitterDef emitters[2] = {Child(1, mvfx_eventDeath), Sprite()};
    emitters[0].hostSpawnCapacity = 8;
    emitters[1].events = mvfx_eventMaskDeath;
    emitters[1].eventCapacity = 4096;
    Compare(gpu, kernels,
            &(Run){.emitters = emitters, .emitterCount = 2, .moving = true, .spawn = &spawn},
            "a spawning emitter before its source, with the host's particles");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestDeaths(gpu, kernels);
    TestBirthsMerged(gpu, kernels);
    TestCollisions(gpu, kernels);
    TestBounds(gpu, kernels);
    TestManyAndDelayed(gpu, kernels);
    TestShortestLife(gpu, kernels);
    TestLoopingAge(gpu, kernels);
    TestOrderAndHost(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
