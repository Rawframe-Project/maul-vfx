// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part beside the CPU path: one fountain played by two instances
// of one context, one stepped on the CPU and one planned and simulated on
// a device on the first adapter through Maul RHI. Each second the host
// reads the device's statistics record back and compares its live count
// and bounds with the CPU instance's. It returns 0 when they agree, 77
// (skipped) when the adapter search finds none, and 1 on any failure,
// which says what failed. As the tests do, it fails rather than skips
// when MVFX_RHI_REQUIRED is set, and fails on an adapter of another
// driver than the one it names ("vulkan" or "d3d12").

// getenv is standard C; MSVC's runtime deprecates it for its own.
#define _CRT_SECURE_NO_WARNINGS

#include "maul-vfx-rhi/gpu.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Ten seconds, the most a frame is waited for once the kernels are ready.
// The first frame compiles every kernel it dispatches, which on a software
// driver with an empty shader cache takes minutes (lavapipe on a 4-core CI
// runner: three, or nine with LLVM's optimizations), so it is waited for
// fifteen.
#define WAIT_NS       UINT64_C(10000000000)
#define FIRST_WAIT_NS UINT64_C(900000000000)

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
    mvfxGpu* kernels;
    // How long the next frame is waited for.
    uint64_t waitNs;
} Gpu;

static uint32_t s_record[MVFX_GPU_STATISTICS_WORDS];

// Hands the device's notifications to the GPU object.
static void Drain(Gpu* gpu)
{
    mrhiDeviceNotification note;
    while (mrhiDevice_TakeNotification(gpu->device, &note) == mrhi_success)
    {
        (void)mvfxGpu_Notify(gpu->kernels, &note);
    }
}

static double Seconds(void)
{
    struct timespec now = {0};
    (void)timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// A device on the first adapter, software ones allowed, and the part's
// GPU object with its pipelines ready (a minute at most): 0, 77 when the
// search finds no adapter, or 1.
static int Open(Gpu* gpu)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    search.allowSoftware = true;
    mrhiRequestId request;
    mrhiInstanceNotification record = {0};
    mrhiAdapterId adapter;
    size_t count = 0;
    mrhiResult made = mrhiCreateInstance(&def, &gpu->instance);
    if (made != mrhi_success)
    {
        printf("the instance was not made: %s\n", mrhiGetResultName(made));
        return 1;
    }
    mrhiResult found = mrhiInstance_RequestAdapters(gpu->instance, &search, &request);
    found = found == mrhi_success ? mrhiInstance_TakeNotification(gpu->instance, &record) : found;
    found = found == mrhi_success ? record.outcome : found;
    found = found == mrhi_success ? mrhiInstance_GetAdapters(gpu->instance, &adapter, 1, &count)
                                  : found;
    if (found != mrhi_success)
    {
        printf("the adapter search failed: %s\n", mrhiGetResultName(found));
        return 1;
    }
    const char* required = getenv("MVFX_RHI_REQUIRED");
    if (count == 0)
    {
        printf(required != NULL ? "no adapter, and MVFX_RHI_REQUIRED is set\n"
                                : "no adapter: skipped\n");
        return required != NULL ? 1 : 77;
    }
    mrhiAdapterInfo info = {0};
    if (required != NULL &&
        (mrhiAdapter_GetInfo(gpu->instance, adapter, &info) != mrhi_success ||
         (strcmp(required, "vulkan") == 0 && info.driver != mrhi_driverVulkan) ||
         (strcmp(required, "d3d12") == 0 && info.driver != mrhi_driverD3d12)))
    {
        printf("the adapter is not the %s driver's, which MVFX_RHI_REQUIRED names\n", required);
        return 1;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    mrhiResult opened = mrhiCreateDevice(gpu->instance, &deviceDef, &gpu->device, &request);
    opened =
        opened == mrhi_success ? mrhiInstance_TakeNotification(gpu->instance, &record) : opened;
    opened = opened == mrhi_success ? record.outcome : opened;
    if (opened != mrhi_success)
    {
        printf("the adapter's device was not made: %s\n", mrhiGetResultName(opened));
        return 1;
    }
    mvfxGpuDef gpuDef = mvfxDefaultGpuDef();
    gpuDef.device = gpu->device;
    mvfxResult kernels = mvfxCreateGpu(&gpuDef, &gpu->kernels);
    if (kernels != mvfx_success)
    {
        printf("the GPU object was not made: %s\n", mvfxGetResultName(kernels));
        return 1;
    }
    double deadline = Seconds() + 60.0;
    while (!mvfxGpu_IsReady(gpu->kernels) && Seconds() < deadline)
    {
        Drain(gpu);
    }
    if (!mvfxGpu_IsReady(gpu->kernels))
    {
        printf("the GPU object's pipelines were not ready in a minute\n");
        return 1;
    }
    return 0;
}

static void Close(Gpu* gpu)
{
    mvfxDestroyGpu(gpu->kernels);
    if (gpu->device != NULL)
    {
        mrhiDestroyDevice(gpu->device);
    }
    if (gpu->instance != NULL)
    {
        mrhiDestroyInstance(gpu->instance);
    }
}

// Submits the frame recorded and waits for it.
static bool Finish(Gpu* gpu)
{
    mrhiRequestId submitted = {0};
    mrhiResult waited = mrhiDevice_SubmitFrame(gpu->device, &submitted);
    waited = waited == mrhi_success ? mrhiRequest_WaitFrame(gpu->device, submitted, gpu->waitNs)
                                    : waited;
    if (waited == mrhi_timeout)
    {
        printf(gpu->waitNs == FIRST_WAIT_NS
                   ? "the first frame did not finish in 15 minutes: its kernels compile then, "
                     "and a driver without a warm shader cache may take minutes\n"
                   : "a frame did not finish in 10 seconds\n");
    }
    gpu->waitNs = WAIT_NS;
    Drain(gpu);
    return waited == mrhi_success;
}

// A frame of the simulation's passes.
static bool Simulate(Gpu* gpu, mvfxGpuSim* sim)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    if (mrhiDevice_BeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    if (mvfxGpuSim_AddPasses(sim) != mvfx_success ||
        mrhiDevice_CompileFrame(gpu->device) != mrhi_success ||
        mvfxGpuSim_Record(sim) != mvfx_success)
    {
        (void)mrhiDevice_DropFrame(gpu->device);
        return false;
    }
    return Finish(gpu);
}

// Reads the emitter's statistics record back in a frame of its own.
static bool ReadRecord(Gpu* gpu, const mvfxGpuSim* sim)
{
    mvfxGpuParticles particles;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    if (mvfxGpuSim_GetParticles(sim, 0, &particles) != mvfx_success ||
        mrhiDevice_BeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    mrhiResourceId statistics = {0};
    mrhiPassId pass = {0};
    mrhiRequestId read = {0};
    bool recorded =
        mrhiImportBuffer(gpu->device, particles.statistics, &statistics) == mrhi_success;
    const mrhiAccess reading = {.resource = statistics, .kind = mrhi_accessCopySource};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = &reading;
    def.accessCount = 1;
    def.neverCull = true;
    recorded = recorded && mrhiAddPass(gpu->device, &def, &pass) == mrhi_success &&
               mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
               mrhiPass_Begin(gpu->device, pass) == mrhi_success &&
               mrhiPass_ReadBuffer(gpu->device, pass, statistics, 0, sizeof s_record, &read) ==
                   mrhi_success &&
               mrhiPass_End(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDevice_DropFrame(gpu->device);
        return false;
    }
    size_t taken = 0;
    return Finish(gpu) &&
           mrhiRequest_TakeReadback(gpu->device, read, s_record, sizeof s_record, &taken) ==
               mrhi_success &&
           taken == sizeof s_record;
}

static mvfxEffectId MakeFountain(mvfxContext* context)
{
    static const mvfxCollider ground = {.kind = mvfx_colliderPlane, .normal = {0.0f, 1.0f, 0.0f}};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 1024;
    emitter.rate = 200.0f;
    emitter.lifetime = mvfxRangeSource(2.0f, 2.5f);
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                      .direction = {0.0f, 1.0f, 0.0f},
                                      .coneAngle = 0.15f,
                                      .speed = {5.0f, 6.0f}};
    emitter.motion.gravity[1] = -9.8f;
    emitter.colliders = &ground;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enable = true, .restitution = 0.3f, .friction = 0.4f};
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(context, &effectDef, &effect) != mvfx_success)
    {
        printf("the effect was refused\n");
    }
    return effect;
}

// The largest gap between two boxes' corners.
static double Gap(const mvfxBounds* a, const mvfxBounds* b)
{
    double gap = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        gap = fmax(gap, fmax(fabs(a->min[k] - b->min[k]), fabs(a->max[k] - b->max[k])));
    }
    return gap;
}

static bool Play(Gpu* gpu)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = MakeFountain(context);
    // One seed for both, so they draw the same particles (0 would seed each
    // by its order of creation).
    instanceDef.seed = 11;
    mvfxInstanceId cpu = {0};
    mvfxInstanceId planned = {0};
    bool ok = mvfxCreateInstance(context, &instanceDef, &cpu) == mvfx_success;
    instanceDef.planned = true;
    ok = ok && mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success;
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = gpu->kernels;
    simDef.context = context;
    simDef.effect = instanceDef.effect;
    simDef.instance = planned;
    mvfxGpuSim* sim = NULL;
    mvfxGpuRefusal refusal;
    ok = ok && mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success;
    // Three seconds at 60 frames a second: the context steps both
    // instances, then the device runs the planned one's step.
    for (int32_t frame = 1; ok && frame <= 3 * 60; ++frame)
    {
        ok = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
             Simulate(gpu, sim);
        if (ok && frame % 60 == 0)
        {
            mvfxEmitterStats stats;
            mvfxBounds onCpu;
            mvfxBounds onGpu;
            ok = ReadRecord(gpu, sim) &&
                 mvfxInstance_GetEmitterStats(context, cpu, 0, &stats) == mvfx_success &&
                 mvfxInstance_GetBounds(context, cpu, 0, &onCpu) == mvfx_success &&
                 mvfxGpuSim_GetBounds(sim, 0, s_record, &onGpu) == mvfx_success;
            double gap = ok ? Gap(&onCpu, &onGpu) : (double)INFINITY;
            printf("%d s: %d live on the CPU, %u on the device; their bounds %.2g apart, up to "
                   "%.2f\n",
                   frame / 60, stats.live, s_record[0], gap, onGpu.max[1]);
            ok = ok && s_record[0] == (uint32_t)stats.live && gap < 1e-3;
        }
    }
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
    return ok;
}

int main(void)
{
    Gpu gpu = {.waitNs = FIRST_WAIT_NS};
    int opened = Open(&gpu);
    if (opened != 0)
    {
        Close(&gpu);
        return opened;
    }
    bool ok = Play(&gpu);
    Close(&gpu);
    printf(ok ? "the device plays the fountain as the CPU does\n" : "the GPU sample failed\n");
    return ok ? 0 : 1;
}
