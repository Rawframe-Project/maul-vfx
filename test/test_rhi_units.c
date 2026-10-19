// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Units (chains and sheets) on a real device (lavapipe on CI) beside the
// CPU path, compared after every step (rhi_harness.h): their points'
// places and velocities within the tolerance (the projections take sqrt
// and division). Covered: rigid and compliant chains over several
// substeps with damping, sheets with shear and bending under a moving
// instance in the world's space, a capacity that binds (whole units
// refused), and what units are still refused beside (pins, tearing,
// aerodynamics, self-collision, colliders).
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// A sprite emitter of chains of points, each spacing apart.
static mvfxEmitterDef Chains(int32_t points, int32_t substeps)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.rate = 120.0f;
    emitter.unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                 .points = points,
                                 .spacing = 0.05f,
                                 .direction = {1, 0, 0},
                                 .downDirection = {0, 1, 0},
                                 .substeps = substeps,
                                 .damping = 2.0f};
    return emitter;
}

// Sheets of across by rows points.
static mvfxEmitterDef Sheets(int32_t across, int32_t rows, int32_t substeps)
{
    mvfxEmitterDef emitter = Chains(across, substeps);
    emitter.rate = 30.0f;
    emitter.unit.kind = mvfx_unitSheet;
    emitter.unit.rows = rows;
    emitter.unit.downDirection[0] = 0.0f;
    emitter.unit.downDirection[2] = 1.0f;
    emitter.unit.downDirection[1] = 0.0f;
    emitter.unit.shearCompliance = 1e-4f;
    emitter.unit.bendCompliance = 1e-3f;
    return emitter;
}

// A moving instance turns and scales at the harness's pace but moves at a
// thousandth of it: a unit's velocity takes its correction over a
// substep's seconds, so a point the instance leaves hundreds of metres
// from the origin, where a binary32 place's last unit is about 3e-5,
// carries one unit's rounding as 7e-3 into its velocity on either path.
static void Moved(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, bool moving,
                  const char* what)
{
    // Within 1e-3: a projection carries a point's difference to its
    // neighbours, and the velocity takes the substep's correction over the
    // substep's seconds.
    Compare(gpu, kernels,
            &(Run){.emitters = emitter,
                   .emitterCount = 1,
                   .moving = moving,
                   .pace = 0.001,
                   .tolerance = 1e-3f},
            what);
}

static void TestChains(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Chains(8, 4);
    Moved(gpu, kernels, &emitter, false, "rigid chains of eight over four substeps");
    emitter = Chains(16, 8);
    emitter.unit.stretchCompliance = 1e-3f;
    emitter.unit.bendCompliance = 1e-2f;
    Moved(gpu, kernels, &emitter, false, "compliant chains over eight substeps");
    // Far more births than room: whole units refused.
    emitter = Chains(8, 2);
    emitter.rate = 2000.0f;
    Moved(gpu, kernels, &emitter, false, "chains past the capacity");
}

static void TestSheets(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Sheets(8, 4, 2);
    Moved(gpu, kernels, &emitter, false, "sheets of eight by four");
    emitter.space = mvfx_spaceWorld;
    Moved(gpu, kernels, &emitter, true, "sheets in the world, the instance moving");
}

// Whether a planned instance of an effect of one emitter is refused on the
// GPU for its units.
static bool RefusedForUnits(mvfxGpu* kernels, const mvfxEmitterDef* emitter)
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
                   sim == NULL && refusal.emitter == 0 && refusal.features == MVFX_FEATURE_UNITS;
    mvfxDestroyContext(context);
    return refused;
}

static void TestRefused(mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Chains(4, 2);
    emitter.unit.pinFirst = true;
    CHECK(RefusedForUnits(kernels, &emitter), "pinned units refused");
    emitter = Chains(4, 2);
    emitter.unit.tearStrain = 0.5f;
    CHECK(RefusedForUnits(kernels, &emitter), "tearing units refused");
    emitter = Sheets(4, 4, 2);
    emitter.unit.normalDrag = 0.5f;
    CHECK(RefusedForUnits(kernels, &emitter), "sheets in the air refused");
    emitter = Chains(4, 2);
    emitter.unit.selfThickness = 0.02f;
    emitter.unit.selfContacts = 8;
    CHECK(RefusedForUnits(kernels, &emitter), "self-colliding units refused");
    static const mvfxCollider plane = {.kind = mvfx_colliderPlane,
                                       .center = {0, -2, 0},
                                       .normal = {0, 1, 0},
                                       .rotation = {0, 0, 0, 1}};
    emitter = Chains(4, 2);
    emitter.colliders = &plane;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enabled = true};
    CHECK(RefusedForUnits(kernels, &emitter), "units beside colliders refused");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestRefused(kernels);
    TestChains(gpu, kernels);
    TestSheets(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
