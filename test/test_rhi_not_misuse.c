// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part's calls that take a context, given a NULL one with a live
// mesh and a live simulation on Maul RHI's test driver: each refuses it
// with mvfx_errorInvalid, never mvfx_errorStale, clears its output and
// counts no misuse on the live context.

#include "test_harness.h"

#include "maul-rhi/instance.h"
#include "maul-rhi/test.h"
#include "maul-vfx-rhi/gpu.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/mesh.h"

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

static const float s_tetrahedron[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};

static void TestNullContext(mvfxGpu* kernels)
{
    // A planned instance of an emitter spawning from a mesh, a gas and a
    // water surface, and its simulation.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = s_tetrahedron;
    meshDef.vertexCount = 4;
    mvfxMeshId mesh = {0};
    CHECK(mvfxCreateMesh(context, &meshDef, &mesh) == mvfx_success, "mesh");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.shape.kind = mvfx_shapeMesh;
    emitter.shape.mesh = mesh;
    emitter.shape.meshMode = mvfx_meshVertices;
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 8;
    gas.size[1] = 8;
    gas.size[2] = 8;
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
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.planned = true;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = instance;
    mvfxGpuSim* sim = NULL;
    mvfxGpuRefusal refusal;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success, "simulation");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success, "stepped");
    uint64_t misuse = mvfxContext_GetMisuse(context);

    int64_t bytes = 7;
    CHECK(mvfxContext_GetGpuHostGridBytes(NULL, NULL, 0, &bytes) == mvfx_errorInvalid && bytes == 0,
          "mvfxContext_GetGpuHostGridBytes");
    bytes = 7;
    CHECK(mvfxMesh_GetGpuBytes(NULL, mesh, mvfx_meshVertices, &bytes) == mvfx_errorInvalid &&
              bytes == 0,
          "mvfxMesh_GetGpuBytes");
    CHECK(mvfxGpuSim_SetEmitterMesh(sim, NULL, 0, mesh) == mvfx_errorInvalid,
          "mvfxGpuSim_SetEmitterMesh");
    const uint32_t gasRecord[MVFX_GPU_GAS_STATISTICS_WORDS] = {0};
    CHECK(mvfxGpuSim_ReportGas(sim, NULL, 0, gasRecord) == mvfx_errorInvalid,
          "mvfxGpuSim_ReportGas");
    const uint32_t waterRecord[MVFX_GPU_WATER_STATISTICS_WORDS] = {0};
    CHECK(mvfxGpuSim_ReportWater(sim, NULL, 0, waterRecord) == mvfx_errorInvalid,
          "mvfxGpuSim_ReportWater");
    CHECK(mvfxContext_GetMisuse(context) == misuse, "no misuse counted on the live context");

    // With the context, each answers, so the refusals above were the NULL
    // context's.
    bytes = 0;
    CHECK(mvfxMesh_GetGpuBytes(context, mesh, mvfx_meshVertices, &bytes) == mvfx_success &&
              bytes > 0,
          "the mesh's bytes with the context");
    CHECK(mvfxGpuSim_SetEmitterMesh(sim, context, 0, mesh) == mvfx_success,
          "the mesh bound with the context");
    mvfxDestroyGpuSim(sim);
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
    TestNullContext(kernels);
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
