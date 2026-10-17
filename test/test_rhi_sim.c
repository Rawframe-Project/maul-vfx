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
#include "maul-vfx/grid.h"
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
    // Room for an emitter of 2^17: its plan's births uploaded, its
    // particles read back.
    deviceDef.deviceLimits.frameUploadBytes = 8u << 20u;
    deviceDef.deviceLimits.readbackBytes = 16u << 20u;
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

// The emitter's particles and counts as the GPU holds them, read back, for
// capacities up to 2^17.
static uint32_t s_words[MVFX_GPU_PARTICLE_WORDS * (1 << 17)];
static uint32_t s_record[MVFX_GPU_STATISTICS_WORDS];

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
        mrhiImportBuffer(gpu->device, particles.statistics, &buffers[1]) == mrhi_success;
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
               mrhiReadBuffer(gpu->device, pass, buffers[1], 0, sizeof s_record, &reads[1]) ==
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
           mrhiTakeReadback(gpu->device, reads[1], s_record, sizeof s_record, &counted) ==
               mrhi_success &&
           counted == sizeof s_record;
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

// The contacts the CPU path's particles hold, summed over a run's steps.
static uint64_t s_contacts;

// The largest differences seen, place and velocity, over the run.
static float s_placeError;
static float s_velocityError;

// Whether the GPU's particles are the CPU path's: counts, serials and ticks
// equal, in order, places and velocities within the tolerance.
static bool SameAsCpu(const mvfxInstance* cpu, int32_t e)
{
    const mvfxEmitterState* state = &cpu->emitters[e];
    if ((int32_t)s_record[0] != state->count || s_record[2] != 0)
    {
        printf("counts: GPU %u (refused %u), CPU %d\n", s_record[0], s_record[2], state->count);
        return false;
    }
    const uint32_t* low = mvfxAttributeArray(cpu, e, mvfx_attributeBirthLow);
    const uint32_t* high = mvfxAttributeArray(cpu, e, mvfx_attributeBirthHigh);
    const uint32_t* serial = mvfxAttributeArray(cpu, e, mvfx_attributeSerial);
    const float* inverseLife = mvfxAttributeArray(cpu, e, mvfx_attributeInverseLife);
    const mvfxAttributePlan* plan = &cpu->effect->emitters[e].plan;
    const uint32_t* collisions = mvfxHasAttribute(plan, mvfx_attributeCollisions)
                                     ? mvfxAttributeArray(cpu, e, mvfx_attributeCollisions)
                                     : NULL;
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
               Word(12, i) == lifeBits && (collisions == NULL || Word(13, i) == collisions[i]);
        s_contacts += collisions != NULL ? collisions[i] : 0u;
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

// The context of a run: a CPU instance and a planned one side by side.
static mvfxContext* s_context;
static mvfxInstanceId s_cpu;
static mvfxInstanceId s_planned;

// The largest difference of the bounds seen over the run, relative to
// the box's size along its axis (as places, offsets from an anchor up to
// that far, are compared relative to theirs).
static double s_boundsError;

// Whether the GPU's bounds, from its record, are the CPU path's.
static bool SameBounds(const mvfxGpuSim* sim, int32_t e)
{
    mvfxBounds gpuBox;
    mvfxBounds cpuBox;
    if (mvfxGpuSim_BoundsOf(sim, e, s_record, &gpuBox) != mvfx_success ||
        mvfxInstance_GetBounds(s_context, s_cpu, e, &cpuBox) != mvfx_success ||
        gpuBox.empty != cpuBox.empty)
    {
        printf("bounds: GPU %s, CPU %s\n", gpuBox.empty ? "empty" : "a box",
               cpuBox.empty ? "empty" : "a box");
        return false;
    }
    bool same = true;
    for (int32_t k = 0; !cpuBox.empty && k < 3; ++k)
    {
        double size = 1.0 + (cpuBox.max[k] - cpuBox.min[k]);
        double low = fabs(gpuBox.min[k] - cpuBox.min[k]) / size;
        double high = fabs(gpuBox.max[k] - cpuBox.max[k]) / size;
        double most = low > high ? low : high;
        s_boundsError = most > s_boundsError ? most : s_boundsError;
        same = same && most <= 1e-4;
    }
    if (!same)
    {
        printf("bounds differ: GPU (%g %g %g)-(%g %g %g), CPU (%g %g %g)-(%g %g %g)\n",
               gpuBox.min[0], gpuBox.min[1], gpuBox.min[2], gpuBox.max[0], gpuBox.max[1],
               gpuBox.max[2], cpuBox.min[0], cpuBox.min[1], cpuBox.min[2], cpuBox.max[0],
               cpuBox.max[1], cpuBox.max[2]);
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
    // Whether the first emitter's particles must be killed on the CPU path
    // over the run (a scene that kills).
    bool killing;
    // Whether its particles must meet colliders over the run, and the
    // instance's colliders (none for NULL).
    bool colliding;
    const mvfxCollider* hostColliders;
    int32_t hostColliderCount;
    // Whether the instances stand still far from the origin, past where
    // binary32 places keep a noise's lattice.
    bool far;
    // A grid made in the run's context for its emitters' vector fields
    // (NULL for none), and whether it is given new values at half time.
    const mvfxGridDef* grid;
    bool animated;
    // How fast a moving instance goes, a share of Move's (0 for all):
    // particles a fast instance leaves behind are far from their anchor,
    // where binary32 places cannot hold a field's detail.
    double pace;
} Run;

// Moves both instances to step s's place: along a line past the rebase
// distance, turning about a tilted axis.
// Places both instances far from the origin, still.
static void PlaceFar(void)
{
    static const double place[3] = {100000.0, 2500.0, -60000.0};
    static const float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(mvfxInstance_SetTransform(s_context, s_cpu, place, q, 1.0f, true) == mvfx_success &&
              mvfxInstance_SetTransform(s_context, s_planned, place, q, 1.0f, true) == mvfx_success,
          "placed far");
}

static void Move(int32_t s, double pace)
{
    const double place[3] = {90.0 * pace * s, 3.0 * pace * s, -40.0 * pace * s};
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

// The run's grid, and its emitters with their vector fields reading it.
static mvfxGridId s_grid;
static mvfxEmitterDef s_emitters[2];
static mvfxForce s_forces[2][MVFX_MAX_FORCES];

// Makes the run's grid and points its emitters' vector fields at it.
static const mvfxEmitterDef* WithGrid(const Run* run)
{
    if (run->grid == NULL)
    {
        return run->emitters;
    }
    CHECK(mvfxCreateGrid(s_context, run->grid, &s_grid) == mvfx_success, "the run's grid");
    for (int32_t e = 0; e < run->emitterCount; ++e)
    {
        s_emitters[e] = run->emitters[e];
        for (int32_t f = 0; f < s_emitters[e].forceCount; ++f)
        {
            s_forces[e][f] = s_emitters[e].forces[f];
            if (s_forces[e][f].kind == mvfx_forceVectorField)
            {
                s_forces[e][f].grid = s_grid;
            }
        }
        s_emitters[e].forces = s_forces[e];
    }
    return s_emitters;
}

// Gives the run's grid new values: each doubled.
static void Animate(const mvfxGridDef* def)
{
    static float values[3 * 64];
    int64_t count = 3 * (int64_t)def->size[0] * def->size[1] * def->size[2];
    CHECK(count <= 3 * 64, "a small grid");
    for (int64_t i = 0; i < count; ++i)
    {
        values[i] = 2.0f * def->values[i];
    }
    CHECK(mvfxGrid_SetValues(s_context, s_grid, values, count) == mvfx_success, "animated");
}

static mvfxGpuSim* OpenRun(mvfxGpu* kernels, const Run* run, mvfxEffectId* effectOut)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = WithGrid(run);
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
    s_contacts = 0;
    if (run->hostColliders != NULL)
    {
        CHECK(mvfxInstance_SetColliders(s_context, s_cpu, run->hostColliders,
                                        run->hostColliderCount) == mvfx_success &&
                  mvfxInstance_SetColliders(s_context, s_planned, run->hostColliders,
                                            run->hostColliderCount) == mvfx_success,
              "the instance's colliders");
    }
    s_boundsError = 0.0;
    bool same = sim != NULL;
    int32_t most[2] = {0, 0};
    int32_t steps = run->steps > 0 ? run->steps : STEPS;
    if (run->far)
    {
        PlaceFar();
    }
    for (int32_t s = 0; same && s < steps; ++s)
    {
        if (run->moving)
        {
            Move(s, run->pace > 0.0 ? run->pace : 1.0);
        }
        if (run->clearing && s == STEPS / 2)
        {
            Clear();
        }
        if (run->animated && s == steps / 2)
        {
            Animate(run->grid);
        }
        CHECK(mvfxContext_StepTicks(s_context, s_stepTicks) == mvfx_success, "stepped");
        same = Simulate(gpu, sim);
        for (int32_t e = 0; same && e < run->emitterCount; ++e)
        {
            same = ReadBack(gpu, sim, e) && SameAsCpu(mvfxContextInstance(s_context, s_cpu), e) &&
                   SameBounds(sim, e);
            most[e] = (int32_t)s_record[0] > most[e] ? (int32_t)s_record[0] : most[e];
        }
    }
    printf("%s: largest relative differences, place %g, velocity %g, bounds %g\n", what,
           (double)s_placeError, (double)s_velocityError, s_boundsError);
    CHECK(same && most[0] >= (run->least > 0 ? run->least : 100) &&
              (run->emitterCount < 2 || most[1] > 64),
          what);
    if (run->colliding)
    {
        printf("  contacts held: %llu\n", (unsigned long long)s_contacts);
        CHECK(s_contacts > 0, "contacts");
    }
    if (run->killing)
    {
        const mvfxEmitterState* state = &mvfxContextInstance(s_context, s_cpu)->emitters[0];
        printf("  killed: %llu\n", (unsigned long long)state->killed);
        CHECK(state->killed > 0, "kills");
    }
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
    // Fixed bounds, a box in the instance's space, under the same motion.
    emitters[0].fixedBounds = true;
    emitters[0].boundsMin[0] = -1.0f;
    emitters[0].boundsMin[1] = -0.5f;
    emitters[0].boundsMin[2] = -2.0f;
    emitters[0].boundsMax[0] = 3.0f;
    emitters[0].boundsMax[1] = 4.0f;
    emitters[0].boundsMax[2] = 0.25f;
    Compare(gpu, kernels, &(Run){.emitters = emitters, .emitterCount = 1, .moving = true},
            "fixed bounds under a moving instance");
}

// Bursts larger than the capacity at one tick, and past it with the rate.
static const mvfxBurst s_floods[2] = {
    {.time = 0.1f, .minCount = 40, .maxCount = 40, .cycles = 1, .probability = 1.0f},
    {.time = 0.3f, .minCount = 150, .maxCount = 150, .cycles = 1, .probability = 1.0f}};

// A full emitter turned over in a half-second step: refused, then
// replaced once its particles die.
static const mvfxBurst s_turnover[3] = {
    {.time = 0.4375f, .minCount = 1000, .maxCount = 1000, .cycles = 1, .probability = 1.0f},
    {.time = 0.5625f, .minCount = 500, .maxCount = 500, .cycles = 1, .probability = 1.0f},
    {.time = 0.75f, .minCount = 1000, .maxCount = 1000, .cycles = 1, .probability = 1.0f}};
static const mvfxBurst s_turnoverLarge[3] = {
    {.time = 0.4375f, .minCount = 1 << 17, .maxCount = 1 << 17, .cycles = 1, .probability = 1.0f},
    {.time = 0.5625f, .minCount = 50000, .maxCount = 50000, .cycles = 1, .probability = 1.0f},
    {.time = 0.75f, .minCount = 1 << 17, .maxCount = 1 << 17, .cycles = 1, .probability = 1.0f}};

// An emitter of 32, bursts only, lives of a quarter of a second.
static mvfxEmitterDef Bursts(const mvfxBurst* bursts, int32_t burstCount);

static void TestForces(Gpu* gpu, mvfxGpu* kernels)
{
    // A constant force, a wind in a cone in the instance's space, an
    // attractor with a kill radius the particles fly through (both the
    // live and the newborn), in the instance's space, a vortex in
    // the instance's space, under a moving, turning, growing instance;
    // then a wind everywhere (the drag every particle shares), a constant
    // force in a tube, an attractor in a sphere, in the world.
    mvfxForce forces[4] = {{.kind = mvfx_forceConstant, .vector = {1, 0, 0}, .strength = 2.0f},
                           {.kind = mvfx_forceWind,
                            .space = mvfx_spaceLocal,
                            .vector = {0, 0, 3},
                            .strength = 0.5f,
                            .falloff = {.kind = mvfx_falloffCone,
                                        .center = {0, -0.5f, 0},
                                        .axis = {0, 1, 0},
                                        .radius = 3.0f,
                                        .angle = 0.6f,
                                        .exponent = 2.0f}},
                           {.kind = mvfx_forceAttractor,
                            .space = mvfx_spaceLocal,
                            .point = {0, 0.75f, 0},
                            .strength = 3.0f,
                            .softening = 0.5f,
                            .killRadius = 0.3f},
                           {.kind = mvfx_forceVortex,
                            .space = mvfx_spaceLocal,
                            .vector = {0, 1, 0},
                            .point = {0.5f, 0, 0},
                            .strength = 1.5f,
                            .pull = 0.25f}};
    mvfxEmitterDef emitter = Sprite();
    emitter.forces = forces;
    emitter.forceCount = 4;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true},
            "forces of four kinds under a moving instance");
    forces[0] = (mvfxForce){.kind = mvfx_forceWind, .vector = {2, 0, 0}, .strength = 1.5f};
    forces[1] = (mvfxForce){
        .kind = mvfx_forceConstant,
        .vector = {0, 0, 1},
        .strength = 4.0f,
        .falloff = {.kind = mvfx_falloffTube, .axis = {0, 1, 0}, .radius = 0.4f, .exponent = 1.0f}};
    forces[2] =
        (mvfxForce){.kind = mvfx_forceAttractor,
                    .point = {0.5f, 1.0f, 0},
                    .strength = -2.0f,
                    .softening = 0.25f,
                    .falloff = {.kind = mvfx_falloffSphere, .center = {0, 1, 0}, .radius = 1.5f}};
    emitter.forceCount = 3;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1},
            "a wind everywhere, falloffs of a tube and a sphere");
    // A strong attractor whose kill radius, out of a newborn's first
    // step's reach and smaller than a step, the living fly through: a
    // step can cross it with both ends outside.
    forces[0] = (mvfxForce){.kind = mvfx_forceAttractor,
                            .point = {0, 0.7f, 0},
                            .strength = 20.0f,
                            .softening = 0.2f,
                            .killRadius = 0.05f};
    emitter.forceCount = 1;
    emitter.rate = 3000.0f;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .least = 500, .killing = true},
            "the living killed by an attractor");
}

static void TestTurbulence(Gpu* gpu, mvfxGpu* kernels)
{
    // Turbulence of three octaves scrolling in the instance's space, as an
    // acceleration, under a turning, growing instance moving at a hundredth
    // of the usual pace (at full pace the particles it leaves behind are
    // 1,400 units from their anchor, where binary32 places differ from the
    // CPU path's turbulence by 1e-4 of a velocity, as measured); then as a
    // velocity everywhere (the drag every particle shares) and one in a
    // sphere, in the world, seeded by the instance, past 4,096 units from
    // the origin where binary32 places lose the lattice.
    mvfxForce forces[2] = {
        {.kind = mvfx_forceTurbulence,
         .space = mvfx_spaceLocal,
         .strength = 3.0f,
         .noise = {.frequency = 1.5f,
                   .octaves = 3,
                   .lacunarity = 2.0f,
                   .gain = 0.5f,
                   .scroll = {0.4f, -0.3f, 0.2f},
                   .seed = 9}},
    };
    mvfxEmitterDef emitter = Sprite();
    emitter.forces = forces;
    emitter.forceCount = 1;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .moving = true, .pace = 0.01},
            "turbulence in the instance's space under a moving instance");
    forces[0] = (mvfxForce){.kind = mvfx_forceTurbulence,
                            .strength = 1.0f,
                            .fieldMode = mvfx_fieldVelocity,
                            .tightness = 2.0f,
                            .noise = {.frequency = 0.7f,
                                      .octaves = 2,
                                      .lacunarity = 3.0f,
                                      .gain = 0.7f,
                                      .seed = 3,
                                      .seedPerInstance = true}};
    forces[1] = (mvfxForce){
        .kind = mvfx_forceTurbulence,
        .strength = 4.0f,
        .fieldMode = mvfx_fieldVelocity,
        .tightness = 1.0f,
        .falloff = {.kind = mvfx_falloffSphere, .center = {0, 0.5f, 0}, .radius = 1.0f},
        .noise = {.frequency = 2.0f, .octaves = 1, .lacunarity = 1.0f, .gain = 1.0f, .seed = 4}};
    emitter.forceCount = 2;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .far = true},
            "turbulence as velocities, far from the origin");
}

// A smooth field: three components a point, on up to 4,096 points.
static float s_field[3 * 4096];

static mvfxGridDef FieldGrid(mvfxGridFormat format, int32_t x, int32_t y, int32_t z)
{
    for (int32_t p = 0; p < x * y * z; ++p)
    {
        float px = (float)(p % x);
        float py = (float)(p / x % y);
        float pz = (float)(p / (x * y));
        s_field[3 * p] = sinf(0.9f * py + 0.3f * pz);
        s_field[3 * p + 1] = 0.5f * cosf(0.7f * px - 0.4f * pz);
        s_field[3 * p + 2] = sinf(0.5f * px + 0.8f * py) - 0.25f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = x;
    def.size[1] = y;
    def.size[2] = z;
    def.components = 3;
    def.format = format;
    def.values = s_field;
    return def;
}

// 4,096 points along X whose first component turns sign at each.
static mvfxGridDef LongGrid(void)
{
    for (int32_t p = 0; p < 4096; ++p)
    {
        s_field[3 * p] = (p & 1) != 0 ? 1.0f : -1.0f;
        s_field[3 * p + 1] = 0.0f;
        s_field[3 * p + 2] = 0.0f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 4096;
    def.components = 3;
    def.values = s_field;
    return def;
}

static void TestVectorFields(Gpu* gpu, mvfxGpu* kernels)
{
    // A turned box repeating its grid, given new values at half time, in
    // the world; a long grid repeating; a binary16 grid clamped at the
    // faces of a box the particles leave, as velocities,
    // in the instance's space under a turning, growing instance at a
    // hundredth of the usual pace; a box with no field past it that the
    // particles fly out of.
    mvfxForce forces[1] = {{.kind = mvfx_forceVectorField,
                            .point = {0.2f, 0.5f, -0.1f},
                            .strength = 3.0f,
                            .boxSize = {1.5f, 1.0f, 1.2f},
                            .boxRotation = {0.0f, 0.38268343f, 0.0f, 0.92387953f},
                            .border = mvfx_borderRepeat}};
    mvfxEmitterDef emitter = Sprite();
    emitter.forces = forces;
    emitter.forceCount = 1;
    mvfxGridDef grid = FieldGrid(mvfx_gridFloat32, 4, 4, 3);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .grid = &grid, .animated = true},
            "a turned box repeating its grid, animated");
    // 4,096 points along an unturned box of 100 repeating, each cell a
    // sign's turn: the cells and shares of a fine grid.
    forces[0].boxSize[0] = 100.0f;
    forces[0].boxRotation[1] = 0.0f;
    forces[0].boxRotation[3] = 1.0f;
    forces[0].strength = 0.5f;
    grid = LongGrid();
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .grid = &grid},
            "a long grid repeating");
    forces[0] = (mvfxForce){.kind = mvfx_forceVectorField,
                            .space = mvfx_spaceLocal,
                            .point = {0, 0.3f, 0},
                            .strength = 2.0f,
                            .fieldMode = mvfx_fieldVelocity,
                            .tightness = 3.0f,
                            .boxSize = {0.8f, 0.8f, 0.8f},
                            .boxRotation = {0, 0, 0, 1},
                            .border = mvfx_borderClamp};
    // An odd count of halves (81): the last alone in its word.
    grid = FieldGrid(mvfx_gridFloat16, 3, 3, 3);
    Compare(
        gpu, kernels,
        &(Run){
            .emitters = &emitter, .emitterCount = 1, .moving = true, .pace = 0.01, .grid = &grid},
        "a binary16 grid clamped, as velocities, under a moving instance");
    forces[0] = (mvfxForce){.kind = mvfx_forceVectorField,
                            .point = {0, 0.2f, 0},
                            .strength = 6.0f,
                            .boxSize = {0.8f, 0.6f, 0.8f},
                            .boxRotation = {0, 0, 0, 1},
                            .border = mvfx_borderNone};
    grid = FieldGrid(mvfx_gridFloat32, 4, 4, 3);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .grid = &grid},
            "a box with no field past it");
}

static void TestCollision(Gpu* gpu, mvfxGpu* kernels)
{
    // A floor, a turned box and an inside sphere around them, the
    // particles bouncing (restitution, friction, a rest speed many come
    // to rest at); then life
    // lost at each contact under a moving instance at a hundredth of the
    // usual pace, colliders in the instance's space and the instance's
    // own, apart (a particle rattling in a corner meets them by rounding);
    // then a contact that kills.
    static const mvfxCollider own[3] = {
        {.kind = mvfx_colliderPlane, .center = {0, -0.3f, 0}, .normal = {0, 1, 0.1f}},
        {.kind = mvfx_colliderBox,
         .center = {0.3f, 0.6f, 0},
         .halfExtents = {0.3f, 0.1f, 0.4f},
         .rotation = {0, 0, 0.38268343f, 0.92387953f}},
        {.kind = mvfx_colliderSphere, .inside = true, .center = {0, 0.2f, 0}, .radius = 1.0f}};
    mvfxEmitterDef emitter = Sprite();
    emitter.colliders = own;
    emitter.colliderCount = 3;
    emitter.collision = (mvfxCollision){
        .enabled = true, .restitution = 0.6f, .friction = 0.2f, .restSpeed = 0.6f, .radius = 0.02f};
    emitter.lifetime = mvfxRangeSource(0.6f, 0.9f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .colliding = true},
            "a floor, a box and a sphere bounced off");
    static const mvfxCollider local[2] = {{.kind = mvfx_colliderPlane,
                                           .space = mvfx_spaceLocal,
                                           .center = {0, -0.2f, 0},
                                           .normal = {0, 1, 0}},
                                          {.kind = mvfx_colliderBox,
                                           .space = mvfx_spaceLocal,
                                           .center = {-0.4f, 0.4f, 0},
                                           .halfExtents = {0.15f, 0.15f, 0.15f},
                                           .rotation = {0, 0, 0, 1}}};
    static const mvfxCollider hosts[1] = {
        {.kind = mvfx_colliderSphere, .center = {0, 0.5f, 0.4f}, .radius = 0.25f}};
    emitter.colliders = local;
    emitter.colliderCount = 2;
    emitter.collision.hostColliders = true;
    emitter.collision.lifeLoss = 0.3f;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter,
                   .emitterCount = 1,
                   .moving = true,
                   .pace = 0.01,
                   .colliding = true,
                   .hostColliders = hosts,
                   .hostColliderCount = 1},
            "life lost at contacts, the instance's colliders, a moving instance");
    emitter.colliders = own;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enabled = true, .killOnCollision = true};
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .killing = true},
            "a contact that kills");
}

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
    // Scans of many blocks of 1,024: about 2,400 live of 3,000, deaths
    // scattered among them by lives of 0.1 to 0.3 seconds; then 1,500
    // owed 500 a step, full, refusing across blocks.
    emitter = Sprite();
    emitter.capacity = 3000;
    emitter.rate = 12000.0f;
    emitter.lifetime = mvfxRangeSource(0.1f, 0.3f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 2000},
            "an emitter of many blocks");
    emitter.capacity = 1500;
    emitter.rate = 30000.0f;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 1500},
            "an emitter of many blocks, full");
    // Half-second steps: a full emitter whose particles all die in the
    // second step, which owes half its capacity refused before they die
    // and its capacity after: a sequence past twice the capacity, its
    // last kept beyond it.
    s_stepTicks = MVFX_TICKS_PER_SECOND / 2;
    emitter = Bursts(s_turnover, 3);
    emitter.capacity = 1000;
    emitter.lifetime = mvfxRangeSource(0.3f, 0.3f);
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 2, .least = 1000},
            "a sequence past twice the capacity");
    // The same at 2^17: more than 256 blocks, the offsets carried across
    // chunks.
    emitter.capacity = 1 << 17;
    emitter.bursts = s_turnoverLarge;
    Compare(gpu, kernels,
            &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 2, .least = 1 << 17},
            "more than 256 blocks");
    s_stepTicks = STEP_TICKS;
}

// Bursts on ticks of exact eighths of a second; lives of a quarter.
static const mvfxBurst s_exact[3] = {
    {.time = 0.125f, .minCount = 40, .maxCount = 40, .cycles = 1, .probability = 1.0f},
    {.time = 0.375f, .minCount = 32, .maxCount = 32, .cycles = 1, .probability = 1.0f},
    {.time = 0.625f, .minCount = 32, .maxCount = 32, .cycles = 1, .probability = 1.0f}};
// Bursts across two half-second plans: the second plan's first batch is all
// refused, held out by the first plan's 32, and dies on its last batch's
// tick.
static const mvfxBurst s_shared[4] = {
    {.time = 0.375f, .minCount = 32, .maxCount = 32, .cycles = 1, .probability = 1.0f},
    {.time = 0.5625f, .minCount = 16, .maxCount = 16, .cycles = 1, .probability = 1.0f},
    {.time = 0.625f, .minCount = 24, .maxCount = 24, .cycles = 1, .probability = 1.0f},
    {.time = 0.8125f, .minCount = 16, .maxCount = 16, .cycles = 1, .probability = 1.0f}};

// An emitter of 32, bursts only, lives of a quarter of a second.
static mvfxEmitterDef Bursts(const mvfxBurst* bursts, int32_t burstCount)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.capacity = 32;
    emitter.rate = 0.0f;
    emitter.bursts = bursts;
    emitter.burstCount = burstCount;
    emitter.lifetime = mvfxRangeSource(0.25f, 0.25f);
    return emitter;
}

static void TestBoundaries(Gpu* gpu, mvfxGpu* kernels)
{
    // Steps of a quarter: each burst's 32 die at the next burst's tick in
    // the next plan, making it room.
    mvfxEmitterDef emitter = Bursts(s_exact, 3);
    s_stepTicks = MVFX_TICKS_PER_SECOND / 4;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 4, .least = 32},
            "live particles dying on a batch's tick");
    // Steps of half a second: the first burst's 40 leaves 8 the plan
    // refuses; its 32 die within the plan at the second's tick.
    s_stepTicks = MVFX_TICKS_PER_SECOND / 2;
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 2, .least = 32},
            "births dying on a later batch's tick of their plan");
    // The refused 16 die on the last batch's tick, where the 24 still live:
    // they leave it room for 8, not 24.
    emitter = Bursts(s_shared, 4);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .steps = 2, .least = 32},
            "refused births taking no room");
    s_stepTicks = STEP_TICKS;
    // Lives of 4,096 seconds, 2^32 ticks, born early in their steps:
    // deaths just short of 2^32 ticks after the plan's start. A life 1,024
    // ticks longer: deaths whose high word is 1 and low word falls within
    // the plan. And longer ones.
    emitter = Sprite();
    emitter.rate = 60.0f;
    emitter.lifetime = mvfxRangeSource(4096.0f, 4096.0f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 50},
            "lives of 2^32 ticks");
    emitter.lifetime = mvfxRangeSource(4096.0009765625f, 4096.0009765625f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 50},
            "lives of 2^32 ticks and 1,024");
    emitter.lifetime = mvfxRangeSource(5000.0f, 6000.0f);
    Compare(gpu, kernels, &(Run){.emitters = &emitter, .emitterCount = 1, .least = 50},
            "lives past 2^32 ticks");
}

static void TestBoundsRefused(mvfxGpu* kernels)
{
    // Reading a record: refused for a NULL argument or an emitter out of
    // range, the bounds empty; a record with no particles is empty.
    mvfxEmitterDef emitter = Sprite();
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(kernels, &(Run){.emitters = &emitter, .emitterCount = 1}, &effect);
    uint32_t record[MVFX_GPU_STATISTICS_WORDS] = {0};
    mvfxBounds box = {.empty = false};
    CHECK(mvfxGpuSim_BoundsOf(sim, 0, record, &box) == mvfx_success && box.empty,
          "no particles, empty");
    box.empty = false;
    CHECK(mvfxGpuSim_BoundsOf(sim, 1, record, &box) == mvfx_errorInvalid && box.empty, "past");
    box.empty = false;
    CHECK(mvfxGpuSim_BoundsOf(sim, -1, record, &box) == mvfx_errorInvalid && box.empty, "negative");
    box.empty = false;
    CHECK(mvfxGpuSim_BoundsOf(NULL, 0, record, &box) == mvfx_errorInvalid && box.empty,
          "no simulation");
    box.empty = false;
    CHECK(mvfxGpuSim_BoundsOf(sim, 0, NULL, &box) == mvfx_errorInvalid && box.empty, "no record");
    CHECK(mvfxGpuSim_BoundsOf(sim, 0, record, NULL) == mvfx_errorInvalid, "no bounds");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(s_context);
}

static void TestHostGridRefused(Gpu* gpu, mvfxGpu* kernels)
{
    // The instance given a heightfield, a kind the part does not run yet:
    // the step's passes refused.
    mvfxEmitterDef emitter = Sprite();
    emitter.collision = (mvfxCollision){.enabled = true, .hostColliders = true};
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(kernels, &(Run){.emitters = &emitter, .emitterCount = 1}, &effect);
    static const float heights[4] = {0, 0.1f, 0.2f, 0.3f};
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[2] = 2;
    gridDef.values = heights;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(s_context, &gridDef, &grid) == mvfx_success, "a grid");
    const mvfxCollider heightfield = {.kind = mvfx_colliderHeightfield,
                                      .halfExtents = {1, 1, 1},
                                      .rotation = {0, 0, 0, 1},
                                      .grid = grid};
    CHECK(mvfxInstance_SetColliders(s_context, s_planned, &heightfield, 1) == mvfx_success &&
              mvfxContext_StepTicks(s_context, s_stepTicks) == mvfx_success,
          "stepped with a heightfield");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(gpu->device, &frame) == mrhi_success &&
              mvfxGpuSim_AddPasses(sim) == mvfx_errorState,
          "a heightfield of the instance refused");
    (void)mrhiDropFrame(gpu->device);
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(s_context);
}

static void TestRefused(mvfxGpu* kernels)
{
    // An emitter it does not run, after one it does: refused, named (a
    // feature it does not run yet, colors).
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    const mvfxVectorSource color = {
        .kind = mvfx_sourceRange, .min = {1, 0.5f, 0, 1}, .max = {1, 1, 0, 1}};
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    emitters[1].color = color;
    emitters[1].shape.kind = mvfx_shapeBox;
    emitters[1].velocity.randomize = 0.5f;
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
              refusal.emitter == 1 && refusal.features == MVFX_FEATURE_COLOR && refusal.shape &&
              refusal.velocity,
          "an emitter it does not run, named");
    simDef.instance = cpu;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_errorInvalid && refusal.emitter == -1,
          "an instance that is not planned");

    // A feature alone refuses it.
    emitters[1] = Sprite();
    emitters[1].color = color;
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(context, &instanceDef, &planned) == mvfx_success, "planned");
    simDef.effect = effect;
    simDef.instance = planned;
    CHECK(mvfxCreateGpuSim(&simDef, &sim, &refusal) == mvfx_errorState && refusal.emitter == 1 &&
              refusal.features == MVFX_FEATURE_COLOR && !refusal.shape && !refusal.velocity,
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
    TestForces(&gpu, kernels);
    TestTurbulence(&gpu, kernels);
    TestVectorFields(&gpu, kernels);
    TestCollision(&gpu, kernels);
    TestFull(&gpu, kernels);
    TestBoundaries(&gpu, kernels);
    TestRefused(kernels);
    TestBoundsRefused(kernels);
    TestHostGridRefused(&gpu, kernels);
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    return s_failures == 0 ? 0 : 1;
}
