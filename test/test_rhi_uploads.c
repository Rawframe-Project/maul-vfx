// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The bytes a step of the GPU part uploads, on Maul RHI's test driver
// (mvfxGpuSim_GetUploadBytes): after the first step, each emitter's as
// mvfxGpuSim_Record states them, from its plan's forces, colliders, kill
// rules, births and host births, for a plain emitter, one with forces,
// colliders and kill rules, one the host spawns into, and one writing
// events with another spawning from them, and one of a mesh shape, its mesh
// uploaded at the first step alone; a surface's of waves and one's of
// shallow water beside one.

#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/test.h"
#include "maul-vfx-rhi/gpu.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/modules.h"
#include "maul-vfx/plan.h"

#define STEPS      30
#define STEP_TICKS (MVFX_TICKS_PER_SECOND / 60)

static mrhiTestAdapter s_adapter;
static mrhiTestDriverDef s_driver;

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

// A device on the test driver, logging its frames.
static bool Open(Gpu* gpu)
{
    *gpu = (Gpu){0};
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
    };
    s_driver = (mrhiTestDriverDef){
        .chain = {.next = NULL, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &s_driver.chain;
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    if (mrhiCreateInstance(&def, &gpu->instance) != mrhi_success ||
        mrhiInstance_RequestAdapters(gpu->instance, &search, &request) != mrhi_success ||
        mrhiInstance_TakeNotification(gpu->instance, &record) != mrhi_success ||
        mrhiInstance_GetAdapters(gpu->instance, &adapter, 1, &count) != mrhi_success || count != 1)
    {
        return false;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    deviceDef.budgets.frameUploadBytes = 4u << 20u;
    return mrhiCreateDevice(gpu->instance, &deviceDef, &gpu->device, &request) == mrhi_success &&
           mrhiInstance_TakeNotification(gpu->instance, &record) == mrhi_success &&
           record.outcome == mrhi_success;
}

static void Close(Gpu* gpu)
{
    if (gpu->device != NULL)
    {
        mrhiDestroyDevice(gpu->device);
    }
    if (gpu->instance != NULL)
    {
        mrhiDestroyInstance(gpu->instance);
    }
}

// Hands the object the device's notifications until it is ready.
static bool AwaitReady(Gpu* gpu, mvfxGpu* kernels)
{
    for (int32_t poll = 0; poll < 100 && !mvfxGpu_IsReady(kernels); ++poll)
    {
        mrhiDeviceNotification record;
        while (mrhiDevice_TakeNotification(gpu->device, &record) == mrhi_success)
        {
            (void)mvfxGpu_Notify(kernels, &record);
        }
    }
    return mvfxGpu_IsReady(kernels);
}

static const mvfxBurst s_burst = {
    .time = 0, .minCount = 50, .maxCount = 50, .cycles = 1, .probability = 1};

// A sprite emitter: a burst, then 600 a second, a sphere's shell.
static mvfxEmitterDef Sprite(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 600.0f;
    emitter.bursts = &s_burst;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(0.2f, 0.4f);
    emitter.shape = (mvfxShape){.kind = mvfx_shapeSphere, .rotation = {0, 0, 0, 1}, .radius = 0.5f};
    emitter.velocity = (mvfxVelocity){
        .mode = mvfx_directionCone, .direction = {0, 1, 0}, .coneAngle = 0.4f, .speed = {2, 4}};
    emitter.motion.gravity[1] = -9.8f;
    return emitter;
}

// The bytes an emitter's grid numbers upload a step after the first:
// the clock's and the shift's, and for one that steers its targets.
static int64_t GridBytes(const mvfxEmitterDef* emitter)
{
    const mvfxSteeringDef* s = &emitter->steering;
    if (s->enable || emitter->pairForce.enable)
    {
        return 160 + 16 * (s->path.weight > 0.0f ? s->path.pointCount : 0);
    }
    return emitter->neighbors.enable ? 32 : 0;
}

// The bytes an emitter's step uploads after the first, from its plan,
// whether it writes events, whether it is a beam, whether it has contacts
// and its grid numbers', as mvfxGpuSim_Record states them.
static int64_t StepBytes(const mvfxEmitterPlan* plan, bool events, bool beam, bool contacts,
                         int64_t grid)
{
    return 1960 + (events ? 104 : 0) + (beam ? 96 : 0) + (contacts ? 96 : 0) + grid +
           432 * (int64_t)plan->forceCount + 128 * (int64_t)plan->colliderCount +
           80 * (int64_t)plan->killRuleCount + 48 * (int64_t)plan->birthCount +
           64 * (int64_t)plan->hostBirthCount;
}

// A water surface the measured effect holds beside its emitters (none for
// NULL): its numbers and its record, 1,132 bytes a step for waves, 2,540
// for shallow water.
static const mvfxWaterDef* s_water;

// Steps an effect's planned instance on the device, the host spawning
// into the first emitter each step when it may; whether every frame after
// the first uploaded what its emitters' plans say, and some step had
// births.
static void Measure(Gpu* gpu, mvfxGpu* kernels, const mvfxEmitterDef* emitters, int32_t count,
                    const char* what)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    // A mesh shape's emitter spawns from a triangle pair made here.
    static const float corners[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
    static const uint32_t triangles[6] = {0, 1, 2, 1, 3, 2};
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = corners;
    meshDef.vertexCount = 4;
    meshDef.triangles = triangles;
    meshDef.triangleCount = 2;
    mvfxEmitterDef own[4];
    for (int32_t e = 0; e < count; ++e)
    {
        own[e] = emitters[e];
        if (own[e].shape.kind == mvfx_shapeMesh)
        {
            CHECK(mvfxCreateMesh(context, &meshDef, &own[e].shape.mesh) == mvfx_success, "mesh");
        }
    }
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = own;
    effectDef.emitterCount = count;
    effectDef.waters = s_water;
    effectDef.waterCount = s_water != NULL ? 1 : 0;
    mvfxEffectId effect;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.planned = true;
    mvfxInstanceId instance;
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = instance;
    mvfxGpuSim* sim = NULL;
    mvfxGpuRefusal refusal;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success, "simulation");
    int64_t none = -1;
    CHECK(mvfxGpuSim_GetUploadBytes(sim, &none) == mvfx_success && none == 0,
          "none before a record");
    static const double places[3 * 4] = {0, 0, 0, 0.1, 0, 0, 0, 0.1, 0, 0, 0, 0.1};
    const mvfxSpawnRequest spawn = {.positions = places, .count = 4};
    bool exact = true;
    int64_t births = 0;
    int64_t last = 0;
    for (int32_t s = 0; exact && s < STEPS; ++s)
    {
        if (emitters[0].hostSpawnCapacity > 0)
        {
            CHECK(mvfxInstance_Spawn(context, instance, 0, &spawn) == mvfx_success, "spawned");
        }
        CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success, "stepped");
        mrhiFrameDef frame = mrhiDefaultFrameDef();
        mrhiRequestId token = {0};
        exact = mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
                mvfxGpuSim_AddPasses(sim) == mvfx_success &&
                mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
                mvfxGpuSim_Record(sim) == mvfx_success &&
                mrhiDevice_SubmitFrame(gpu->device, &token) == mrhi_success &&
                mrhiRequest_WaitFrame(gpu->device, token, 1000000000ull) == mrhi_success;
        // Each frame's answer taken, so the scenes' frames never fill the
        // device's queue.
        mrhiDeviceNotification record;
        while (mrhiDevice_TakeNotification(gpu->device, &record) == mrhi_success)
        {
            (void)mvfxGpu_Notify(kernels, &record);
        }
        int64_t expected =
            s_water == NULL ? 0 : (s_water->model == mvfx_waterShallow ? 2540 : 1132);
        for (int32_t e = 0; exact && e < count; ++e)
        {
            mvfxEmitterPlan plan;
            exact = mvfxInstance_GetEmitterPlan(context, instance, e, &plan) == mvfx_success;
            expected += StepBytes(&plan, emitters[e].events != 0, emitters[e].beam.enable,
                                  emitters[e].contacts.enable, GridBytes(&emitters[e]));
            births += plan.birthCount + plan.hostBirthCount;
        }
        int64_t uploaded = -1;
        exact = exact && mvfxGpuSim_GetUploadBytes(sim, &uploaded) == mvfx_success;
        if (exact && s > 0 && uploaded != expected)
        {
            printf("step %d: %lld bytes uploaded, %lld stated\n", s, (long long)uploaded,
                   (long long)expected);
            exact = false;
        }
        last = uploaded;
    }
    printf("%s: %lld bytes the last step\n", what, (long long)last);
    CHECK(exact && births > 0, what);
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
}

static void TestUploads(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef plain = Sprite();
    Measure(gpu, kernels, &plain, 1, "a plain emitter");

    static const mvfxForce forces[2] = {
        {.kind = mvfx_forceConstant,
         .vector = {1, 0, 0},
         .strength = {.kind = mvfx_sourceRange, .range = {2.0f, 2.0f}}},
        {.kind = mvfx_forceAttractor,
         .point = {0, 1, 0},
         .strength = {.kind = mvfx_sourceRange, .range = {1.0f, 1.0f}},
         .softening = 0.2f}};
    static const mvfxCollider colliders[3] = {
        {.kind = mvfx_colliderPlane, .center = {0, -1, 0}, .normal = {0, 1, 0}},
        {.kind = mvfx_colliderPlane, .center = {0, 2, 0}, .normal = {0, -1, 0}},
        {.kind = mvfx_colliderPlane, .center = {2, 0, 0}, .normal = {-1, 0, 0}}};
    static const mvfxKillRule rule = {
        .kind = mvfx_killInsideSphere, .center = {0, 3, 0}, .radius = 0.5f};
    mvfxEmitterDef modules = Sprite();
    modules.forces = forces;
    modules.forceCount = 2;
    modules.colliders = colliders;
    modules.colliderCount = 3;
    modules.collision = (mvfxCollision){.enable = true, .restitution = 0.5f};
    modules.killRules = &rule;
    modules.killRuleCount = 1;
    Measure(gpu, kernels, &modules, 1, "forces, colliders and a kill rule");

    mvfxEmitterDef hosted = Sprite();
    hosted.hostSpawnCapacity = 64;
    Measure(gpu, kernels, &hosted, 1, "the host's particles");

    mvfxEmitterDef pair[2] = {Sprite(), Sprite()};
    pair[0].events = mvfx_eventMaskDeath;
    pair[0].eventCapacity = 4096;
    pair[1].rate = 0.0f;
    pair[1].burstCount = 0;
    pair[1].eventSpawn = (mvfxEventSpawn){
        .enable = true, .kind = mvfx_eventDeath, .minCount = 1, .maxCount = 2, .capacity = 512};
    Measure(gpu, kernels, pair, 2, "events and births from them");

    // A mesh shape's mesh goes up at the first step alone while it stays
    // the same.
    mvfxEmitterDef meshed = Sprite();
    meshed.shape =
        (mvfxShape){.kind = mvfx_shapeMesh, .rotation = {0, 0, 0, 1}, .meshMode = mvfx_meshSurface};
    Measure(gpu, kernels, &meshed, 1, "a mesh shape's emitter");

    mvfxEmitterDef beam = mvfxDefaultEmitterDef();
    beam.rate = 0.0f;
    beam.capacity = 9;
    beam.lifetime = mvfxRangeSource(0.05f, 0.1f);
    beam.ribbon.ids = mvfx_ribbonOne;
    beam.beam = (mvfxBeamDef){
        .enable = true, .segments = 8, .start = {.point = {0, 0, 0}}, .end = {.point = {4, 0, 0}}};
    Measure(gpu, kernels, &beam, 1, "a beam");
    mvfxEmitterDef neighbors = Sprite();
    neighbors.neighbors =
        (mvfxNeighborDef){.enable = true, .source = -1, .radius = 0.2f, .maxNeighbors = 8};
    Measure(gpu, kernels, &neighbors, 1, "an emitter finding its neighbors");
    static const float path[9] = {0, 0, 0, 1, 1, 0, 2, 0, 1};
    neighbors.steering = (mvfxSteeringDef){
        .enable = true,
        .cruiseSpeed = 2.0f,
        .path = {.weight = 1.0f, .points = path, .pointCount = 3, .lookahead = 0.2f}};
    Measure(gpu, kernels, &neighbors, 1, "an emitter steering along a path");
    mvfxEmitterDef touching = Sprite();
    touching.contacts =
        (mvfxContactDef){.enable = true, .radiusScale = 0.02f, .iterations = 2, .maxContacts = 8};
    Measure(gpu, kernels, &touching, 1, "an emitter keeping its particles apart");
    // Its plexus numbers go up at the first step alone.
    mvfxEmitterDef linked = Sprite();
    linked.plexus = (mvfxPlexusDef){
        .enable = true, .distance = 0.2f, .maxLinks = 256, .maxPerParticle = 4, .width = 1.0f};
    Measure(gpu, kernels, &linked, 1, "an emitter linking its particles");
    mvfxWaterDef water = mvfxDefaultWaterDef();
    s_water = &water;
    Measure(gpu, kernels, &plain, 1, "a water surface beside an emitter");
    water.model = mvfx_waterShallow;
    Measure(gpu, kernels, &plain, 1, "a shallow water surface beside an emitter");
    s_water = NULL;
}

static void TestRefusals(void)
{
    int64_t bytes = -1;
    CHECK(mvfxGpuSim_GetUploadBytes(NULL, &bytes) == mvfx_errorInvalid && bytes == 0 &&
              mvfxGpuSim_GetUploadBytes((const mvfxGpuSim*)&bytes, NULL) == mvfx_errorInvalid,
          "NULL arguments refused");
}

int main(void)
{
    TestRefusals();
    Gpu gpu;
    CHECK(Open(&gpu), "a device");
    mvfxGpuDef def = mvfxDefaultGpuDef();
    def.device = gpu.device;
    mvfxGpu* kernels = NULL;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success && AwaitReady(&gpu, kernels), "kernels");
    TestUploads(&gpu, kernels);
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
