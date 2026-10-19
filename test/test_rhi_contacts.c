// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particle contacts on a real device (lavapipe on CI) beside the CPU path,
// compared after every step (rhi_harness.h), the CPU path then taking the
// GPU's places and velocities, so each step is compared from the same
// state: a pair the paths place within the tolerance of touching may touch
// on one path only. Places and velocities within 1e-3, since a contact's
// line turns by the places' difference over the pair's distance (sqrt and
// division are not exact in WGSL); the particles whose contacts passed the
// most exactly. Covered: sized particles over several
// passes with restitution and friction, a cap that binds, sizes over life
// by age and by speed, no stored size, the world's space under a moving
// instance, and the effects refused beside contacts (kills, ribbons).
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// A sprite emitter of sized particles kept apart.
static mvfxEmitterDef Touching(int32_t iterations, int32_t most)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.04f, 0.04f, 0.04f}, .max = {0.1f, 0.1f, 0.1f}};
    emitter.contacts = (mvfxContactDef){.enabled = true,
                                        .radiusScale = 0.5f,
                                        .iterations = iterations,
                                        .maxContacts = most,
                                        .restitution = 0.3f,
                                        .friction = 0.2f};
    return emitter;
}

static void Touched(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, bool moving,
                    int32_t leastCapped, const char* what)
{
    Compare(gpu, kernels,
            &(Run){.emitters = emitter,
                   .emitterCount = 1,
                   .moving = moving,
                   .leastContactsCapped = leastCapped,
                   .tolerance = 1e-3f,
                   .adopting = true},
            what);
}

static void TestContacts(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Touching(4, 16);
    Touched(gpu, kernels, &emitter, false, 0, "sized particles over four passes");
    // Born packed in a small sphere: most touch more than two.
    emitter = Touching(1, 2);
    emitter.shape.radius = 0.12f;
    emitter.rate = 2000.0f;
    Touched(gpu, kernels, &emitter, false, 50, "one pass, a cap of two that binds");
    emitter = Touching(3, 32);
    emitter.shape.radius = 0.12f;
    emitter.rate = 2000.0f;
    emitter.space = mvfx_spaceWorld;
    Touched(gpu, kernels, &emitter, true, 0, "in the world, the instance moving");
}

static void TestSizes(Gpu* gpu, mvfxGpu* kernels)
{
    // Sizes over life by age, then by speed; then no stored size, the
    // radius the scale alone.
    static const mvfxCurveKey keys[2] = {
        {.time = 0, .value = 0.5f, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 2.0f, .interpolation = mvfx_interpolationLinear}};
    mvfxEmitterDef emitter = Touching(2, 16);
    emitter.overLife.size = (mvfxSizeOverLife){
        .enabled = true, .size = {{.value = 1.0f, .curve = {.keys = keys, .keyCount = 2}}}};
    Touched(gpu, kernels, &emitter, false, 0, "sizes over life by age");
    emitter.overLife.size.input = mvfx_overSpeed;
    emitter.overLife.size.speedRange[0] = 0.5f;
    emitter.overLife.size.speedRange[1] = 4.0f;
    Touched(gpu, kernels, &emitter, false, 0, "sizes over life by speed");
    emitter = Touching(2, 16);
    emitter.size = (mvfxVectorSource){0};
    emitter.contacts.radiusScale = 0.03f;
    Touched(gpu, kernels, &emitter, false, 0, "no stored size, the scale alone");
}

// Whether a planned instance of an effect of one emitter is refused on the
// GPU for its contacts.
static bool RefusedForContacts(mvfxGpu* kernels, const mvfxEmitterDef* emitter)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
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
                   sim == NULL && refusal.emitter == 0 && refusal.features == MVFX_FEATURE_CONTACTS;
    mvfxDestroyContext(context);
    return refused;
}

static void TestRefused(mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Touching(2, 16);
    static const mvfxKillRule rule = {.kind = mvfx_killSlower, .speed = 0.1f};
    emitter.killRules = &rule;
    emitter.killRuleCount = 1;
    CHECK(RefusedForContacts(kernels, &emitter), "contacts beside a kill rule refused");
    emitter = Touching(2, 16);
    static const mvfxCollider plane = {.kind = mvfx_colliderPlane,
                                       .center = {0, -2, 0},
                                       .normal = {0, 1, 0},
                                       .rotation = {0, 0, 0, 1}};
    emitter.colliders = &plane;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enabled = true, .killOnCollision = true};
    CHECK(RefusedForContacts(kernels, &emitter), "contacts beside colliders that kill refused");
    emitter = Touching(2, 16);
    static const mvfxForce attractor = {
        .kind = mvfx_forceAttractor, .strength = 1.0f, .softening = 0.2f, .killRadius = 0.1f};
    emitter.forces = &attractor;
    emitter.forceCount = 1;
    CHECK(RefusedForContacts(kernels, &emitter), "contacts beside an attractor that kills refused");
    emitter = Touching(2, 16);
    emitter.ribbon.ids = mvfx_ribbonOne;
    CHECK(RefusedForContacts(kernels, &emitter), "contacts beside ribbons refused");
    emitter = Touching(2, 16);
    emitter.collision = (mvfxCollision){.enabled = true};
    emitter.colliders = &plane;
    emitter.colliderCount = 1;
    CHECK(!RefusedForContacts(kernels, &emitter), "contacts beside colliders that do not kill run");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestRefused(kernels);
    TestContacts(gpu, kernels);
    TestSizes(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
