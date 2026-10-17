// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Planned instances simulated on a real device (lavapipe on CI) beside the
// same effects on the CPU path, compared after every step (one emitter;
// two, under a moving, turning instance, cleared at half time; one full
// every step): the live count,
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

static int32_t s_capacity;

static bool ReadBack(Gpu* gpu, const mvfxGpuSim* sim, int32_t emitter)
{
    mvfxGpuParticles particles;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId buffers[2] = {{0}, {0}};
    mrhiPassId pass = {0};
    mrhiRequestId reads[2] = {{0}, {0}};
    if (mvfxGpuSim_GetParticles(sim, emitter, &particles) != mvfx_success ||
        mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    s_capacity = particles.capacity;
    uint64_t bytes = (uint64_t)s_capacity * MVFX_GPU_PARTICLE_WORDS * sizeof(uint32_t);
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
               mrhiReadBuffer(gpu->device, pass, buffers[0], 0, bytes, &reads[0]) == mrhi_success &&
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
           taken == bytes &&
           mrhiTakeReadback(gpu->device, reads[1], s_counts, sizeof s_counts, &counted) ==
               mrhi_success &&
           counted == sizeof s_counts;
}

// A word of particle i in the read-back arrays.
static uint32_t Word(int32_t array, int32_t i)
{
    return s_words[array * s_capacity + i];
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
static bool SameAsCpu(const mvfxInstance* cpu, int32_t e)
{
    const mvfxEmitterState* state = &cpu->emitters[e];
    if ((int32_t)s_counts[0] != state->count || s_counts[2] != 0)
    {
        printf("counts: GPU %u (refused %u), CPU %d\n", s_counts[0], s_counts[2], state->count);
        return false;
    }
    const uint32_t* low = mvfxAttributeArray(cpu, e, mvfx_attributeBirthLow);
    const uint32_t* high = mvfxAttributeArray(cpu, e, mvfx_attributeBirthHigh);
    const uint32_t* serial = mvfxAttributeArray(cpu, e, mvfx_attributeSerial);
    const float* inverseLife = mvfxAttributeArray(cpu, e, mvfx_attributeInverseLife);
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
            const float* p = mvfxAttributeArray(cpu, e, places[k]);
            const float* v = mvfxAttributeArray(cpu, e, velocities[k]);
            // Relative to the size, as places reach the rebase distance.
            float dp = fabsf(FloatOf(k, i) - p[i]) / (1.0f + fabsf(p[i]));
            float dv = fabsf(FloatOf(3 + k, i) - v[i]) / (1.0f + fabsf(v[i]));
            s_placeError = dp > s_placeError ? dp : s_placeError;
            s_velocityError = dv > s_velocityError ? dv : s_velocityError;
            same = same && dp <= 1e-4f && dv <= 1e-4f;
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

// A sprite emitter: a sphere's shell, cone velocities, gravity and drag,
// a burst; in the world.
static mvfxEmitterDef Sprite(void)
{
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
    return emitter;
}

// The ticks a step of the run takes.
static int64_t s_stepTicks = STEP_TICKS;

// A run of an effect: a CPU instance and a planned one side by side, the
// GPU's particles compared with the CPU path's after every step. The
// instance moves and turns when moving, and is cleared and restarted at
// half time when clearing.
typedef struct Run
{
    const mvfxEmitterDef* emitters;
    int32_t emitterCount;
    bool moving;
    bool clearing;
    // Its steps, STEPS for 0; the most the first emitter must hold at
    // some step, 100 for 0.
    int32_t steps;
    int32_t least;
} Run;

static mvfxContext* s_context;
static mvfxInstanceId s_cpu;
static mvfxInstanceId s_planned;

// Moves both instances to step s's place: along a line past the rebase
// distance, turning about a tilted axis.
static void Move(int32_t s)
{
    const double place[3] = {90.0 * s, 3.0 * s, -40.0 * s};
    float angle = 0.03f * (float)s;
    const float q[4] = {0.6f * sinf(angle), 0.8f * sinf(angle), 0.0f, cosf(angle)};
    float scale = 1.0f + 0.01f * (float)s;
    CHECK(mvfxInstance_SetTransform(s_context, s_cpu, place, q, scale, false) == mvfx_success &&
              mvfxInstance_SetTransform(s_context, s_planned, place, q, scale, false) ==
                  mvfx_success,
          "moved");
}

// Clears and restarts both instances.
static void Clear(void)
{
    mvfxInstanceId both[2] = {s_cpu, s_planned};
    for (int32_t i = 0; i < 2; ++i)
    {
        CHECK(mvfxInstance_Stop(s_context, both[i], mvfx_stopAndClear) == mvfx_success &&
                  mvfxInstance_Restart(s_context, both[i]) == mvfx_success,
              "cleared");
    }
}

static mvfxGpuSim* OpenRun(mvfxGpu* kernels, const Run* run, mvfxEffectId* effectOut)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = run->emitters;
    effectDef.emitterCount = run->emitterCount;
    CHECK(mvfxCreateEffect(s_context, &effectDef, effectOut) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = *effectOut;
    instanceDef.seed = 11;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_cpu) == mvfx_success, "a CPU instance");
    instanceDef.planned = true;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_planned) == mvfx_success, "planned");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = s_context;
    simDef.effect = *effectOut;
    simDef.instance = s_planned;
    mvfxGpuSim* sim = NULL;
    mvfxGpuRefusal refusal;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_success && refusal.emitter == -1,
          "the simulation");
    return sim;
}

static void Compare(Gpu* gpu, mvfxGpu* kernels, const Run* run, const char* what)
{
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(kernels, run, &effect);
    s_placeError = 0.0f;
    s_velocityError = 0.0f;
    bool same = sim != NULL;
    int32_t most[2] = {0, 0};
    int32_t steps = run->steps > 0 ? run->steps : STEPS;
    for (int32_t s = 0; same && s < steps; ++s)
    {
        if (run->moving)
        {
            Move(s);
        }
        if (run->clearing && s == STEPS / 2)
        {
            Clear();
        }
        CHECK(mvfxContext_StepTicks(s_context, s_stepTicks) == mvfx_success, "stepped");
        same = Simulate(gpu, sim);
        for (int32_t e = 0; same && e < run->emitterCount; ++e)
        {
            same = ReadBack(gpu, sim, e) && SameAsCpu(mvfxContextInstance(s_context, s_cpu), e);
            most[e] = (int32_t)s_counts[0] > most[e] ? (int32_t)s_counts[0] : most[e];
        }
    }
    printf("%s: largest relative differences, place %g, velocity %g\n", what, (double)s_placeError,
           (double)s_velocityError);
    CHECK(same && most[0] >= (run->least > 0 ? run->least : 100) &&
              (run->emitterCount < 2 || most[1] > 64),
          what);
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(s_context);
}

static void TestSprite(Gpu* gpu, mvfxGpu* kernels)
{
    const mvfxEmitterDef sprite = Sprite();
    Compare(gpu, kernels, &(Run){.emitters = &sprite, .emitterCount = 1},
            "the CPU path's particles on the GPU, every step");
}

static void TestMoving(Gpu* gpu, mvfxGpu* kernels)
{
    // A world-space shell with a share of the instance's velocity and a
    // strong drag, and a local-space emitter of 100 with world gravity and
    // shape-normal velocities, under an instance moving past the rebase
    // distance, turning and growing; cleared at half time. The second
    // holds more than 64, the update's groups rounded up.
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    emitters[0].shape.thickness = 0.4f;
    emitters[0].inheritVelocity = 0.5f;
    emitters[0].motion.drag = 30.0f;
    emitters[1].capacity = 100;
    emitters[1].rate = 320.0f;
    emitters[1].lifetime = mvfxRangeSource(0.2f, 0.3f);
    emitters[1].space = mvfx_spaceLocal;
    emitters[1].motion.gravitySpace = mvfx_spaceWorld;
    emitters[1].velocity = (mvfxVelocity){.mode = mvfx_directionShapeNormal, .speed = {1, 2}};
    emitters[1].burstCount = 0;
    Compare(gpu, kernels,
            &(Run){.emitters = emitters, .emitterCount = 2, .moving = true, .clearing = true},
            "two emitters under a moving instance, cleared");
}

// Bursts larger than the capacity at one tick, and past it with the rate.
static const mvfxBurst s_floods[2] = {
    {.time = 0.1f, .minCount = 40, .maxCount = 40, .cycles = 1, .probability = 1.0f},
    {.time = 0.3f, .minCount = 150, .maxCount = 150, .cycles = 1, .probability = 1.0f}};

static void TestFull(Gpu* gpu, mvfxGpu* kernels)
{
    // An emitter of 64 owed about 180 at a time, and a burst of 150 at one
    // tick: full every step, the room for each batch as the CPU path's
    // heap gives it.
    mvfxEmitterDef emitter = Sprite();
    emitter.capacity = 64;
    emitter.bursts = s_floods;
    emitter.burstCount = 2;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 64},
            "an emitter full every step");
}

// Bursts on ticks of exact quarters and eighths of a second, lives of a
// quarter: deaths fall on batches' ticks.
static const mvfxBurst s_exact[3] = {
    {.time = 0.125f, .minCount = 40, .maxCount = 40, .cycles = 1, .probability = 1.0f},
    {.time = 0.375f, .minCount = 32, .maxCount = 32, .cycles = 1, .probability = 1.0f},
    {.time = 0.625f, .minCount = 32, .maxCount = 32, .cycles = 1, .probability = 1.0f}};

static void TestBoundaries(Gpu* gpu, mvfxGpu* kernels)
{
    // Steps of half a second, an emitter of 32 lives of a quarter: the
    // first burst's 40 leaves 8 refused, its 32 die within the plan at
    // the second's tick and make it room; the second's die at the third's
    // tick, in the next plan.
    mvfxEmitterDef emitter = Sprite();
    emitter.capacity = 32;
    emitter.rate = 0.0f;
    emitter.bursts = s_exact;
    emitter.burstCount = 3;
    emitter.lifetime = mvfxRangeSource(0.25f, 0.25f);
    s_stepTicks = MVFX_TICKS_PER_SECOND / 2;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 4, .least = 32},
            "deaths on batches' ticks");
    s_stepTicks = STEP_TICKS;
    // Lives past 2^32 ticks: deaths in two words.
    emitter = Sprite();
    emitter.lifetime = mvfxRangeSource(5000.0f, 6000.0f);
    emitter.rate = 60.0f;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 50},
            "lives past 2^32 ticks");
}

static void TestRefused(mvfxGpu* kernels)
{
    // An emitter it does not run, after one it does: refused, named.
    static const mvfxForce force = {.kind = mvfx_forceConstant, .vector = {1, 0, 0}};
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    emitters[1].forces = &force;
    emitters[1].forceCount = 1;
    emitters[1].shape.kind = mvfx_shapeBox;
    emitters[1].velocity.randomize = 0.5f;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = 2;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
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
    mvfxGpuSim* sim = (mvfxGpuSim*)&sim;
    mvfxGpuRefusal refusal;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_errorState && sim == NULL &&
              refusal.emitter == 1 && refusal.features == MVFX_FEATURE_FORCES && refusal.shape &&
              refusal.velocity,
          "an emitter it does not run, named");
    simDef.instance = cpu;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_errorInvalid && refusal.emitter == -1,
          "an instance that is not planned");

    // A feature alone refuses it.
    emitters[1] = Sprite();
    emitters[1].forces = &force;
    emitters[1].forceCount = 1;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success, "planned");
    simDef.effect = effect;
    simDef.instance = planned;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_errorState && refusal.emitter == 1 &&
              refusal.features == MVFX_FEATURE_FORCES && !refusal.shape && !refusal.velocity,
          "a feature alone refused");
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
    TestMoving(&gpu, kernels);
    TestFull(&gpu, kernels);
    TestBoundaries(&gpu, kernels);
    TestRefused(kernels);
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
