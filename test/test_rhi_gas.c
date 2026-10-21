// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Smoke gases on a real device (lavapipe on CI) beside the CPU path,
// compared after every step: each field the GPU writes through
// mvfxGpuSim_AddGasRead against mvfxInstance_ReadGas's, relative to its
// size. Covered: sources pouring and setting velocity, buoyancy, decay,
// closed and open walls, levels above the tail and in it, no solve, a
// 2D gas, a clear; every force; MacCormack's carry, vorticity
// confinement, the burn expanding (less its mean in a sealed box); the
// holds read back and reported, a stopped instance finishing; obstacles
// still and moving, of every kind; reads
// in every format over a box with padded pitches, the padding kept; the
// refusals.
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/resources.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// The most cells a scene's gas has, and the tolerance of a value relative
// to one more than its size.
#define MOST_CELLS (48 * 48 * 32)
#define TOLERANCE  1e-4f

static float s_cpu[MOST_CELLS];
static float s_gpu[MOST_CELLS];

// A run: its context, its instances, the planned one's simulation.
typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId cpu;
    mvfxInstanceId planned;
    mvfxGpuSim* sim;
} Scene;

// A sprite beside the gas: an effect has an emitter.
static mvfxEmitterDef Beside(void)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.capacity = 64;
    emitter.rate = 0.0f;
    return emitter;
}

// An emitter splatting into the scenes' gas, beside it when NULL.
static const mvfxEmitterDef* s_splatter;

// Opens a run of a gas; with a grid, made in the run's context and read
// by the gas's vector fields and grid obstacles.
static bool Open(mvfxGpu* kernels, const mvfxGasDef* gas, const mvfxGridDef* grid, Scene* scene)
{
    *scene = (Scene){0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene->context) == mvfx_success, "a context");
    mvfxGasDef made = *gas;
    static mvfxForce forces[MVFX_MAX_FORCES];
    mvfxGridId gridId = {0};
    if (grid != NULL)
    {
        CHECK(mvfxCreateGrid(scene->context, grid, &gridId) == mvfx_success, "a field's grid");
    }
    for (int32_t f = 0; f < gas->forceCount; ++f)
    {
        forces[f] = gas->forces[f];
        forces[f].grid = forces[f].kind == mvfx_forceVectorField ? gridId : forces[f].grid;
    }
    made.forces = gas->forceCount > 0 ? forces : NULL;
    static mvfxCollider obstacles[MVFX_MAX_COLLIDERS];
    for (int32_t c = 0; c < gas->obstacleCount; ++c)
    {
        obstacles[c] = gas->obstacles[c];
        bool gridded = obstacles[c].kind == mvfx_colliderHeightfield ||
                       obstacles[c].kind == mvfx_colliderDistance;
        obstacles[c].grid = gridded ? gridId : obstacles[c].grid;
    }
    made.obstacles = gas->obstacleCount > 0 ? obstacles : NULL;
    mvfxEmitterDef emitter = s_splatter != NULL ? *s_splatter : Beside();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    def.gases = &made;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene->context, &def, &effect) == mvfx_success, "a gas's effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.seed = 3;
    CHECK(mvfxCreateInstance(scene->context, &instanceDef, &scene->cpu) == mvfx_success,
          "a CPU instance");
    instanceDef.planned = true;
    CHECK(mvfxCreateInstance(scene->context, &instanceDef, &scene->planned) == mvfx_success,
          "a planned instance");
    mvfxGpuSimDef simDef = mvfxDefaultGpuSimDef();
    simDef.gpu = kernels;
    simDef.context = scene->context;
    simDef.effect = effect;
    simDef.instance = scene->planned;
    mvfxGpuRefusal refusal;
    mvfxResult opened = mvfxCreateGpuSim(&simDef, &scene->sim, &refusal);
    CHECK(opened == mvfx_success, "a gas's simulation");
    return opened == mvfx_success;
}

static void Close(Scene* scene)
{
    mvfxDestroyGpuSim(scene->sim);
    mvfxDestroyContext(scene->context);
}

// Writes a read of gas 0 from a byte of a buffer of bytes, filled with
// fill first, and reads the buffer back into out.
static bool ReadGpu(Gpu* gpu, const Scene* scene, const mvfxGasRead* read, uint64_t bytes,
                    uint64_t offset, uint8_t fill, void* out)
{
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = bytes;
    bufferDef.usage = mrhi_bufferStorage | mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    if (mrhiCreateBuffer(gpu->device, &bufferDef, &buffer) != mrhi_success)
    {
        return false;
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId target = {0};
    mrhiPassId passes[2] = {{0}, {0}};
    mrhiRequestId request = {0};
    bool recorded = mrhiBeginFrame(gpu->device, &frame) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, buffer, &target) == mrhi_success;
    const mrhiAccess filled = {.resource = target, .kind = mrhi_accessCopyDestination};
    const mrhiAccess taken = {.resource = target, .kind = mrhi_accessCopySource};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = &filled;
    def.accessCount = 1;
    def.neverCull = true;
    mvfxGpuGasReadDef readDef = mvfxDefaultGpuGasReadDef();
    readDef.read = *read;
    readDef.target = target;
    readDef.offset = offset;
    readDef.bytes = bytes - offset;
    recorded = recorded && mrhiAddPass(gpu->device, &def, &passes[0]) == mrhi_success &&
               mvfxGpuSim_AddGasRead(scene->sim, &readDef) == mvfx_success;
    def.accesses = &taken;
    recorded = recorded && mrhiAddPass(gpu->device, &def, &passes[1]) == mrhi_success &&
               mrhiCompileFrame(gpu->device) == mrhi_success &&
               mrhiBeginPass(gpu->device, passes[0]) == mrhi_success &&
               mrhiClearBuffer(gpu->device, passes[0], target, 0, MRHI_WHOLE_SIZE) == mrhi_success;
    // The fill: words of fill bytes over the buffer, cleared first.
    static uint8_t fills[1 << 16];
    memset(fills, fill, sizeof fills);
    for (uint64_t at = 0; recorded && at < bytes; at += sizeof fills)
    {
        uint64_t span = bytes - at < sizeof fills ? bytes - at : sizeof fills;
        recorded = mrhiWriteBuffer(gpu->device, passes[0], target, at, fills, span) == mrhi_success;
    }
    recorded = recorded && mrhiEndPass(gpu->device, passes[0]) == mrhi_success &&
               mvfxGpuSim_Record(scene->sim) == mvfx_success &&
               mrhiBeginPass(gpu->device, passes[1]) == mrhi_success &&
               mrhiReadBuffer(gpu->device, passes[1], target, 0, bytes, &request) == mrhi_success &&
               mrhiEndPass(gpu->device, passes[1]) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        (void)mrhiDestroyBuffer(gpu->device, buffer);
        return false;
    }
    size_t got = 0;
    bool done = Finish(gpu) &&
                mrhiTakeReadback(gpu->device, request, out, (size_t)bytes, &got) == mrhi_success &&
                got == bytes;
    (void)mrhiDestroyBuffer(gpu->device, buffer);
    return done;
}

// Reads gas 0's statistics record back and reports it to the planned
// instance, as a host does any frames after a step.
static bool ReportHolds(Gpu* gpu, const Scene* scene)
{
    mvfxGpuGas buffers;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId statistics = {0};
    mrhiPassId pass = {0};
    mrhiRequestId request = {0};
    if (mvfxGpuSim_GetGas(scene->sim, 0, &buffers) != mvfx_success ||
        mrhiBeginFrame(gpu->device, &frame) != mrhi_success)
    {
        return false;
    }
    bool recorded = mrhiImportBuffer(gpu->device, buffers.statistics, &statistics) == mrhi_success;
    const mrhiAccess reading = {.resource = statistics, .kind = mrhi_accessCopySource};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = &reading;
    def.accessCount = 1;
    def.neverCull = true;
    uint32_t record[MVFX_GPU_GAS_STATISTICS_WORDS];
    recorded =
        recorded && mrhiAddPass(gpu->device, &def, &pass) == mrhi_success &&
        mrhiCompileFrame(gpu->device) == mrhi_success &&
        mrhiBeginPass(gpu->device, pass) == mrhi_success &&
        mrhiReadBuffer(gpu->device, pass, statistics, 0, sizeof record, &request) == mrhi_success &&
        mrhiEndPass(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDropFrame(gpu->device);
        return false;
    }
    size_t got = 0;
    static mvfxGasPlan plan;
    bool read =
        Finish(gpu) &&
        mrhiTakeReadback(gpu->device, request, record, sizeof record, &got) == mrhi_success &&
        got == sizeof record &&
        mvfxInstance_GetGasPlan(scene->context, scene->planned, 0, &plan) == mvfx_success;
    // The record is of the step the plan spans.
    uint64_t tick = (uint64_t)record[MVFX_GPU_GAS_TICK] | (uint64_t)record[MVFX_GPU_GAS_TICK + 1]
                                                              << 32u;
    CHECK(!read || tick == (uint64_t)(plan.start + plan.ticks), "the record's tick");
    return read && mvfxGpuSim_ReportGas(scene->sim, scene->context, 0, record) == mvfx_success;
}

// The whole gas's field as float32 on both paths; the largest difference
// relative to one more than the CPU path's value into worst.
static bool SameField(Gpu* gpu, const Scene* scene, const mvfxGasDef* gas, mvfxGasField field,
                      float* worst)
{
    int64_t cells = (int64_t)gas->size[0] * gas->size[1] * gas->size[2];
    const mvfxGasRead read = {.field = field,
                              .format = mvfx_formatFloat32,
                              .size = {gas->size[0], gas->size[1], gas->size[2]}};
    if (mvfxInstance_ReadGas(scene->context, scene->cpu, 0, field, s_cpu, cells) != mvfx_success ||
        !ReadGpu(gpu, scene, &read, 4u * (uint64_t)cells, 0, 0, s_gpu))
    {
        return false;
    }
    bool same = true;
    for (int64_t i = 0; i < cells; ++i)
    {
        float apart = fabsf(s_gpu[i] - s_cpu[i]) / (1.0f + fabsf(s_cpu[i]));
        *worst = fmaxf(*worst, apart);
        same = same && apart <= TOLERANCE;
    }
    return same;
}

// The largest density the CPU path holds.
static float MostDensity(const Scene* scene, const mvfxGasDef* gas)
{
    int64_t cells = (int64_t)gas->size[0] * gas->size[1] * gas->size[2];
    float most = 0.0f;
    CHECK(mvfxInstance_ReadGas(scene->context, scene->cpu, 0, mvfx_gasDensity, s_cpu, cells) ==
              mvfx_success,
          "the CPU path's density");
    for (int64_t i = 0; i < cells; ++i)
    {
        most = fmaxf(most, s_cpu[i]);
    }
    return most;
}

// The step a scene stops both instances emitting at, -1 for none.
static int32_t s_stopAt = -1;

// The instance's colliders a scene sets on both instances before each
// step, moved by the step: a sphere crossing the box and a box turning.
static int32_t s_hostCount;

static void HostColliders(int32_t s, mvfxCollider out[2])
{
    float turn = 0.12f * (float)s;
    out[0] = (mvfxCollider){.kind = mvfx_colliderSphere,
                            .space = mvfx_spaceLocal,
                            .center = {-0.6f + 0.06f * (float)s, 0.1f, 0.05f},
                            .radius = 0.22f,
                            .rotation = {0, 0, 0, 1}};
    out[1] = (mvfxCollider){.kind = mvfx_colliderBox,
                            .space = mvfx_spaceLocal,
                            .center = {0.3f, 0.7f, -0.1f},
                            .halfExtents = {0.25f, 0.06f, 0.15f},
                            .rotation = {0, 0, sinf(turn), cosf(turn)}};
}

// Steps a gas on both paths, each field compared after every step, its
// holds reported and both instances finished alike; with clearing,
// stopped and cleared at half time and restarted; at s_stopAt stopped
// emitting, until finished.
static void Steps(Gpu* gpu, mvfxGpu* kernels, const mvfxGasDef* gas, const mvfxGridDef* grid,
                  int32_t steps, bool clearing, const char* what)
{
    Scene scene;
    if (!Open(kernels, gas, grid, &scene))
    {
        return;
    }
    bool same = true;
    float worst = 0.0f;
    float most = 0.0f;
    int32_t finished = -1;
    LongFrames(true);
    for (int32_t s = 0; same && finished < 0 && s < steps; ++s)
    {
        if (s == s_stopAt)
        {
            CHECK(mvfxInstance_Stop(scene.context, scene.cpu, mvfx_stopEmitting) == mvfx_success &&
                      mvfxInstance_Stop(scene.context, scene.planned, mvfx_stopEmitting) ==
                          mvfx_success,
                  "stopped emitting");
        }
        if (s_hostCount > 0)
        {
            mvfxCollider hosts[2];
            HostColliders(s, hosts);
            CHECK(mvfxInstance_SetColliders(scene.context, scene.cpu, hosts, s_hostCount) ==
                          mvfx_success &&
                      mvfxInstance_SetColliders(scene.context, scene.planned, hosts, s_hostCount) ==
                          mvfx_success,
                  "the instance's colliders");
        }
        if (clearing && s == steps / 2)
        {
            CHECK(mvfxInstance_Stop(scene.context, scene.cpu, mvfx_stopAndClear) == mvfx_success &&
                      mvfxInstance_Stop(scene.context, scene.planned, mvfx_stopAndClear) ==
                          mvfx_success &&
                      mvfxInstance_Restart(scene.context, scene.cpu) == mvfx_success &&
                      mvfxInstance_Restart(scene.context, scene.planned) == mvfx_success,
                  "cleared and restarted");
        }
        CHECK(mvfxContext_StepTicks(scene.context, StepTicks()) == mvfx_success, "stepped");
        same = Simulate(gpu, scene.sim);
        LongFrames(false);
        for (int32_t f = mvfx_gasDensity; same && f <= mvfx_gasVelocityZ; ++f)
        {
            same = SameField(gpu, &scene, gas, (mvfxGasField)f, &worst);
            if (!same)
            {
                printf("  step %d: field %d apart\n", s, f);
            }
        }
        most = fmaxf(most, MostDensity(&scene, gas));
        CHECK(!same || ReportHolds(gpu, &scene), "the holds reported");
        bool ended = mvfxInstance_IsFinished(scene.context, scene.cpu);
        if (same && ended != mvfxInstance_IsFinished(scene.context, scene.planned))
        {
            printf("  step %d: finished on one path\n", s);
            same = false;
        }
        finished = ended ? s : -1;
    }
    printf("%s: largest relative difference %g, most density %g, %d passes a step\n", what,
           (double)worst, (double)most, mvfxGpuSim_GasPasses(scene.sim, 0));
    if (s_stopAt >= 0)
    {
        printf("  stopped at step %d, finished at %d on both paths\n", s_stopAt, finished);
    }
    CHECK(same && most > 0.1f && (finished >= 0) == (s_stopAt >= 0), what);
    Close(&scene);
}

// A smoke gas of n cells along each axis rising from a sphere at its
// floor, hot and setting its velocity, its floor closed.
static mvfxGasDef Plume(int32_t x, int32_t y, int32_t z, const mvfxGasSource* source)
{
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = x;
    gas.size[1] = y;
    gas.size[2] = z;
    gas.cellSize = 0.1f;
    gas.substeps = 2;
    gas.pressureCycles = 2;
    gas.closedWalls = mvfx_gasWallMinY;
    gas.heatLift = 1.5f;
    gas.densityWeight = 0.25f;
    gas.gravity[1] = -9.8f;
    gas.densityDecay = 0.2f;
    gas.temperatureDecay = 0.5f;
    gas.velocityDecay = 0.1f;
    gas.sources = source;
    gas.sourceCount = 1;
    return gas;
}

static const mvfxGasSource s_sphere = {.shape = mvfx_gasSphere,
                                       .center = {0.05f, -0.8f, 0.0f},
                                       .radius = 0.3f,
                                       .density = 4.0f,
                                       .temperature = 2.0f,
                                       .fuel = 1.0f,
                                       .setsVelocity = true,
                                       .velocity = {0.2f, 1.0f, -0.1f}};
static const mvfxGasSource s_box = {.shape = mvfx_gasBox,
                                    .center = {-0.4f, -1.0f, 0.0f},
                                    .halfExtents = {0.3f, 0.2f, 0.5f},
                                    .density = 3.0f,
                                    .temperature = 1.0f};

static void TestPlumes(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxGasDef gas = Plume(16, 24, 16, &s_sphere);
    Steps(gpu, kernels, &gas, NULL, 30, false, "a plume of 16 by 24 by 16, its levels in the tail");
    // Levels of 48 by 48 by 32 (73,728 cells) and 24 by 24 by 16 above the
    // tail, which takes 12 by 12 by 8 down; every wall closed, four
    // substeps.
    gas = Plume(48, 48, 32, &s_sphere);
    gas.closedWalls = 63;
    gas.substeps = 4;
    Steps(gpu, kernels, &gas, NULL, 12, false, "a plume of 48 by 48 by 32, closed");
    // A 2D gas from a box, three cycles, cleared and restarted.
    gas = Plume(40, 32, 1, &s_box);
    gas.pressureCycles = 3;
    Steps(gpu, kernels, &gas, NULL, 30, true, "a 2D gas from a box, cleared");
    // No solve: the walls closed alone.
    gas = Plume(16, 16, 16, &s_sphere);
    gas.pressureCycles = 0;
    Steps(gpu, kernels, &gas, NULL, 20, false, "a gas without a solve");
}

// The bytes of a read in each path's buffer, filled alike first: the
// padding kept on both, each value within the tolerance (a normalized
// one within a step).
static uint8_t s_cpuBytes[1 << 16];
static uint8_t s_gpuBytes[1 << 16];

// A value of a format at bytes, decoded.
static float Decoded(mvfxFormat format, const uint8_t* at)
{
    switch (format)
    {
    case mvfx_formatUnorm8:
        return (float)at[0] / 255.0f;
    case mvfx_formatSnorm8:
        return (float)(int8_t)at[0] / 127.0f;
    case mvfx_formatUnorm16:
        return (float)(uint16_t)(at[0] | at[1] << 8) / 65535.0f;
    case mvfx_formatSnorm16:
        return (float)(int16_t)(uint16_t)(at[0] | at[1] << 8) / 32767.0f;
    case mvfx_formatFloat16:
    {
        uint16_t h = (uint16_t)(at[0] | at[1] << 8);
        float magnitude = ldexpf((float)((h & 0x3FFu) | ((h >> 10 & 0x1Fu) != 0 ? 0x400u : 0u)),
                                 (int)(h >> 10 & 0x1Fu) - 25 + ((h >> 10 & 0x1Fu) == 0 ? 1 : 0));
        return (h & 0x8000u) != 0 ? -magnitude : magnitude;
    }
    default:
    {
        float f;
        memcpy(&f, at, sizeof f);
        return f;
    }
    }
}

// Whether a read's bytes agree on both paths: every byte off the values
// the fill, each value within the format's step of the CPU path's.
static bool SameRead(Gpu* gpu, const Scene* scene, const mvfxGasRead* read, uint64_t offset,
                     uint64_t bytes, int32_t size)
{
    memset(s_cpuBytes, 0xA5, sizeof s_cpuBytes);
    if (mvfxInstance_ReadGasAs(scene->context, scene->cpu, 0, read, s_cpuBytes + offset,
                               (int64_t)(bytes - offset)) != mvfx_success ||
        !ReadGpu(gpu, scene, read, bytes, offset, 0xA5, s_gpuBytes))
    {
        return false;
    }
    float step = read->format == mvfx_formatFloat32 || read->format == mvfx_formatFloat16
                     ? 0.0f
                     : 1.0f / 127.0f;
    bool same = true;
    for (uint64_t b = 0; same && b < bytes; ++b)
    {
        uint64_t rel = b - offset;
        uint64_t row = rel % read->layerBytes % read->rowBytes;
        bool value = b >= offset && rel / read->layerBytes < (uint64_t)read->size[2] &&
                     rel % read->layerBytes / read->rowBytes < (uint64_t)read->size[1] &&
                     row < (uint64_t)(read->size[0] * size);
        if (!value)
        {
            same = s_cpuBytes[b] == s_gpuBytes[b] && s_gpuBytes[b] == 0xA5;
        }
        else if (row % (uint64_t)size == 0)
        {
            float c = Decoded(read->format, s_cpuBytes + b);
            float g = Decoded(read->format, s_gpuBytes + b);
            float allowed =
                read->format == mvfx_formatFloat16 ? 1e-3f * (1.0f + fabsf(c)) : step + TOLERANCE;
            same = fabsf(c - g) <= allowed;
        }
    }
    return same;
}

static void TestReads(Gpu* gpu, mvfxGpu* kernels)
{
    // After 15 steps of a plume: each format over a box from (2, 3, 1) of
    // 5 by 7 by 4 cells, its rows padded by 3 bytes and its layers by 6,
    // from byte 8 of the buffer; then the reads refused.
    mvfxGasDef gas = Plume(16, 24, 16, &s_sphere);
    Scene scene;
    if (!Open(kernels, &gas, NULL, &scene))
    {
        return;
    }
    LongFrames(true);
    for (int32_t s = 0; s < 15; ++s)
    {
        CHECK(mvfxContext_StepTicks(scene.context, StepTicks()) == mvfx_success &&
                  Simulate(gpu, scene.sim),
              "stepped");
        LongFrames(false);
    }
    static const mvfxFormat formats[6] = {mvfx_formatFloat32, mvfx_formatFloat16,
                                          mvfx_formatUnorm8,  mvfx_formatSnorm8,
                                          mvfx_formatUnorm16, mvfx_formatSnorm16};
    static const int32_t sizes[6] = {4, 2, 1, 1, 2, 2};
    bool same = true;
    for (int32_t f = 0; same && f < 6; ++f)
    {
        uint32_t row = (uint32_t)(5 * sizes[f] + 3);
        uint32_t layer = 7u * row + 6u;
        const mvfxGasRead read = {.field = f % 2 == 0 ? mvfx_gasDensity : mvfx_gasVelocityY,
                                  .format = formats[f],
                                  .first = {2, 3, 1},
                                  .size = {5, 7, 4},
                                  .low = -1.0f,
                                  .high = 2.0f,
                                  .rowBytes = row,
                                  .layerBytes = layer};
        same = SameRead(gpu, &scene, &read, 8, 8u + 4u * layer, sizes[f]);
        if (!same)
        {
            printf("  format %d apart\n", f);
        }
    }
    CHECK(same, "a gas's reads in every format");
    mvfxGpuGasReadDef def = mvfxDefaultGpuGasReadDef();
    def.target = (mrhiResourceId){.index1 = 1};
    def.bytes = 4;
    mvfxGpuGasReadDef bad[8] = {def, def, def, def, def, def, def, def};
    bad[0].cookie = 0;
    bad[1].gas = 1;
    bad[2].read.field = mvfx_gasDistance;
    bad[3].read.size[0] = 17;
    bad[4].read.format = mvfx_formatUnorm8;
    bad[5].offset = 2;
    bad[6].bytes = 3;
    bad[7].target = (mrhiResourceId){0};
    bool refused = mvfxGpuSim_AddGasRead(NULL, &def) == mvfx_errorInvalid &&
                   mvfxGpuSim_AddGasRead(scene.sim, NULL) == mvfx_errorInvalid;
    for (int32_t b = 0; b < 8; ++b)
    {
        refused = refused && mvfxGpuSim_AddGasRead(scene.sim, &bad[b]) == mvfx_errorInvalid;
    }
    CHECK(refused && mvfxGpuSim_GasPasses(scene.sim, 1) == 0 &&
              mvfxGpuSim_GasPasses(NULL, 0) == 0 && mvfxGpuSim_GasPasses(scene.sim, 0) == 74,
          "a gas read's refusals");
    // A gas's buffers and its report: refused for a NULL argument, a gas
    // out of range or another context.
    mvfxGpuGas buffers = {.statistics = {1, 1}};
    const uint32_t record[MVFX_GPU_GAS_STATISTICS_WORDS] = {0};
    mvfxContext* other = NULL;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxGpuSim_GetGas(NULL, 0, &buffers) == mvfx_errorInvalid &&
              buffers.statistics.index1 == 0 &&
              mvfxGpuSim_GetGas(scene.sim, 1, &buffers) == mvfx_errorInvalid &&
              mvfxGpuSim_GetGas(scene.sim, 0, NULL) == mvfx_errorInvalid &&
              mvfxGpuSim_GetGas(scene.sim, 0, &buffers) == mvfx_success &&
              buffers.statistics.index1 != 0 &&
              mvfxCreateContext(&contextDef, &other) == mvfx_success &&
              mvfxGpuSim_ReportGas(scene.sim, other, 0, record) == mvfx_errorInvalid &&
              mvfxGpuSim_ReportGas(NULL, scene.context, 0, record) == mvfx_errorInvalid &&
              mvfxGpuSim_ReportGas(scene.sim, scene.context, 1, record) == mvfx_errorInvalid &&
              mvfxGpuSim_ReportGas(scene.sim, scene.context, 0, NULL) == mvfx_errorInvalid,
          "a gas's buffers and report refused");
    mvfxDestroyContext(other);
    Close(&scene);
}

// A vector field's grid of 4 by 3 by 3 points, three components each.
static float s_field[3 * 4 * 3 * 3];

static mvfxGridDef FieldGrid(void)
{
    for (int32_t p = 0; p < 36; ++p)
    {
        float px = (float)(p % 4);
        float py = (float)(p / 4 % 3);
        float pz = (float)(p / 12);
        s_field[3 * p] = sinf(0.9f * py + 0.3f * pz);
        s_field[3 * p + 1] = 0.5f * cosf(0.7f * px - 0.4f * pz);
        s_field[3 * p + 2] = sinf(0.5f * px + 0.8f * py) - 0.25f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 4;
    def.size[1] = 3;
    def.size[2] = 3;
    def.components = 3;
    def.values = s_field;
    return def;
}

static void TestForces(Gpu* gpu, mvfxGpu* kernels)
{
    // A plume under a world wind fading over a sphere, a local turbulence,
    // a vortex, an attractor and a vector field pulling toward its
    // velocity (each face pushed at its place, a drag's exponential decay
    // its own).
    const mvfxForce forces[5] = {{.kind = mvfx_forceWind,
                                  .space = mvfx_spaceWorld,
                                  .vector = {1, 0, 0.5f},
                                  .strength = 0.8f,
                                  .falloff = {.kind = mvfx_falloffSphere,
                                              .center = {0, 0.2f, 0},
                                              .radius = 1.2f,
                                              .exponent = 1.5f}},
                                 {.kind = mvfx_forceTurbulence,
                                  .space = mvfx_spaceLocal,
                                  .strength = 2.0f,
                                  .noise = {.frequency = 1.5f,
                                            .octaves = 2,
                                            .lacunarity = 2.0f,
                                            .gain = 0.5f,
                                            .scroll = {0.3f, -0.2f, 0.1f},
                                            .seed = 9}},
                                 {.kind = mvfx_forceVortex,
                                  .space = mvfx_spaceLocal,
                                  .vector = {0, 1, 0},
                                  .point = {0.1f, 0, -0.1f},
                                  .strength = 1.5f,
                                  .pull = 0.25f},
                                 {.kind = mvfx_forceAttractor,
                                  .space = mvfx_spaceLocal,
                                  .point = {0.3f, 0.5f, 0},
                                  .strength = 1.0f,
                                  .softening = 0.2f},
                                 {.kind = mvfx_forceVectorField,
                                  .space = mvfx_spaceLocal,
                                  .point = {0, 0.3f, 0},
                                  .strength = 2.0f,
                                  .fieldMode = mvfx_fieldVelocity,
                                  .tightness = 3.0f,
                                  .boxSize = {1.6f, 1.6f, 1.6f},
                                  .boxRotation = {0, 0, 0, 1},
                                  .border = mvfx_borderClamp}};
    mvfxGasDef gas = Plume(16, 24, 16, &s_sphere);
    gas.forces = forces;
    gas.forceCount = 5;
    mvfxGridDef grid = FieldGrid();
    Steps(gpu, kernels, &gas, &grid, 30, false, "a plume under every force");
}

static void TestBurns(Gpu* gpu, mvfxGpu* kernels)
{
    // A plume carried by MacCormack's under vorticity confinement.
    mvfxGasDef gas = Plume(16, 24, 16, &s_sphere);
    gas.advection = mvfx_gasMacCormack;
    gas.vorticity = 0.4f;
    Steps(gpu, kernels, &gas, NULL, 30, false, "a plume by MacCormack's, confined");
    // A 2D gas the same, cleared and restarted.
    gas = Plume(40, 32, 1, &s_box);
    gas.advection = mvfx_gasMacCormack;
    gas.vorticity = 0.6f;
    Steps(gpu, kernels, &gas, NULL, 30, true, "a 2D gas by MacCormack's, confined, cleared");
    // A fire: the sphere's fuel burning where it is hot enough, heating,
    // smoking and expanding, open above.
    gas = Plume(16, 24, 16, &s_sphere);
    gas.ignitionTemperature = 0.5f;
    gas.burnRate = 4.0f;
    gas.burnHeat = 1.5f;
    gas.burnSmoke = 0.5f;
    gas.burnExpansion = 0.3f;
    Steps(gpu, kernels, &gas, NULL, 30, false, "a fire, expanding");
    // The same sealed, its expansion less its mean, by MacCormack's and
    // confined.
    gas.closedWalls = 63;
    gas.advection = mvfx_gasMacCormack;
    gas.vorticity = 0.3f;
    Steps(gpu, kernels, &gas, NULL, 30, false, "a sealed fire by MacCormack's, confined");
}

static void TestFinishing(Gpu* gpu, mvfxGpu* kernels)
{
    // A plume stopped emitting, its scalars decaying fast, its
    // temperature the last to fall to nothing: finished on that step, on
    // both paths alike.
    mvfxGasDef gas = Plume(16, 16, 16, &s_sphere);
    gas.densityDecay = 20.0f;
    gas.temperatureDecay = 12.0f;
    gas.fuelDecay = 20.0f;
    s_stopAt = 10;
    Steps(gpu, kernels, &gas, NULL, 120, false, "a plume stopped and finishing");
    s_stopAt = -1;
}

// A ball's distances over a grid of 9 points a side, 0.1 apart: radius
// 0.3 about the grid's center.
static float s_ball[9 * 9 * 9];

static mvfxGridDef BallGrid(void)
{
    for (int32_t p = 0; p < 9 * 9 * 9; ++p)
    {
        float x = 0.1f * (float)(p % 9) - 0.4f;
        float y = 0.1f * (float)(p / 9 % 9) - 0.4f;
        float z = 0.1f * (float)(p / 81) - 0.4f;
        s_ball[p] = sqrtf(x * x + y * y + z * z) - 0.3f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 9;
    def.size[1] = 9;
    def.size[2] = 9;
    def.values = s_ball;
    return def;
}

static void TestObstacles(Gpu* gpu, mvfxGpu* kernels)
{
    // A plume about its own still obstacles: a sphere over the source, a
    // turned box, a distance grid's ball, and a plane taking the box's top
    // cells.
    const mvfxCollider obstacles[4] = {{.kind = mvfx_colliderSphere,
                                        .space = mvfx_spaceLocal,
                                        .center = {0.0f, 0.15f, 0.0f},
                                        .radius = 0.25f,
                                        .rotation = {0, 0, 0, 1}},
                                       {.kind = mvfx_colliderBox,
                                        .space = mvfx_spaceLocal,
                                        .center = {-0.35f, 0.6f, 0.1f},
                                        .halfExtents = {0.2f, 0.05f, 0.3f},
                                        .rotation = {0, 0, 0.2588190f, 0.9659258f}},
                                       {.kind = mvfx_colliderDistance,
                                        .space = mvfx_spaceLocal,
                                        .center = {0.4f, -0.3f, -0.2f},
                                        .halfExtents = {0.4f, 0.4f, 0.4f},
                                        .rotation = {0, 0, 0, 1}},
                                       {.kind = mvfx_colliderPlane,
                                        .space = mvfx_spaceLocal,
                                        .center = {0.0f, 1.0f, 0.0f},
                                        .normal = {0, -1, 0},
                                        .rotation = {0, 0, 0, 1}}};
    mvfxGasDef gas = Plume(16, 24, 16, &s_sphere);
    gas.obstacles = obstacles;
    gas.obstacleCount = 4;
    mvfxGridDef ball = BallGrid();
    Steps(gpu, kernels, &gas, &ball, 30, false, "a plume about still obstacles");
    // The same with the instance's colliders moving through it: a sphere
    // crossing it and a box turning, their faces pushing the gas.
    gas.hostObstacles = true;
    s_hostCount = 2;
    Steps(gpu, kernels, &gas, &ball, 30, false, "a plume about moving obstacles");
    // A 2D gas about a sphere crossing it, cleared and restarted.
    gas = Plume(40, 32, 1, &s_box);
    gas.hostObstacles = true;
    s_hostCount = 1;
    Steps(gpu, kernels, &gas, NULL, 30, true, "a 2D gas about a moving sphere, cleared");
    s_hostCount = 0;
    // A plume kept inside a sphere and a box: the sphere takes the box's
    // corners, the box the sphere's sides.
    const mvfxCollider within[2] = {{.kind = mvfx_colliderSphere,
                                     .space = mvfx_spaceLocal,
                                     .radius = 0.9f,
                                     .rotation = {0, 0, 0, 1},
                                     .inside = true},
                                    {.kind = mvfx_colliderBox,
                                     .space = mvfx_spaceLocal,
                                     .halfExtents = {0.7f, 1.0f, 0.7f},
                                     .rotation = {0, 0, 0, 1},
                                     .inside = true}};
    gas = Plume(16, 24, 16, &s_sphere);
    gas.obstacles = within;
    gas.obstacleCount = 2;
    Steps(gpu, kernels, &gas, NULL, 30, false, "a plume inside a sphere and a box");
}

// Emitters following a gas: world particles pulled toward a moving
// plume's velocity, and local ones pushed by it from below its box; then
// a 2D gas's, cleared at half time; then one emitter following two.
static void TestFollowing(Gpu* gpu, mvfxGpu* kernels)
{
    static const mvfxForce pulled = {.kind = mvfx_forceGas,
                                     .strength = 1.0f,
                                     .fieldMode = mvfx_fieldVelocity,
                                     .tightness = 8.0f};
    static const mvfxForce pushed = {
        .kind = mvfx_forceGas, .strength = 2.0f, .fieldMode = mvfx_fieldAcceleration};
    mvfxEmitterDef emitters[2] = {Sprite(), Sprite()};
    emitters[0].space = mvfx_spaceWorld;
    emitters[0].forces = &pulled;
    emitters[0].forceCount = 1;
    emitters[1].shape.offset[1] = -1.6f;
    emitters[1].forces = &pushed;
    emitters[1].forceCount = 1;
    mvfxGasDef gas = Plume(16, 24, 16, &s_sphere);
    Run run = {.emitters = emitters,
               .emitterCount = 2,
               .gases = &gas,
               .gasCount = 1,
               .moving = true,
               .pace = 0.002};
    Compare(gpu, kernels, &run, "particles following a moving plume");
    gas = Plume(40, 32, 1, &s_box);
    emitters[0].space = mvfx_spaceLocal;
    run = (Run){
        .emitters = emitters, .emitterCount = 2, .gases = &gas, .gasCount = 1, .clearing = true};
    Compare(gpu, kernels, &run, "particles following a 2D gas, cleared");
    // One emitter following two gases, pulled by the first and pushed by
    // the second, whose faces sit past the first's in its grids.
    static const mvfxForce both[2] = {
        {.kind = mvfx_forceGas,
         .strength = 1.0f,
         .fieldMode = mvfx_fieldVelocity,
         .tightness = 8.0f},
        {.kind = mvfx_forceGas, .strength = 2.0f, .fieldMode = mvfx_fieldAcceleration, .gas = 1}};
    mvfxGasDef gases[2] = {Plume(16, 24, 16, &s_sphere), Plume(16, 24, 16, &s_sphere)};
    gases[1].center[1] = -1.0f;
    emitters[0].forces = both;
    emitters[0].forceCount = 2;
    emitters[0].shape.offset[1] = -0.8f;
    run = (Run){.emitters = emitters, .emitterCount = 1, .gases = gases, .gasCount = 2};
    Compare(gpu, kernels, &run, "particles following two gases");
}

// Emitters splatting into a gas, their particles kept well inside its box
// (one pouring on one path alone, at a face, would set the paths apart
// by its whole splat): a local one into a plume, rising and pulling the
// faces; a world one into a 2D gas, cleared at half time.
static void TestSplats(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef splatter = Sprite();
    splatter.capacity = 512;
    splatter.rate = 300.0f;
    splatter.burstCount = 0;
    splatter.lifetime = mvfxRangeSource(0.3f, 0.6f);
    splatter.shape.radius = 0.3f;
    splatter.velocity.speed = (mvfxRange){0.5f, 1.0f};
    splatter.motion.gravity[1] = 0.0f;
    splatter.space = mvfx_spaceLocal;
    splatter.gasSplat = (mvfxGasSplat){
        .enabled = true, .density = 2.0f, .temperature = 1.0f, .fuel = 0.5f, .drag = 4.0f};
    s_splatter = &splatter;
    mvfxGasDef gas = Plume(16, 24, 16, &s_sphere);
    Steps(gpu, kernels, &gas, NULL, 30, false, "a plume splatted into");
    splatter.space = mvfx_spaceWorld;
    gas = Plume(40, 32, 1, &s_box);
    Steps(gpu, kernels, &gas, NULL, 30, true, "a 2D gas splatted into, cleared");
    s_splatter = NULL;
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestSplats(gpu, kernels);
    TestFollowing(gpu, kernels);
    TestPlumes(gpu, kernels);
    TestForces(gpu, kernels);
    TestBurns(gpu, kernels);
    TestFinishing(gpu, kernels);
    TestObstacles(gpu, kernels);
    TestReads(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
