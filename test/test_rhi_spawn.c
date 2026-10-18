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
    // (within a thousandth of the step), and a count drawn from it could
    // differ.
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
    TestOrderAndHost(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
