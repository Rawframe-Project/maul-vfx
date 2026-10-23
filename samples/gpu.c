// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part beside the CPU path: one fountain played by two instances
// of one context, one stepped on the CPU and one planned and simulated on
// a device on the first adapter through Maul RHI. Each second the host
// reads the device's statistics record back and compares its live count
// and bounds with the CPU instance's. It returns 0 when they agree, and
// 77 (skipped) when there is no adapter.

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
    while (mrhiNextDeviceNotification(gpu->device, &note) == mrhi_success)
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
// GPU object with its pipelines ready (a minute at most).
static bool Open(Gpu* gpu)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    search.allowSoftware = true;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    if (mrhiCreateInstance(&def, &gpu->instance) != mrhi_success ||
        mrhiRequestAdapters(gpu->instance, &search, &request) != mrhi_success ||
        mrhiNextInstanceNotification(gpu->instance, &record) != mrhi_success ||
        record.outcome != mrhi_success ||
        mrhiGetAdapters(gpu->instance, &adapter, 1, &count) != mrhi_success || count == 0)
    {
        return false;
    }
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    mvfxGpuDef gpuDef = mvfxDefaultGpuDef();
    if (mrhiCreateDevice(gpu->instance, &deviceDef, &gpu->device, &request) != mrhi_success ||
        mrhiNextInstanceNotification(gpu->instance, &record) != mrhi_success ||
        record.outcome != mrhi_success)
    {
        return false;
    }
    gpuDef.device = gpu->device;
    if (mvfxCreateGpu(&gpuDef, &gpu->kernels) != mvfx_success)
    {
        return false;
    }
    double deadline = Seconds() + 60.0;
    while (!mvfxGpu_IsReady(gpu->kernels) && Seconds() < deadline)
    {
        Drain(gpu);
    }
    return mvfxGpu_IsReady(gpu->kernels);
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
    mrhiResult waited = mrhiSubmitFrame(gpu->device, &submitted);
    waited = waited == mrhi_success ? mrhiWaitFrame(gpu->device, submitted, gpu->waitNs) : waited;
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
    if (mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    if (mvfxGpuSim_AddPasses(sim) != mvfx_success ||
        mrhiCompileFrame(gpu->device) != mrhi_success || mvfxGpuSim_Record(sim) != mvfx_success)
    {
        (void)mrhiDropFrame(gpu->device);
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
        mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
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
    recorded =
        recorded && mrhiAddPass(gpu->device, &def, &pass) == mrhi_success &&
        mrhiCompileFrame(gpu->device) == mrhi_success &&
        mrhiBeginPass(gpu->device, pass) == mrhi_success &&
        mrhiReadBuffer(gpu->device, pass, statistics, 0, sizeof s_record, &read) == mrhi_success &&
        mrhiEndPass(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    size_t taken = 0;
    return Finish(gpu) &&
           mrhiTakeReadback(gpu->device, read, s_record, sizeof s_record, &taken) == mrhi_success &&
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
    emitter.collision = (mvfxCollision){.enabled = true, .restitution = 0.3f, .friction = 0.4f};
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
                 mvfxGpuSim_BoundsOf(sim, 0, s_record, &onGpu) == mvfx_success;
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
    if (!Open(&gpu))
    {
        Close(&gpu);
        printf("no adapter: skipped\n");
        return 77;
    }
    bool ok = Play(&gpu);
    Close(&gpu);
    printf(ok ? "the device plays the fountain as the CPU does\n" : "the GPU sample failed\n");
    return ok ? 0 : 1;
}
