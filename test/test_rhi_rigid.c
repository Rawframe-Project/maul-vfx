// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles that meet nothing on a real device (lavapipe on CI)
// beside the CPU path, compared after every step (rhi_harness.h): their
// places, velocities, orientations, angular velocities and sleep within
// the tolerance (masses and the gyroscopic turn take square roots and
// division). Covered: spheres, capsules, boxes and hulls of sizes unequal
// along their axes tumbling under gravity and drag over several substeps
// (the gyroscopic term turning those of unequal inertias), newborns
// taking the rest of their step's substeps, sizes over life by age and by
// speed, in the world's space under a moving instance, and bodies coming
// to rest in a drag and falling asleep alone; what is still refused
// (collision, kills within a step). Skips (77) without an adapter, unless
// MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

// A lopsided hull: a wedge with a spike.
static const float s_lopsided[21] = {0,    0, 0,    1, 0, 0,    0, 0.6f, 0,    0,   0,
                                     0.3f, 1, 0.1f, 0, 0, 0.4f, 0, 0.7f, 0.8f, 1.1f};

// Sprites as rigid bodies of a shape, sizes unequal along their axes,
// random orientations and fast angular velocities, four substeps.
static mvfxEmitterDef Bodies(mvfxRigidShape shape)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.3f, 0.2f}, .max = {0.2f, 0.5f, 0.3f}};
    emitter.orientationMode = mvfx_orientationRandom;
    emitter.angularVelocity =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {-20, -20, -20}, .max = {20, 20, 20}};
    emitter.motion.angularDrag = 0.3f;
    emitter.rigid = (mvfxRigidDef){.enabled = true,
                                   .shape = shape,
                                   .density = 500.0f,
                                   .substeps = 4,
                                   .staticFriction = 0.5f,
                                   .dynamicFriction = 0.4f};
    return emitter;
}

static void Tumbled(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitter, bool moving,
                    const char* what)
{
    static const mvfxMeshDef hull = {
        .cookie = MVFX_MESH_DEF_COOKIE, .positions = s_lopsided, .vertexCount = 7};
    // A step at a time from the same state, within 1e-3: the inertias are
    // binary32 here and binary64 on the CPU path, and the gyroscopic turn
    // carries their rounding into the angular velocity a substep at a
    // time, which a body tumbling about its middle axis grows from step
    // to step (the Dzhanibekov effect).
    Compare(gpu, kernels,
            &(Run){.emitters = emitter,
                   .emitterCount = 1,
                   .moving = moving,
                   .pace = 0.001,
                   .adopting = true,
                   .tolerance = 1e-3f,
                   .mesh = emitter->rigid.shape == mvfx_rigidHull ? &hull : NULL},
            what);
}

static void TestShapes(Gpu* gpu, mvfxGpu* kernels)
{
    static const char* const names[4] = {"spheres tumbling", "capsules tumbling", "boxes tumbling",
                                         "hulls tumbling"};
    for (int32_t shape = 0; shape < 4; ++shape)
    {
        mvfxEmitterDef emitter = Bodies((mvfxRigidShape)shape);
        Tumbled(gpu, kernels, &emitter, false, names[shape]);
    }
    // Eight substeps in the world's space under a moving instance.
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox);
    emitter.rigid.substeps = 8;
    emitter.space = mvfx_spaceWorld;
    Tumbled(gpu, kernels, &emitter, true, "boxes in the world, the instance moving");
}

static void TestSizes(Gpu* gpu, mvfxGpu* kernels)
{
    // Sizes over life by age (one curve, then one an axis), then by speed:
    // the mass a body turns by is its size's at the step's end, by speed
    // its speed's at each substep.
    static const mvfxCurveKey grow[2] = {
        {.time = 0, .value = 0.5f, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 2.0f, .interpolation = mvfx_interpolationLinear}};
    static const mvfxCurveKey shrink[2] = {
        {.time = 0, .value = 1.5f, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 0.5f, .interpolation = mvfx_interpolationLinear}};
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox);
    emitter.overLife.size = (mvfxSizeOverLife){
        .enabled = true, .size = {{.value = 1.0f, .curve = {.keys = grow, .keyCount = 2}}}};
    Tumbled(gpu, kernels, &emitter, false, "boxes growing over life");
    emitter.overLife.size.perAxis = true;
    emitter.overLife.size.size[1] =
        (mvfxLifeCurve){.value = 1.0f, .curve = {.keys = shrink, .keyCount = 2}};
    emitter.overLife.size.size[2] = (mvfxLifeCurve){.value = 0.7f};
    Tumbled(gpu, kernels, &emitter, false, "boxes sized over life an axis each");
    emitter.overLife.size.input = mvfx_overSpeed;
    emitter.overLife.size.speedRange[0] = 1.0f;
    emitter.overLife.size.speedRange[1] = 4.0f;
    Tumbled(gpu, kernels, &emitter, false, "boxes sized by their speed");
}

static void TestSleep(Gpu* gpu, mvfxGpu* kernels)
{
    // No gravity and a strong drag: bodies come to rest within their life
    // and fall asleep alone, held still, their velocities none.
    mvfxEmitterDef emitter = Bodies(mvfx_rigidCapsule);
    emitter.motion.gravity[1] = 0.0f;
    emitter.motion.drag = 30.0f;
    emitter.motion.angularDrag = 30.0f;
    emitter.lifetime = mvfxRangeSource(1.0f, 1.5f);
    emitter.rigid.sleepSpeed = 0.05f;
    emitter.rigid.sleepTime = 0.1f;
    Tumbled(gpu, kernels, &emitter, false, "capsules falling asleep in a drag");
}

// Whether a planned instance of an emitter is refused on the GPU for its
// rigid particles.
static bool RefusedForRigid(mvfxGpu* kernels, const mvfxEmitterDef* emitter)
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
                   sim == NULL && refusal.emitter == 0 && refusal.features == MVFX_FEATURE_RIGID;
    mvfxDestroyContext(context);
    return refused;
}

static void TestRefused(mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Bodies(mvfx_rigidBox);
    emitter.collision.enabled = true;
    CHECK(RefusedForRigid(kernels, &emitter), "colliding rigid particles refused");
    static const mvfxKillRule rule = {.kind = mvfx_killPastPlane, .normal = {0, 1, 0}};
    emitter = Bodies(mvfx_rigidBox);
    emitter.killRules = &rule;
    emitter.killRuleCount = 1;
    CHECK(RefusedForRigid(kernels, &emitter), "rigid particles killed within a step refused");
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestRefused(kernels);
    TestShapes(gpu, kernels);
    TestSizes(gpu, kernels);
    TestSleep(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
