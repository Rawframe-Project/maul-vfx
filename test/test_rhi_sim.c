// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A planned instance simulated on a real device (lavapipe on CI) beside the
// same effect on the CPU path, compared after every step: the live count,
// each particle's serial and its birth and death ticks equal, in the same
// order; its place and velocity within a tolerance (the GPU's floats are
// not the CPU's bits). Skips (77) without an adapter, unless
// MVFX_RHI_REQUIRED is set.

#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CAPACITY   1024
#define STEPS      40
#define STEP_TICKS (MVFX_TICKS_PER_SECOND / 60)
#define WAIT_NS    10000000000ull

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

static bool Open(Gpu* gpu)
{
    *gpu = (Gpu){0};
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
    return mrhiCreateDevice(gpu->instance, &deviceDef, &gpu->device, &request) == mrhi_success &&
           mrhiNextInstanceNotification(gpu->instance, &record) == mrhi_success &&
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

static double Seconds(void)
{
    struct timespec now = {0};
    (void)timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// Polls the device, ten seconds at most, for the object's pipelines.
static bool AwaitReady(Gpu* gpu, mvfxGpu* kernels)
{
    double deadline = Seconds() + 10.0;
    while (!mvfxGpu_IsReady(kernels) && Seconds() < deadline)
    {
        mrhiDeviceNotification record;
        while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
        {
            (void)mvfxGpu_Notify(kernels, &record);
        }
    }
    return mvfxGpu_IsReady(kernels);
}

// Submits the frame recorded and waits for it, its notifications drained.
static bool Finish(Gpu* gpu)
{
    mrhiRequestId token = {0};
    bool done = mrhiSubmitFrame(gpu->device, &token) == mrhi_success &&
                mrhiWaitFrame(gpu->device, token, WAIT_NS) == mrhi_success;
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
    {
    }
    return done;
}

// One frame of the simulation's step.
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

// The emitter's particles and counts as the GPU holds them, read back.
static uint32_t s_words[MVFX_GPU_PARTICLE_WORDS * CAPACITY];
static uint32_t s_counts[4];

static bool ReadBack(Gpu* gpu, const mvfxGpuSim* sim)
{
    mvfxGpuParticles particles;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId buffers[2] = {{0}, {0}};
    mrhiPassId pass = {0};
    mrhiRequestId reads[2] = {{0}, {0}};
    if (mvfxGpuSim_GetParticles(sim, 0, &particles) != mvfx_success ||
        mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    bool recorded =
        mrhiImportBuffer(gpu->device, particles.particles, &buffers[0]) == mrhi_success &&
        mrhiImportBuffer(gpu->device, particles.counts, &buffers[1]) == mrhi_success;
    const mrhiAccess reading[2] = {{.resource = buffers[0], .kind = mrhi_accessCopySource},
                                   {.resource = buffers[1], .kind = mrhi_accessCopySource}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = reading;
    def.accessCount = 2;
    def.neverCull = true;
    recorded = recorded && mrhiAddPass(gpu->device, &def, &pass) == mrhi_success &&
               mrhiCompileFrame(gpu->device) == mrhi_success &&
               mrhiBeginPass(gpu->device, pass) == mrhi_success &&
               mrhiReadBuffer(gpu->device, pass, buffers[0], 0, sizeof s_words, &reads[0]) ==
                   mrhi_success &&
               mrhiReadBuffer(gpu->device, pass, buffers[1], 0, sizeof s_counts, &reads[1]) ==
                   mrhi_success &&
               mrhiEndPass(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    size_t taken = 0;
    size_t counted = 0;
    return Finish(gpu) &&
           mrhiTakeReadback(gpu->device, reads[0], s_words, sizeof s_words, &taken) ==
               mrhi_success &&
           mrhiTakeReadback(gpu->device, reads[1], s_counts, sizeof s_counts, &counted) ==
               mrhi_success &&
           taken == sizeof s_words && counted == sizeof s_counts;
}

// A word of particle i in the read-back arrays.
static uint32_t Word(int32_t array, int32_t i)
{
    return s_words[array * CAPACITY + i];
}

static float FloatOf(int32_t array, int32_t i)
{
    float f;
    uint32_t w = Word(array, i);
    memcpy(&f, &w, sizeof f);
    return f;
}

// The largest differences seen, place and velocity, over the run.
static float s_placeError;
static float s_velocityError;

// Whether the GPU's particles are the CPU path's: counts, serials and ticks
// equal, in order, places and velocities within the tolerance.
static bool SameAsCpu(const mvfxInstance* cpu)
{
    const mvfxEmitterState* state = &cpu->emitters[0];
    if ((int32_t)s_counts[0] != state->count || s_counts[2] != 0)
    {
        printf("counts: GPU %u (refused %u), CPU %d\n", s_counts[0], s_counts[2], state->count);
        return false;
    }
    const uint32_t* low = mvfxAttributeArray(cpu, 0, mvfx_attributeBirthLow);
    const uint32_t* high = mvfxAttributeArray(cpu, 0, mvfx_attributeBirthHigh);
    const uint32_t* serial = mvfxAttributeArray(cpu, 0, mvfx_attributeSerial);
    const float* inverseLife = mvfxAttributeArray(cpu, 0, mvfx_attributeInverseLife);
    static const mvfxAttribute places[3] = {mvfx_attributePositionX, mvfx_attributePositionY,
                                            mvfx_attributePositionZ};
    static const mvfxAttribute velocities[3] = {mvfx_attributeVelocityX, mvfx_attributeVelocityY,
                                                mvfx_attributeVelocityZ};
    bool same = true;
    for (int32_t i = 0; same && i < state->count; ++i)
    {
        uint32_t lifeBits;
        memcpy(&lifeBits, &inverseLife[i], sizeof lifeBits);
        same = Word(6, i) == serial[i] && Word(8, i) == low[i] && Word(9, i) == high[i] &&
               Word(12, i) == lifeBits;
        for (int32_t k = 0; k < 3; ++k)
        {
            const float* p = mvfxAttributeArray(cpu, 0, places[k]);
            const float* v = mvfxAttributeArray(cpu, 0, velocities[k]);
            float dp = fabsf(FloatOf(k, i) - p[i]);
            float dv = fabsf(FloatOf(3 + k, i) - v[i]);
            s_placeError = dp > s_placeError ? dp : s_placeError;
            s_velocityError = dv > s_velocityError ? dv : s_velocityError;
            same = same && dp <= 1e-3f && dv <= 1e-3f;
        }
        if (!same)
        {
            printf("particle %d of serial %u differs\n", i, serial[i]);
        }
    }
    return same;
}

static const mvfxBurst s_bursts[1] = {
    {.time = 0.1f, .minCount = 20, .maxCount = 20, .cycles = 1, .probability = 1.0f}};

static void TestSprite(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = CAPACITY;
    emitter.rate = 600.0f;
    emitter.bursts = s_bursts;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(0.2f, 0.4f);
    emitter.shape = (mvfxShape){.kind = mvfx_shapeSphere, .rotation = {0, 0, 0, 1}, .radius = 0.5f};
    emitter.velocity = (mvfxVelocity){
        .mode = mvfx_directionCone, .direction = {0, 1, 0}, .coneAngle = 0.4f, .speed = {2, 4}};
    emitter.motion.gravity[1] = -9.8f;
    emitter.motion.drag = 0.5f;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.seed = 11;
    mvfxInstanceId cpu = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &cpu) == mvfx_success, "a CPU instance");
    instanceDef.planned = true;
    mvfxInstanceId planned = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success, "planned");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = planned;
    mvfxGpuSim* sim = NULL;
    mvfxGpuRefusal refusal;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success && refusal.emitter == -1,
          "the simulation");
    bool same = true;
    int32_t most = 0;
    for (int32_t s = 0; sim != NULL && same && s < STEPS; ++s)
    {
        CHECK(mvfxContext_StepTicks(context, STEP_TICKS) == mvfx_success, "stepped");
        same = Simulate(gpu, sim) && ReadBack(gpu, sim) &&
               SameAsCpu(mvfxContextInstance(context, cpu));
        most = (int32_t)s_counts[0] > most ? (int32_t)s_counts[0] : most;
    }
    printf("largest differences: place %g, velocity %g\n", (double)s_placeError,
           (double)s_velocityError);
    CHECK(same && most > 100, "the CPU path's particles on the GPU, every step");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
}

int main(void)
{
    Gpu gpu;
    if (!Open(&gpu))
    {
        Close(&gpu);
        if (getenv("MVFX_RHI_REQUIRED") != NULL)
        {
            printf("FAIL: no adapter, and MVFX_RHI_REQUIRED is set\n");
            return 1;
        }
        printf("SKIP: no adapter\n");
        return 77;
    }
    mvfxGpuDef def = mvfxDefaultGpuDef();
    def.device = gpu.device;
    mvfxGpu* kernels = NULL;
    CHECK(mvfxCreateGpu(&def, &kernels) == mvfx_success && AwaitReady(&gpu, kernels), "kernels");
    TestSprite(&gpu, kernels);
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
