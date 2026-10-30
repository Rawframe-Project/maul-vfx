// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part's blocks of docs/user/, each in a function as it stands
// there, run on a device on the first adapter (lavapipe on CI); skipped
// (77) without one unless MVFX_RHI_REQUIRED is set, whose driver the
// adapter must be.

// getenv is standard C; MSVC's runtime deprecates it for its own.
#define _CRT_SECURE_NO_WARNINGS

#include "rhi_driver.h"
#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"
#include "maul-vfx-rhi/gpu.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

// A device on the first adapter, software ones allowed: OpenDevice's
// result.
static int Open(Gpu* gpu)
{
    *gpu = (Gpu){0};
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    mrhiAdapterId adapter;
    int opened = OpenDevice(def, &gpu->instance, &adapter, &gpu->device);
    return opened;
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

static double Seconds(void)
{
    struct timespec now = {0};
    // hygiene: bounds waits on the device, never a result
    (void)timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// Polls the device, a minute at most, for the object's pipelines.
static bool AwaitReady(Gpu* gpu, mvfxGpu* kernels)
{
    double deadline = Seconds() + 60.0;
    while (!mvfxGpu_IsReady(kernels) && Seconds() < deadline)
    {
        mrhiDeviceNotification record;
        while (mrhiDevice_TakeNotification(gpu->device, &record) == mrhi_success)
        {
            (void)mvfxGpu_Notify(kernels, &record);
        }
    }
    return mvfxGpu_IsReady(kernels);
}

// The GPU part: the first section's fountain on the device.
static bool GpuFountain(mrhiDevice* device, mvfxGpu* kernels)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return false;
    }
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 128;
    emitter.rate = 50.0f;
    emitter.lifetime = mvfxRangeSource(2.0f, 2.0f);
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                      .direction = {0.0f, 1.0f, 0.0f},
                                      .coneAngle = 0.2f,
                                      .speed = {3.0f, 4.0f}};
    emitter.motion.gravity[1] = -9.8f;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    mvfxInstanceId instance = {0};
    bool made = mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success;
    // Planned: the context plans its steps, the simulation runs them.
    instanceDef.effect = effect;
    instanceDef.planned = true;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = instance;
    mvfxGpuSim* sim = NULL;
    mvfxGpuRefusal refusal;
    made = made && mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success;

    // A second of frames: the context steps, the simulation adds its passes
    // to the frame and records them once it is compiled.
    for (int32_t step = 0; made && step < 60; ++step)
    {
        mrhiFrameDef frame = mrhiDefaultFrameDef();
        mrhiRequestId submitted = {0};
        made = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success &&
               mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
               mvfxGpuSim_AddPasses(sim) == mvfx_success &&
               mrhiDevice_CompileFrame(device) == mrhi_success &&
               mvfxGpuSim_Record(sim) == mvfx_success &&
               mrhiDevice_SubmitFrame(device, &submitted) == mrhi_success &&
               mrhiRequest_WaitFrame(device, submitted, UINT64_C(10000000000)) == mrhi_success;
        if (!made)
        {
            (void)mrhiDevice_DropFrame(device); // a frame left open by a failure
        }
        mrhiDeviceNotification note;
        while (mrhiDevice_TakeNotification(device, &note) == mrhi_success)
        {
            (void)mvfxGpu_Notify(kernels, &note);
        }
    }

    // The statistics record read back in a frame of its own (a host reads it
    // in a later frame's pass, as late as its frames lag).
    mvfxGpuParticles particles;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId statistics = {0};
    mrhiPassId pass = {0};
    mrhiRequestId read = {0};
    mrhiRequestId submitted = {0};
    made = made && mvfxGpuSim_GetParticles(sim, 0, &particles) == mvfx_success &&
           mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
           mrhiImportBuffer(device, particles.statistics, &statistics) == mrhi_success;
    const mrhiAccess reading = {.resource = statistics, .kind = mrhi_accessCopySource};
    mrhiPassDef passDef = mrhiDefaultPassDef();
    passDef.passClass = mrhi_passTransfer;
    passDef.accesses = &reading;
    passDef.accessCount = 1;
    passDef.neverCull = true;
    static uint32_t record[MVFX_GPU_STATISTICS_WORDS];
    made = made && mrhiAddPass(device, &passDef, &pass) == mrhi_success &&
           mrhiDevice_CompileFrame(device) == mrhi_success &&
           mrhiPass_Begin(device, pass) == mrhi_success &&
           mrhiPass_ReadBuffer(device, pass, statistics, 0, sizeof record, &read) == mrhi_success &&
           mrhiPass_End(device, pass) == mrhi_success &&
           mrhiDevice_SubmitFrame(device, &submitted) == mrhi_success &&
           mrhiRequest_WaitFrame(device, submitted, UINT64_C(10000000000)) == mrhi_success;
    if (!made)
    {
        (void)mrhiDevice_DropFrame(device);
    }
    mrhiDeviceNotification note;
    while (mrhiDevice_TakeNotification(device, &note) == mrhi_success)
    {
        (void)mvfxGpu_Notify(kernels, &note);
    }
    size_t taken = 0;
    made = made &&
           mrhiRequest_TakeReadback(device, read, record, sizeof record, &taken) == mrhi_success;

    // About 50 live, as on the CPU, and their bounds above the ground.
    mvfxBounds bounds;
    made = made && record[0] >= 45 && record[0] <= 55 &&
           mvfxGpuSim_GetBounds(sim, 0, record, &bounds) == mvfx_success && bounds.max[1] > 1.0;
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
    return made;
}

int main(void)
{
    Gpu gpu;
    int opened = Open(&gpu);
    if (opened != 0)
    {
        Close(&gpu);
        return opened;
    }
    mvfxGpuDef def = mvfxDefaultGpuDef();
    def.device = gpu.device;
    mvfxGpu* kernels = NULL;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success && AwaitReady(&gpu, kernels), "kernels");
    CHECK(kernels != NULL && GpuFountain(gpu.device, kernels), "a fountain on the device");
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
