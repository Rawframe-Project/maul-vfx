// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Events of planned instances written on a real device (lavapipe on CI)
// beside the CPU path's, compared after every step (rhi_harness.h): births,
// deaths by age and by kills, collisions, in the CPU path's order, bounded
// and counted. Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is
// set.

#include "rhi_harness.h"
#include "test_harness.h"

#include "maul-vfx/modules.h"

// A ceiling the particles rise into.
static const mvfxCollider s_ceiling = {
    .kind = mvfx_colliderPlane, .center = {0, 0.55f, 0}, .normal = {0, -1, 0}};

static void TestBirthsAndDeaths(Gpu* gpu, mvfxGpu* kernels)
{
    // Births and deaths by age, in the world's space and the instance's,
    // under a moving instance.
    mvfxEmitterDef emitter = Sprite();
    emitter.events = mvfx_eventMaskBirth | mvfx_eventMaskDeath;
    emitter.eventCapacity = 4096;
    emitter.space = mvfx_spaceWorld;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true},
            "births and deaths, in the world's space");
    emitter.space = mvfx_spaceLocal;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true},
            "births and deaths, in the instance's space");
}

static void TestCollisions(Gpu* gpu, mvfxGpu* kernels)
{
    // Every kind the GPU writes as particles rise into a ceiling: bounced,
    // in the world's space and the instance's; each contact taking 60% of
    // the life left, which ends some lives (deaths by a kill); a contact
    // that kills (few live to a step's end); a kill rule, under an
    // instance that barely moves.
    static const mvfxKillRule rule = {
        .kind = mvfx_killInsideSphere, .center = {0, 0.3f, 0}, .radius = 0.35f};
    mvfxEmitterDef emitter = Sprite();
    emitter.events = mvfx_eventMaskBirth | mvfx_eventMaskDeath | mvfx_eventMaskCollision;
    emitter.eventCapacity = 4096;
    emitter.colliders = &s_ceiling;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enabled = true, .restitution = 0.5f};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .colliding = true},
            "collisions bounced, in the instance's space");
    emitter.space = mvfx_spaceWorld;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .colliding = true},
            "collisions bounced, in the world's space");
    emitter.collision.lifeLoss = 0.6f;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .colliding = true},
            "collisions taking lives");
    emitter.collision.lifeLoss = 0.0f;
    emitter.collision.killOnCollision = true;
    Compare(
        gpu, kernels,
        &(Run){
            .emitters = &emitter, .emitterCount = 1, .least = 5, .moving = true, .killing = true},
        "collisions that kill");
    emitter.collision.killOnCollision = false;
    emitter.killRules = &rule;
    emitter.killRuleCount = 1;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .least = 20,
                   .moving = true,
                   .pace = 0.001,
                   .killing = true},
            "kills by a rule");
}

static void TestValues(Gpu* gpu, mvfxGpu* kernels)
{
    // Events' colours and sizes with the values over life (a colour by a
    // gradient at the speed with alpha over life, sizes an axis by age)
    // and stored values drawn at birth, in the world's space (the speed
    // in the emitter's units under a growing instance) and the instance's;
    // then under the ceiling in the world's space, each contact taking 60%
    // of the life left, so that deaths by a kill (births' among them) are
    // aged by the inverse life their contacts left. (In the instance's
    // space a grazing contact there counts on one side and not the other.)
    static const mvfxCurveKey keys[2] = {
        {.time = 0, .value = 1, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 0.25f, .interpolation = mvfx_interpolationLinear}};
    static const mvfxColorKey colors[2] = {{0, 1, 0.5f, 0}, {1, 0, 0.25f, 1}};
    static const mvfxAlphaKey alphas[2] = {{0, 1}, {1, 0.5f}};
    const mvfxCurve curve = {.keys = keys, .keyCount = 2};
    mvfxEmitterDef emitter = Sprite();
    emitter.events = mvfx_eventMaskBirth | mvfx_eventMaskDeath;
    emitter.eventCapacity = 4096;
    emitter.space = mvfx_spaceWorld;
    emitter.color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.2f, 0.2f, 0.2f, 1}, .max = {1, 1, 1, 1}};
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.2f, 0.3f}, .max = {0.4f, 0.5f, 0.6f}};
    emitter.overLife = (mvfxOverLife){
        .color = {.enabled = true,
                  .input = mvfx_overSpeed,
                  .speedRange = {0.5f, 4},
                  .gradient = {.colorKeys = colors,
                               .colorKeyCount = 2,
                               .alphaKeys = alphas,
                               .alphaKeyCount = 2},
                  .scaleAlpha = true,
                  .alpha = {.value = 1, .curve = curve}},
        .size = {
            .enabled = true,
            .perAxis = true,
            .size = {{.value = 2, .curve = curve}, {.value = 1}, {.value = 3, .curve = curve}}}};
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true},
            "events' values over life, in the world's space");
    emitter.space = mvfx_spaceLocal;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true},
            "events' values over life, in the instance's space");
    emitter.space = mvfx_spaceWorld;
    emitter.colliders = &s_ceiling;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enabled = true, .restitution = 0.5f, .lifeLoss = 0.6f};
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .colliding = true},
            "events' values over life after contacts' life loss");
}

static void TestBound(Gpu* gpu, mvfxGpu* kernels)
{
    // Fewer events a step than the particles give: the rest dropped and
    // counted, the order's first kept.
    mvfxEmitterDef emitter = Sprite();
    emitter.events = mvfx_eventMaskBirth | mvfx_eventMaskDeath;
    emitter.eventCapacity = 7;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true},
            "events past the bound dropped");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestBirthsAndDeaths(gpu, kernels);
    TestCollisions(gpu, kernels);
    TestValues(gpu, kernels);
    TestBound(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
