// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host code calling back into a simulation of the GPU part, on Maul RHI's
// test driver: from its allocator, while mvfxDestroyGpuSim releases the
// host's particles' memory, each call that changes the simulation is
// refused with mvfx_errorBusy and changes nothing, a second destruction
// does nothing, a reading call still answers, and the destruction then
// gives back all the memory taken. And its creation failing at each of
// its allocations in turn, for an effect with a liquid gas (whose split
// steps keep lanes): refused, all it took given back.

#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/test.h"
#include "maul-vfx-rhi/gpu.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

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
    bool refused = mvfxGpuSim_UploadBytes(sim, &before) == mvfx_success && before > 0 &&
                   mvfxGpuSim_SetDepthTextures(sim, (mrhiResourceId){0}, (mrhiResourceId){0}) ==
                       mvfx_errorBusy &&
                   mvfxGpuSim_AddPasses(sim) == mvfx_errorBusy &&
                   mvfxGpuSim_AddRecords(sim, &records) == mvfx_errorBusy &&
                   mvfxGpuSim_Record(sim) == mvfx_errorBusy;
    mvfxDestroyGpuSim(sim);
    s_refused = refused && mvfxGpuSim_UploadBytes(sim, &after) == mvfx_success && after == before;
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
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
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
    emitter.liquid.enabled = true;
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
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "a liquid gas's effect");
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
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
