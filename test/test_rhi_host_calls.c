// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host code calling back into a simulation of the GPU part, on Maul RHI's
// test driver: from its allocator, while mvfxDestroyGpuSim releases the
// host's particles' memory, each call that changes the simulation is
// refused with mvfx_errorBusy and changes nothing, a second destruction
// does nothing, a reading call still answers, and the destruction then
// gives back all the memory taken. And the part's calls that take the
// context mutable refused from the context's host code, and taken outside
// it. And its creation failing at each of its allocations in turn, for an
// effect with a liquid gas (whose split steps keep lanes): refused, all it
// took given back. And the part's calls that take a const context run no
// host code.

#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/test.h"
#include "maul-vfx-rhi/gpu.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/mesh.h"
#include "maul-vfx/modules.h"

#include <stdint.h>
#include <stdlib.h>

static mrhiTestAdapter s_adapter;
static mrhiTestDriverDef s_driver;

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

// A device on the test driver.
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

// The simulation the allocator calls back into at its first release while
// armed, the bytes it holds, and whether the calls back in were refused
// as they should be.
static mvfxGpuSim* s_target;
static int64_t s_held;
static int32_t s_made;
static bool s_refused;
// The allocation the allocator fails (from 1, 0 for none) and those it
// was asked for.
static int32_t s_failAt;
static int32_t s_calls;

// Calls back in once: each changing call refused, the upload count its
// last record left unchanged, the destruction doing nothing.
static void CallBack(void)
{
    mvfxGpuSim* sim = s_target;
    if (sim == NULL)
    {
        return;
    }
    s_target = NULL;
    int64_t before = -1;
    int64_t after = -1;
    mvfxGpuRecordsDef records = mvfxDefaultGpuRecordsDef();
    records.records = (mrhiResourceId){.index1 = 1};
    bool refused = mvfxGpuSim_GetUploadBytes(sim, &before) == mvfx_success && before > 0 &&
                   mvfxGpuSim_SetDepthTextures(sim, (mrhiResourceId){0}, (mrhiResourceId){0}) ==
                       mvfx_errorBusy &&
                   mvfxGpuSim_AddPasses(sim) == mvfx_errorBusy &&
                   mvfxGpuSim_AddRecords(sim, &records) == mvfx_errorBusy &&
                   mvfxGpuSim_Record(sim) == mvfx_errorBusy;
    mvfxDestroyGpuSim(sim);
    s_refused =
        refused && mvfxGpuSim_GetUploadBytes(sim, &after) == mvfx_success && after == before;
    s_made += 1;
}

// Memory aligned by hand (not every C library has aligned_alloc), the
// block's start kept before it.
static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    if (++s_calls == s_failAt)
    {
        return NULL;
    }
    uint8_t* raw = malloc(size + alignment + sizeof(void*));
    if (raw == NULL)
    {
        return NULL;
    }
    uintptr_t at = ((uintptr_t)(raw + sizeof(void*)) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void**)at)[-1] = raw;
    s_held += (int64_t)size;
    return (void*)at;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    CallBack();
    s_held -= (int64_t)size;
    free(((void**)memory)[-1]);
}

static const mvfxBurst s_burst = {
    .time = 0, .minCount = 50, .maxCount = 50, .cycles = 1, .probability = 1};

static void TestCallsBackIn(Gpu* gpu, mvfxGpu* kernels)
{
    // An emitter the host spawns into: the simulation keeps memory for the
    // host's particles, which its destruction releases first.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.bursts = &s_burst;
    emitter.burstCount = 1;
    emitter.shape = (mvfxShape){.kind = mvfx_shapeSphere, .rotation = {0, 0, 0, 1}, .radius = 0.5f};
    emitter.velocity = (mvfxVelocity){
        .mode = mvfx_directionCone, .direction = {0, 1, 0}, .coneAngle = 0.4f, .speed = {2, 4}};
    emitter.hostSpawnCapacity = 64;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.planned = true;
    mvfxInstanceId instance;
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.allocator = (mvfxAllocator){.alloc = Alloc, .free = Free};
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = instance;
    mvfxGpuSim* sim = NULL;
    mvfxGpuRefusal refusal;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success && s_held > 0, "simulation");
    // A step recorded, so its uploads are counted.
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success, "stepped");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
              mvfxGpuSim_AddPasses(sim) == mvfx_success &&
              mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
              mvfxGpuSim_Record(sim) == mvfx_success &&
              mrhiDevice_SubmitFrame(gpu->device, &token) == mrhi_success &&
              mrhiRequest_WaitFrame(gpu->device, token, 1000000000ull) == mrhi_success,
          "a frame");
    // Its destruction in an open frame, called back into from its first
    // release; the frame then empty, and all its memory given back.
    CHECK(mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success, "a frame open");
    s_target = sim;
    mvfxDestroyGpuSim(sim);
    CHECK(s_made == 1 && s_refused, "calls back in from the allocator refused");
    CHECK(mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
              mrhiDevice_SubmitFrame(gpu->device, &token) == mrhi_success &&
              mrhiRequest_WaitFrame(gpu->device, token, 1000000000ull) == mrhi_success,
          "the frame open meanwhile untouched");
    CHECK(s_held == 0, "the memory all given back");
    mvfxDestroyContext(context);
}

static void TestFailedMemory(mvfxGpu* kernels)
{
    // A 2D liquid gas of 8 by 8 cells and its liquid emitter.
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.kind = mvfx_gasLiquid;
    gas.size[0] = 8;
    gas.size[1] = 8;
    gas.size[2] = 1;
    gas.cellSize = 0.1f;
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 256;
    emitter.liquid.enable = true;
    emitter.liquid.solver = mvfx_liquidGrid;
    emitter.liquid.gas = 0;
    emitter.liquid.spacing = 0.05f;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    effectDef.gases = &gas;
    effectDef.gasCount = 1;
    mvfxEffectId effect;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success,
          "a liquid gas's effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.planned = true;
    mvfxInstanceId instance;
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.allocator = (mvfxAllocator){.alloc = Alloc, .free = Free};
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = instance;
    bool made = false;
    int32_t failed = 0;
    for (int32_t n = 1; !made && n < 100000; ++n)
    {
        s_failAt = n;
        s_calls = 0;
        mvfxGpuSim* sim = NULL;
        mvfxGpuRefusal refusal;
        made = mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success;
        if (made)
        {
            mvfxDestroyGpuSim(sim);
        }
        else
        {
            CHECK(sim == NULL && s_held == 0, "a failed creation's memory all given back");
            failed += 1;
        }
    }
    s_failAt = 0;
    CHECK(made && failed > 0 && s_held == 0, "made once its allocations do not fail");
    mvfxDestroyContext(context);
}

// The tasks a context has handed the host and waited on.
static int32_t s_tasks;

static void* Enqueue(mvfxTaskFunction* task, int32_t itemCount, int32_t minRange, void* taskContext,
                     void* userContext)
{
    (void)minRange;
    (void)userContext;
    s_tasks += 1;
    task(0, itemCount, taskContext);
    return &s_tasks;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
    s_tasks += 1;
}

static void TestConstCalls(void)
{
    // A context on the counting allocator and task hooks, with a mesh and
    // a heightfield's grid: the part's calls taking it const answer, and
    // make no allocation, free nothing and run no task.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.allocator = (mvfxAllocator){.alloc = Alloc, .free = Free};
    contextDef.enqueueTask = Enqueue;
    contextDef.finishTask = Finish;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static const float corners[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = corners;
    meshDef.vertexCount = 4;
    mvfxMeshId mesh = {0};
    CHECK(mvfxCreateMesh(context, &meshDef, &mesh) == mvfx_success, "mesh");
    static const float heights[4] = {0, 0.1f, 0.2f, 0.3f};
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[2] = 2;
    gridDef.values = heights;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(context, &gridDef, &grid) == mvfx_success, "grid");
    const mvfxCollider ground = {.kind = mvfx_colliderHeightfield,
                                 .halfExtents = {1, 1, 1},
                                 .rotation = {0, 0, 0, 1},
                                 .grid = grid};
    int32_t calls = s_calls;
    int64_t held = s_held;
    int32_t tasks = s_tasks;
    int64_t gridBytes = -1;
    int64_t meshBytes = -1;
    CHECK(mvfxContext_GetGpuHostGridBytes(context, &ground, 1, &gridBytes) == mvfx_success &&
              gridBytes > 0 &&
              mvfxMesh_GetGpuBytes(context, mesh, mvfx_meshVertices, &meshBytes) == mvfx_success &&
              meshBytes > 0 && s_calls == calls && s_held == held && s_tasks == tasks,
          "the part's const calls answer and run no host code");
    mvfxDestroyContext(context);
}

// The part's calls that take the context mutable, each called back in
// from a host module of another instance of the context through one
// table: refused as busy, one misuse counted, the planned instance's mesh
// and its gas's and water surface's reports left as they were. Each is
// called with arguments it takes, so its refusal is the host code's.
// tools/check_host_calls.py refuses such a call of the part's headers that
// the table lacks.
typedef mvfxResult (*Change)(mvfxContext* context);

static mvfxGpuSim* s_sim;
static mvfxMeshId s_mesh;
static const uint32_t s_gasRecord[MVFX_GPU_GAS_STATISTICS_WORDS];
static const uint32_t s_waterRecord[MVFX_GPU_WATER_STATISTICS_WORDS];

static mvfxResult SetEmitterMesh(mvfxContext* context)
{
    return mvfxGpuSim_SetEmitterMesh(s_sim, context, 0, s_mesh);
}

static mvfxResult ReportGas(mvfxContext* context)
{
    return mvfxGpuSim_ReportGas(s_sim, context, 0, s_gasRecord);
}

static mvfxResult ReportWater(mvfxContext* context)
{
    return mvfxGpuSim_ReportWater(s_sim, context, 0, s_waterRecord);
}

// Each call by its public name, which tools/check_host_calls.py reads.
static const struct
{
    Change call;
    const char* name;
} s_changes[] = {
    {SetEmitterMesh, "mvfxGpuSim_SetEmitterMesh"},
    {ReportGas, "mvfxGpuSim_ReportGas"},
    {ReportWater, "mvfxGpuSim_ReportWater"},
};

enum
{
    CHANGES = (int32_t)(sizeof s_changes / sizeof s_changes[0])
};

static mvfxContext* s_changing;
static mvfxResult s_changed[CHANGES];
static uint64_t s_changeMisuse[CHANGES];

static void ChangeBack(const mvfxModuleCall* call, void* user)
{
    (void)call;
    (void)user;
    mvfxContext* context = s_changing;
    if (context == NULL)
    {
        return;
    }
    s_changing = NULL;
    for (int32_t c = 0; c < CHANGES; ++c)
    {
        uint64_t before = mvfxContext_GetMisuse(context);
        s_changed[c] = s_changes[c].call(context);
        s_changeMisuse[c] = mvfxContext_GetMisuse(context) - before;
    }
}

static const mvfxHostModule s_changeBack = {.stage = mvfx_stageAfterMotion, .function = ChangeBack};

static void TestContextChangesRefused(mvfxGpu* kernels)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static const float corners[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = corners;
    meshDef.vertexCount = 4;
    CHECK(mvfxCreateMesh(context, &meshDef, &s_mesh) == mvfx_success, "mesh");
    // A planned instance spawning from the mesh, with a gas and a water
    // surface, and its simulation.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.shape.kind = mvfx_shapeMesh;
    emitter.shape.mesh = s_mesh;
    emitter.shape.meshMode = mvfx_meshVertices;
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 8;
    gas.size[1] = 8;
    gas.size[2] = 1;
    gas.cellSize = 0.1f;
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 8;
    water.size[1] = 8;
    water.cellSize = 0.1f;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    effectDef.gases = &gas;
    effectDef.gasCount = 1;
    effectDef.waters = &water;
    effectDef.waterCount = 1;
    mvfxEffectId planned;
    CHECK(mvfxCreateEffect(context, &effectDef, &planned, NULL) == mvfx_success,
          "a planned effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = planned;
    instanceDef.planned = true;
    mvfxInstanceId instance;
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = planned;
    simDef.instance = instance;
    mvfxGpuRefusal refusal;
    CHECK(mvfxCreateGpuSim(&simDef, &s_sim, &refusal) == mvfx_success, "simulation");
    // Another instance, stepped on the CPU, whose host module calls back in.
    mvfxEmitterDef hosted = mvfxDefaultEmitterDef();
    hosted.hostModules = &s_changeBack;
    hosted.hostModuleCount = 1;
    mvfxEffectDef hostedDef = mvfxDefaultEffectDef();
    hostedDef.emitters = &hosted;
    hostedDef.emitterCount = 1;
    mvfxEffectId stepped;
    CHECK(mvfxCreateEffect(context, &hostedDef, &stepped, NULL) == mvfx_success,
          "a stepped effect");
    instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = stepped;
    mvfxInstanceId steppedInstance;
    CHECK(mvfxCreateInstance(context, &instanceDef, &steppedInstance) == mvfx_success,
          "a stepped instance");
    for (int32_t c = 0; c < CHANGES; ++c)
    {
        s_changed[c] = mvfx_success;
        s_changeMisuse[c] = 0;
    }
    s_changing = context;
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
              s_changing == NULL,
          "the host module called back in");
    for (int32_t c = 0; c < CHANGES; ++c)
    {
        if (s_changed[c] != mvfx_errorBusy || s_changeMisuse[c] != 1)
        {
            printf("%s from a host module: %d, %llu misuse\n", s_changes[c].name, (int)s_changed[c],
                   (unsigned long long)s_changeMisuse[c]);
        }
        CHECK(s_changed[c] == mvfx_errorBusy && s_changeMisuse[c] == 1,
              "a change from a host module refused as busy and counted");
    }
    // Outside host code each is taken, so the refusals were the host code's.
    CHECK(SetEmitterMesh(context) == mvfx_success && ReportGas(context) == mvfx_success &&
              ReportWater(context) == mvfx_success,
          "the same calls taken outside host code");
    mvfxDestroyGpuSim(s_sim);
    s_sim = NULL;
    mvfxDestroyContext(context);
}

int main(void)
{
    Gpu gpu;
    CHECK(Open(&gpu), "a device");
    mvfxGpuDef def = mvfxDefaultGpuDef();
    def.device = gpu.device;
    mvfxGpu* kernels = NULL;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success && AwaitReady(&gpu, kernels), "kernels");
    TestCallsBackIn(&gpu, kernels);
    TestFailedMemory(kernels);
    TestConstCalls();
    TestContextChangesRefused(kernels);
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
