// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part's step on the first adapter Maul RHI finds (a software one
// allowed), wall time from submission to completion: a sprite emitter of
// 16,384 particles, full, then the same inside 16 distance grids (8 its
// own, 8 the instance's) at the collision's worst: particles crossing a
// ball's grid of 64 points a side at 500 a second, a step's segment taking
// near the 256 samples a grid allows and its 4 contacts, each grid's
// crossing halved 10 times. Each case prints the best of three runs of 20
// steps, per particle a step; the collision's figure is the extra over the
// plain emitter. Then the bytes the plain emitter's step uploads
// (mvfxGpuSim_UploadBytes) and their share of its step: a frame writing
// that many bytes into a buffer against one writing 4, the best of three
// runs of 20 frames. Then births from events at 4,096 and 16,384 events a
// step: a source dying whole each step, its events buffer full, and a
// sub-emitter taking a birth from each death, both with room for four
// steps' births, against the same sub-emitter delayed past the run, which
// keeps no event: the extra is the births' passes (the events kept, their
// sort by tick in one workgroup, the scan and the merge) and the
// sub-emitter's own step, a share of the step without them. Last, a full
// emitter's births at 4,096 a step living a step, room freed only by the
// deaths of the particles it holds, then living half a step, freed by
// their own deaths too, each against the same with room for them all:
// what deciding room costs by the allowance and by the walk. A software
// adapter's figures say what the GPU's work costs against itself, not
// what a GPU takes.

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-vfx-rhi/gpu.h"
#include "maul-vfx-rhi/sim.h"
#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define CAPACITY 16384
#define SIDE     64
#define WARM     30
#define STEPS    20
#define RUNS     3
#define WAIT_NS  600000000000ull

typedef struct Gpu
{
    mrhiInstance* instance;
    mrhiDevice* device;
} Gpu;

static double Now(void)
{
    struct timespec now = {0};
    (void)timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

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
    // The grids' first upload: two of 512 KiB.
    deviceDef.deviceLimits.frameUploadBytes = 8u << 20u;
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

// Polls the device, ten seconds at most, for the object's pipelines.
static bool AwaitReady(Gpu* gpu, mvfxGpu* kernels)
{
    double deadline = Now() + 10.0;
    while (!mvfxGpu_IsReady(kernels) && Now() < deadline)
    {
        mrhiDeviceNotification record;
        while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
        {
            (void)mvfxGpu_Notify(kernels, &record);
        }
    }
    return mvfxGpu_IsReady(kernels);
}

// One step of the context and its frame on the GPU, waited for.
static bool Step(Gpu* gpu, mvfxContext* context, mvfxGpuSim* sim)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiRequestId token = {0};
    if (mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) != mvfx_success ||
        mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    if (mvfxGpuSim_AddPasses(sim) != mvfx_success ||
        mrhiCompileFrame(gpu->device) != mrhi_success || mvfxGpuSim_Record(sim) != mvfx_success)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    bool done = mrhiSubmitFrame(gpu->device, &token) == mrhi_success &&
                mrhiWaitFrame(gpu->device, token, WAIT_NS) == mrhi_success;
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(gpu->device, &record) == mrhi_success)
    {
    }
    return done;
}

// Signed distances to a ball of radius 0.9 at the middle of a box of half
// size 1, 64 points a side, as binary16.
static float s_distances[SIDE * SIDE * SIDE];

static mvfxGridDef BallGrid(void)
{
    for (int32_t p = 0; p < SIDE * SIDE * SIDE; ++p)
    {
        float x = -1.0f + 2.0f * (float)(p % SIDE) / (float)(SIDE - 1);
        float y = -1.0f + 2.0f * (float)(p / SIDE % SIDE) / (float)(SIDE - 1);
        float z = -1.0f + 2.0f * (float)(p / (SIDE * SIDE)) / (float)(SIDE - 1);
        s_distances[p] = sqrtf(x * x + y * y + z * z) - 0.9f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = SIDE;
    def.size[1] = SIDE;
    def.size[2] = SIDE;
    def.format = mvfx_gridFloat16;
    def.values = s_distances;
    return def;
}

static const mvfxBurst s_fill[1] = {
    {.time = 0.0f, .minCount = CAPACITY, .maxCount = CAPACITY, .cycles = 1, .probability = 1}};

// The emitter: full from its first step, its particles inside the balls
// and living past the run; with colliders, 8 inside balls of the grid.
static mvfxEmitterDef Emitter(const mvfxCollider* colliders, int32_t colliderCount, float speed)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = CAPACITY;
    emitter.bursts = s_fill;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitter.shape = (mvfxShape){.kind = mvfx_shapeSphere, .rotation = {0, 0, 0, 1}, .radius = 0.5f};
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionShapeNormal, .speed = {speed, speed}};
    emitter.colliders = colliders;
    emitter.colliderCount = colliderCount;
    emitter.collision = (mvfxCollision){
        .enabled = colliderCount > 0, .hostColliders = colliderCount > 0, .restitution = 1};
    return emitter;
}

// The best of RUNS runs of STEPS steps after WARM, in ns a particle a
// step, and the bytes the last step uploaded; a negative value when a step
// fails.
static double Run(Gpu* gpu, mvfxGpu* kernels, bool colliding, int64_t* uploadedOut)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return -1.0;
    }
    mvfxGridDef gridDef = BallGrid();
    mvfxGridId grid = {0};
    mvfxCollider balls[MVFX_MAX_COLLIDERS];
    for (int32_t c = 0; c < MVFX_MAX_COLLIDERS; ++c)
    {
        float shift = 0.001f * (float)c;
        balls[c] = (mvfxCollider){.kind = mvfx_colliderDistance,
                                  .inside = true,
                                  .center = {shift, -shift, shift},
                                  .halfExtents = {1, 1, 1},
                                  .rotation = {0, 0, 0, 1}};
    }
    bool made = mvfxCreateGrid(context, &gridDef, &grid) == mvfx_success;
    for (int32_t c = 0; c < MVFX_MAX_COLLIDERS; ++c)
    {
        balls[c].grid = grid;
    }
    mvfxEmitterDef emitter = Emitter(balls, colliding ? MVFX_MAX_COLLIDERS : 0, 500.0f);
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.planned = true;
    mvfxInstanceId instance = {0};
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    mvfxGpuSim* sim = NULL;
    made = made && mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made =
        made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success &&
        (!colliding ||
         (mvfxInstance_SetColliders(context, instance, balls, MVFX_MAX_COLLIDERS) == mvfx_success &&
          mvfxGpuSim_HostGridBytes(context, balls, MVFX_MAX_COLLIDERS, &simDef.hostGridBytes) ==
              mvfx_success));
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = instance;
    made = made && mvfxCreateGpuSim(&simDef, &sim, NULL) == mvfx_success;
    for (int32_t s = 0; made && s < WARM; ++s)
    {
        made = Step(gpu, context, sim);
    }
    double best = 1e300;
    for (int32_t r = 0; made && r < RUNS; ++r)
    {
        double start = Now();
        for (int32_t s = 0; made && s < STEPS; ++s)
        {
            made = Step(gpu, context, sim);
        }
        double seconds = Now() - start;
        best = seconds < best ? seconds : best;
    }
    made = made && mvfxGpuSim_UploadBytes(sim, uploadedOut) == mvfx_success;
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
    return made ? best * 1e9 / ((double)STEPS * CAPACITY) : -1.0;
}

// The best of RUNS runs of STEPS steps after WARM, in ns a step, of a
// source of births a step particles living life seconds, room for room
// steps' births, writing their deaths when it has a sub-emitter (sub), which
// spawns one birth a death (delayed past the run, so none, unless
// spawning), its births joining ribbons by ids; a negative value when a
// step fails.
static double Births(Gpu* gpu, mvfxGpu* kernels, int32_t births, int32_t room, bool sub,
                     bool spawning, float life, mvfxRibbonIds ribbons)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        return -1.0;
    }
    mvfxEmitterDef emitters[2] = {Emitter(NULL, 0, 1.0f), Emitter(NULL, 0, 1.0f)};
    emitters[0].capacity = room * births;
    emitters[0].burstCount = 0;
    emitters[0].rate = (float)births * 60.0f;
    emitters[0].lifetime = mvfxRangeSource(life, life);
    emitters[0].events = sub ? mvfx_eventMaskDeath : 0;
    emitters[0].ribbon.ids = ribbons;
    emitters[0].ribbon.texture = mvfx_ribbonTileByDistance;
    emitters[0].ribbon.breakDistance = 0.5f;
    emitters[0].eventCapacity = sub ? births : 0;
    emitters[1].capacity = room * births;
    emitters[1].burstCount = 0;
    emitters[1].rate = 0.0f;
    emitters[1].lifetime = mvfxRangeSource(1.0f / 60.0f, 1.0f / 60.0f);
    emitters[1].startDelay = spawning ? 0.0f : 1000.0f;
    emitters[1].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                              .source = 0,
                                              .kind = mvfx_eventDeath,
                                              .minCount = 1,
                                              .maxCount = 1,
                                              .capacity = births};
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitters;
    effectDef.emitterCount = sub ? 2 : 1;
    mvfxEffectId effect = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.planned = true;
    mvfxInstanceId instance = {0};
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    mvfxGpuSim* sim = NULL;
    bool made = mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success;
    instanceDef.effect = effect;
    made = made && mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
    simDef.gpu = kernels;
    simDef.context = context;
    simDef.effect = effect;
    simDef.instance = instance;
    made = made && mvfxCreateGpuSim(&simDef, &sim, NULL) == mvfx_success;
    for (int32_t s = 0; made && s < WARM; ++s)
    {
        made = Step(gpu, context, sim);
    }
    double best = 1e300;
    for (int32_t r = 0; made && r < RUNS; ++r)
    {
        double start = Now();
        for (int32_t s = 0; made && s < STEPS; ++s)
        {
            made = Step(gpu, context, sim);
        }
        double seconds = Now() - start;
        best = seconds < best ? seconds : best;
    }
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
    return made ? best * 1e9 / (double)STEPS : -1.0;
}

static uint8_t s_bytes[1 << 16];

// The best of RUNS runs of STEPS frames, each writing bytes into a buffer
// of its own in a transfer pass, in ns a frame; a negative value when a
// frame fails.
static double Uploads(Gpu* gpu, uint64_t bytes)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = sizeof s_bytes;
    def.usage = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    bool made =
        bytes <= sizeof s_bytes && mrhiCreateBuffer(gpu->device, &def, &buffer) == mrhi_success;
    double best = 1e300;
    for (int32_t r = 0; made && r < RUNS; ++r)
    {
        double start = Now();
        for (int32_t s = 0; made && s < STEPS; ++s)
        {
            mrhiFrameDef frame = mrhiDefaultFrameDef();
            mrhiResourceId resource = {0};
            mrhiPassId pass = {0};
            mrhiRequestId token = {0};
            made = mrhiBeginFrame(gpu->device, &frame) == mrhi_success &&
                   mrhiImportBuffer(gpu->device, buffer, &resource) == mrhi_success;
            const mrhiAccess written[1] = {
                {.resource = resource, .kind = mrhi_accessCopyDestination}};
            mrhiPassDef passDef = mrhiDefaultPassDef();
            passDef.passClass = mrhi_passTransfer;
            passDef.accesses = written;
            passDef.accessCount = 1;
            passDef.neverCull = true;
            made =
                made && mrhiAddPass(gpu->device, &passDef, &pass) == mrhi_success &&
                mrhiCompileFrame(gpu->device) == mrhi_success &&
                mrhiBeginPass(gpu->device, pass) == mrhi_success &&
                mrhiWriteBuffer(gpu->device, pass, resource, 0, s_bytes, bytes) == mrhi_success &&
                mrhiEndPass(gpu->device, pass) == mrhi_success &&
                mrhiSubmitFrame(gpu->device, &token) == mrhi_success &&
                mrhiWaitFrame(gpu->device, token, WAIT_NS) == mrhi_success;
        }
        double seconds = Now() - start;
        best = seconds < best ? seconds : best;
    }
    if (buffer.index1 != 0)
    {
        (void)mrhiDestroyBuffer(gpu->device, buffer);
    }
    return made ? best * 1e9 / (double)STEPS : -1.0;
}

int main(void)
{
    Gpu gpu;
    if (!Open(&gpu))
    {
        Close(&gpu);
        printf("no adapter: nothing measured\n");
        return 0;
    }
    mvfxGpuDef def = mvfxDefaultGpuDef();
    def.device = gpu.device;
    mvfxGpu* kernels = NULL;
    int failed = mvfxCreateGpu(&def, &kernels) != mvfx_success || !AwaitReady(&gpu, kernels);
    int64_t uploaded = 0;
    int64_t unused = 0;
    double plain = failed ? -1.0 : Run(&gpu, kernels, false, &uploaded);
    double colliding = plain < 0.0 ? -1.0 : Run(&gpu, kernels, true, &unused);
    double many = colliding < 0.0 ? -1.0 : Uploads(&gpu, (uint64_t)uploaded);
    double few = many < 0.0 ? -1.0 : Uploads(&gpu, 4);
    if (plain < 0.0 || colliding < 0.0 || few < 0.0)
    {
        printf("a step failed\n");
        failed = 1;
    }
    else
    {
        printf("# %d particles, wall time a step from submission\n", CAPACITY);
        printf("%-44s %12.2f ns a particle\n", "the sprite emitter's GPU step", plain);
        printf("%-44s %12.2f ns a particle\n", "16 distance grids at the worst (extra)",
               colliding - plain);
        double cost = many > few ? many - few : 0.0;
        printf("%-44s %12lld bytes\n", "the plain step's uploads", (long long)uploaded);
        printf("%-44s %12.0f ns, %.2f%% of the step\n",
               "their cost (a frame of them less one of 4)", cost,
               100.0 * cost / (plain * CAPACITY));
        static const int32_t sizes[2] = {4096, 16384};
        for (int32_t n = 0; n < 2 && !failed; ++n)
        {
            double without =
                Births(&gpu, kernels, sizes[n], 4, true, false, 1.0f / 60.0f, mvfx_ribbonNone);
            double with = without < 0.0 ? -1.0
                                        : Births(&gpu, kernels, sizes[n], 4, true, true,
                                                 1.0f / 60.0f, mvfx_ribbonNone);
            failed = with < 0.0;
            char label[64];
            (void)snprintf(label, sizeof label, "births from %d events a step (extra)", sizes[n]);
            printf("%-44s %12.0f ns, %.1f%% of the step without\n", label, with - without,
                   100.0 * (with - without) / without);
        }
        static const float lives[2] = {1.0f / 60.0f, 1.0f / 120.0f};
        static const char* const labels[2] = {"a full emitter's 4096 births a step (extra)",
                                              "the same dying within the step (extra)"};
        for (int32_t n = 0; n < 2 && !failed; ++n)
        {
            double roomy = Births(&gpu, kernels, 4096, 4, false, false, lives[n], mvfx_ribbonNone);
            double full = roomy < 0.0 ? -1.0
                                      : Births(&gpu, kernels, 4096, 1, false, false, lives[n],
                                               mvfx_ribbonNone);
            failed = full < 0.0;
            printf("%-44s %12.0f ns, %.1fx the step with room\n", labels[n], full - roomy,
                   full / roomy);
        }
        // Births joining one ribbon by the counter, broken past half a
        // unit, tiled by distance: the ribbons pass's walk over them.
        for (int32_t n = 0; n < 2 && !failed; ++n)
        {
            double plainBirths =
                Births(&gpu, kernels, sizes[n], 4, false, false, 1.0f / 60.0f, mvfx_ribbonNone);
            double ribboned = plainBirths < 0.0 ? -1.0
                                                : Births(&gpu, kernels, sizes[n], 4, false, false,
                                                         1.0f / 60.0f, mvfx_ribbonOne);
            failed = ribboned < 0.0;
            char label[64];
            (void)snprintf(label, sizeof label, "ribbons through %d births a step (extra)",
                           sizes[n]);
            printf("%-44s %12.0f ns, %.1f%% of the step without\n", label, ribboned - plainBirths,
                   100.0 * (ribboned - plainBirths) / plainBirths);
        }
        if (failed)
        {
            printf("a step failed\n");
        }
    }
    mvfxDestroyGpu(kernels);
    Close(&gpu);
    return failed;
}
