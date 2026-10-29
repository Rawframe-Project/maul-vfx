// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Surface gases on a real device (lavapipe on CI) beside the CPU path,
// compared after every step through the harness's runs: the distance the
// GPU writes through mvfxGpuSim_AddGasRead against mvfxInstance_ReadGas's,
// relative to its size (rhi_surfaces.c). Covered: drops pooling on a
// floor in a 3D box, with Solenthaler's correction and without, with no
// smoothing and the most; a 2D box; particles beyond the box, a box off
// the instance's origin; a moving instance, the box in the world and in
// the instance; a clear; frames dropped; an emitter left out by the
// quality (the reach everywhere); the passes; the record read back and
// reported, a stopped instance finishing; a read in a normalized format;
// the distance sampled at points (mvfxGpuSim_AddGasSamples), the reach
// past the box; the refusals.
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "gas.h"
#include "gas_api.h"
#include "instance.h"
#include "rhi_harness.h"
#include "rhi_samples.h"
#include "rhi_surfaces.h"
#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/resources.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const mvfxCollider s_floor = {
    .kind = mvfx_colliderPlane, .center = {0, -0.4f, 0}, .normal = {0, 1, 0}};

// A surface gas of x by y by z cells of 0.05 about the origin, wrapping
// the first emitter's particles of 0.04 within three radii.
static mvfxGasDef Wrap(int32_t x, int32_t y, int32_t z)
{
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.kind = mvfx_gasSurface;
    gas.size[0] = x;
    gas.size[1] = y;
    gas.size[2] = z;
    gas.cellSize = 0.05f;
    gas.surface.emitter = 0;
    gas.surface.radius = 0.04f;
    gas.surface.reach = 3.0f;
    return gas;
}

// Drops poured down from a small sphere onto a floor, where they pool.
static mvfxEmitterDef Drops(void)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.capacity = 512;
    emitter.rate = 300.0f;
    emitter.burstCount = 0;
    emitter.lifetime = mvfxRangeSource(1.0f, 1.5f);
    emitter.shape.radius = 0.15f;
    emitter.velocity.direction[1] = -1.0f;
    emitter.velocity.speed = (mvfxRange){0.2f, 0.5f};
    emitter.colliders = &s_floor;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enabled = true};
    return emitter;
}

// Compares a run of drops wrapped by a gas, adopting the CPU path's
// particles each step.
static void Wrapped(Gpu* gpu, mvfxGpu* kernels, const mvfxGasDef* gas,
                    const mvfxEmitterDef* emitter, Run run, const char* what)
{
    run.emitters = emitter;
    run.emitterCount = 1;
    run.gases = gas;
    run.gasCount = 1;
    run.adopting = true;
    Compare(gpu, kernels, &run, what);
}

static void TestWrapped(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Drops();
    mvfxGasDef gas = Wrap(24, 24, 16);
    Wrapped(gpu, kernels, &gas, &emitter, (Run){0}, "drops wrapped in a box");
    // Without Solenthaler's correction (low at high), and unsmoothed.
    gas.surface.low = 0.0f;
    gas.surface.high = 0.0f;
    gas.surface.smoothing = 0;
    Wrapped(gpu, kernels, &gas, &emitter, (Run){0}, "drops wrapped, uncorrected and unsmoothed");
    // The most smoothing, a wide reach and its eigenvalue's thresholds
    // low, so most cells' are past the lower (no cell then lies within a
    // particle).
    gas = Wrap(24, 24, 16);
    gas.surface.smoothing = MVFX_MAX_SURFACE_SMOOTHING;
    gas.surface.reach = 5.0f;
    gas.surface.low = 0.05f;
    gas.surface.high = 0.8f;
    Wrapped(gpu, kernels, &gas, &emitter, (Run){0}, "drops wrapped, smoothed the most");
}

static void TestPlanar(Gpu* gpu, mvfxGpu* kernels)
{
    // A 2D box, its particles at its cells' Z.
    mvfxEmitterDef emitter = Drops();
    mvfxGasDef gas = Wrap(32, 32, 1);
    Wrapped(gpu, kernels, &gas, &emitter, (Run){0}, "drops wrapped in a 2D box");
}

static void TestBeyond(Gpu* gpu, mvfxGpu* kernels)
{
    // A box narrower than the drops' sphere, off the instance's origin:
    // particles beyond its buckets are in none.
    mvfxEmitterDef emitter = Drops();
    emitter.shape.radius = 0.5f;
    mvfxGasDef gas = Wrap(12, 20, 8);
    gas.center[0] = 0.2f;
    gas.center[1] = -0.1f;
    Wrapped(gpu, kernels, &gas, &emitter, (Run){0}, "drops partly beyond their box");
}

static void TestMoving(Gpu* gpu, mvfxGpu* kernels)
{
    // A moving, turning, growing instance: the drops in the world, the
    // box in the instance; then the box in the world where the instance
    // began, the drops in the instance.
    mvfxEmitterDef emitter = Drops();
    emitter.colliderCount = 0;
    emitter.collision.enabled = false;
    emitter.space = mvfx_spaceWorld;
    mvfxGasDef gas = Wrap(24, 24, 16);
    Wrapped(gpu, kernels, &gas, &emitter, (Run){.moving = true, .pace = 0.001},
            "world drops wrapped in a moving box");
    emitter.space = mvfx_spaceLocal;
    gas.space = mvfx_gasWorld;
    Wrapped(gpu, kernels, &gas, &emitter, (Run){.moving = true, .pace = 0.001},
            "a moving instance's drops wrapped in a world box");
}

static void TestCleared(Gpu* gpu, mvfxGpu* kernels)
{
    // Cleared and restarted at half time; and each frame dropped once
    // first, its steps undone.
    mvfxEmitterDef emitter = Drops();
    mvfxGasDef gas = Wrap(24, 24, 16);
    // The clear at step 20 holds the drops to about 100.
    Wrapped(gpu, kernels, &gas, &emitter, (Run){.clearing = true, .least = 80},
            "drops wrapped and cleared");
    Wrapped(gpu, kernels, &gas, &emitter, (Run){.dropping = true},
            "drops wrapped, each frame dropped once first");
}

static void TestOmitted(Gpu* gpu, mvfxGpu* kernels)
{
    // The drops left out by the quality: no room on either path, the
    // reach in every cell.
    static const mvfxQualityLevel quality = {
        .capacityScale = 1.0f, .rateScale = 1.0f, .omitted = 1};
    mvfxEmitterDef emitter = Drops();
    mvfxGasDef gas = Wrap(24, 24, 16);
    Wrapped(gpu, kernels, &gas, &emitter, (Run){.quality = &quality, .least = -1},
            "a box of drops the quality leaves out");
    CHECK(SurfaceLeast() == gas.surface.reach * gas.surface.radius, "the reach in every cell");
}

static void TestPasses(mvfxGpu* kernels)
{
    // The write, the keys, the sort's eight, the runs, the field, and a
    // mean and a lower a smoothing pass.
    mvfxEmitterDef emitter = Drops();
    mvfxGasDef gas = Wrap(24, 24, 16);
    gas.surface.smoothing = 3;
    mvfxEffectId effect = {0};
    const Run run = {.emitters = &emitter, .emitterCount = 1, .gases = &gas, .gasCount = 1};
    mvfxGpuSim* sim = OpenRun(kernels, &run, &effect);
    CHECK(mvfxGpuSim_GasPasses(sim, 0) == 1 + 1 + 8 + 1 + 1 + 2 * 3, "a surface gas's passes");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
}

static void TestFinishing(Gpu* gpu, mvfxGpu* kernels)
{
    // Stopped emitting with drops alive: the planned instance finishes at
    // the step the CPU path's does, once its record says the surface
    // holds nothing.
    mvfxEmitterDef emitter = Drops();
    emitter.lifetime = mvfxRangeSource(0.2f, 0.4f);
    mvfxGasDef gas = Wrap(24, 24, 16);
    mvfxEffectId effect = {0};
    const Run run = {.emitters = &emitter, .emitterCount = 1, .gases = &gas, .gasCount = 1};
    mvfxGpuSim* sim = OpenRun(kernels, &run, &effect);
    mvfxContext* context = RunContext();
    bool same = sim != NULL;
    int32_t finished = -1;
    LongFrames(true);
    for (int32_t s = 0; same && finished < 0 && s < 120; ++s)
    {
        if (s == 10)
        {
            same = mvfxInstance_Stop(context, CpuInstance(), mvfx_stopEmitting) == mvfx_success &&
                   mvfxInstance_Stop(context, PlannedInstance(), mvfx_stopEmitting) == mvfx_success;
        }
        same = same && mvfxContext_StepTicks(context, StepTicks()) == mvfx_success &&
               Simulate(gpu, sim) && ReportGases(gpu, sim);
        LongFrames(false);
        bool ended = mvfxInstance_IsFinished(context, CpuInstance());
        if (same && ended != mvfxInstance_IsFinished(context, PlannedInstance()))
        {
            printf("  step %d: finished on one path\n", s);
            same = false;
        }
        finished = ended ? s : -1;
    }
    printf("drops stopped: finished at step %d on both paths\n", finished);
    CHECK(same && finished > 10, "a stopped surface finishing");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
}

// The bytes of a read on each path.
static uint8_t s_cpuBytes[1 << 16];
static uint8_t s_gpuBytes[1 << 16];

// Writes a read of gas 0 through mvfxGpuSim_AddGasRead into a buffer of
// bytes and reads it back into out.
static bool ReadBytes(Gpu* gpu, mvfxGpuSim* sim, const mvfxGasRead* read, uint64_t bytes, void* out)
{
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = bytes;
    bufferDef.usage = mrhi_bufferStorage | mrhi_bufferCopySource;
    mrhiBufferId buffer = {0};
    if (mrhiCreateBuffer(gpu->device, &bufferDef, &buffer) != mrhi_success)
    {
        return false;
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId target = {0};
    mrhiPassId pass = {0};
    mrhiRequestId request = {0};
    bool recorded = mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success &&
                    mrhiImportBuffer(gpu->device, buffer, &target) == mrhi_success;
    const mrhiAccess taken = {.resource = target, .kind = mrhi_accessCopySource};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = &taken;
    def.accessCount = 1;
    def.neverCull = true;
    mvfxGpuGasReadDef readDef = mvfxDefaultGpuGasReadDef();
    readDef.read = *read;
    readDef.target = target;
    readDef.bytes = bytes;
    recorded = recorded && mvfxGpuSim_AddGasRead(sim, &readDef) == mvfx_success &&
               mrhiAddPass(gpu->device, &def, &pass) == mrhi_success &&
               mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
               mvfxGpuSim_Record(sim) == mvfx_success &&
               mrhiPass_Begin(gpu->device, pass) == mrhi_success &&
               mrhiPass_ReadBuffer(gpu->device, pass, target, 0, bytes, &request) == mrhi_success &&
               mrhiPass_End(gpu->device, pass) == mrhi_success;
    if (!recorded)
    {
        (void)mrhiDevice_DropFrame(gpu->device);
        (void)mrhiDestroyBuffer(gpu->device, buffer);
        return false;
    }
    size_t got = 0;
    bool done = Finish(gpu) &&
                mrhiRequest_TakeReadback(gpu->device, request, out, (size_t)bytes, &got) == mrhi_success &&
                got == bytes;
    (void)mrhiDestroyBuffer(gpu->device, buffer);
    return done;
}

// The samples' points and each path's samples.
static float s_points[3 * GAS_SAMPLE_POINTS];
static double s_world[3 * GAS_SAMPLE_POINTS];
static float s_cpuSamples[GAS_SAMPLE_POINTS];
static float s_gpuSamples[GAS_SAMPLE_POINTS];

static void TestReads(Gpu* gpu, mvfxGpu* kernels)
{
    // After a few steps, a box of the distance in 16-bit units over the
    // reach's span on both paths, each value within a unit, and the
    // distance sampled at points; a surface's other fields (read or
    // sampled) and a read past the box refused.
    mvfxEmitterDef emitter = Drops();
    mvfxGasDef gas = Wrap(24, 24, 16);
    mvfxEffectId effect = {0};
    const Run run = {.emitters = &emitter, .emitterCount = 1, .gases = &gas, .gasCount = 1};
    mvfxGpuSim* sim = OpenRun(kernels, &run, &effect);
    mvfxContext* context = RunContext();
    bool stepped = sim != NULL;
    LongFrames(true);
    for (int32_t s = 0; stepped && s < 20; ++s)
    {
        stepped = mvfxContext_StepTicks(context, StepTicks()) == mvfx_success &&
                  Simulate(gpu, sim) && ReportGases(gpu, sim);
        LongFrames(false);
    }
    float reach = gas.surface.reach * gas.surface.radius;
    const mvfxGasRead read = {.field = mvfx_gasDistance,
                              .format = mvfx_formatUnorm16,
                              .first = {4, 2, 3},
                              .size = {16, 12, 9},
                              .low = -reach,
                              .high = reach};
    uint64_t bytes = 2u * 16u * 12u * 9u;
    bool read16 = stepped &&
                  mvfxInstance_ReadGasAs(context, CpuInstance(), 0, &read, s_cpuBytes, bytes) ==
                      mvfx_success &&
                  ReadBytes(gpu, sim, &read, bytes, s_gpuBytes);
    int32_t most = 0;
    for (uint64_t i = 0; read16 && i < bytes; i += 2)
    {
        int32_t cpu = s_cpuBytes[i] | s_cpuBytes[i + 1] << 8;
        int32_t gpuValue = s_gpuBytes[i] | s_gpuBytes[i + 1] << 8;
        most = abs(cpu - gpuValue) > most ? abs(cpu - gpuValue) : most;
    }
    printf("a surface's distance in 16-bit units: at most %d apart\n", most);
    CHECK(read16 && most <= 1, "a surface's distance read in 16-bit units");
    // Its distance sampled about the box on both paths, the reach past its
    // faces.
    mvfxGasInfo info;
    bool laid = mvfxInstance_GetGasInfo(context, CpuInstance(), 0, &info) == mvfx_success;
    const double origin[3] = {info.center[0] - 0.5, info.center[1] + 0.25, info.center[2]};
    int32_t inside = laid ? LayGasPoints(&info, origin, s_points, s_world) : 0;
    bool sampled = laid &&
                   mvfxInstance_SampleGas(context, CpuInstance(), 0, mvfx_gasDistance, s_world,
                                          GAS_SAMPLE_POINTS, s_cpuSamples) == mvfx_success &&
                   SampleGasGpu(gpu, sim, 0, mvfx_gasDistance, origin, s_points, GAS_SAMPLE_POINTS,
                                s_gpuSamples);
    float worst = 0.0f;
    int32_t reached = 0;
    for (int32_t i = 0; sampled && i < GAS_SAMPLE_POINTS; ++i)
    {
        worst = fmaxf(worst,
                      fabsf(s_gpuSamples[i] - s_cpuSamples[i]) / (1.0f + fabsf(s_cpuSamples[i])));
        reached += s_gpuSamples[i] == reach && s_cpuSamples[i] == reach ? 1 : 0;
    }
    printf("a surface sampled: %d of %d points in the box, %d at the reach, largest relative "
           "difference %g\n",
           inside, GAS_SAMPLE_POINTS, reached, (double)worst);
    CHECK(sampled && worst <= 1e-4f && reached >= GAS_SAMPLE_POINTS - inside &&
              reached < GAS_SAMPLE_POINTS,
          "a surface's distance sampled");
    mvfxGpuGasReadDef def = mvfxDefaultGpuGasReadDef();
    def.target = (mrhiResourceId){.index1 = 1};
    def.bytes = 1u << 20;
    def.read.size[0] = 4;
    def.read.size[1] = 4;
    def.read.size[2] = 4;
    bool refused = true;
    for (int32_t f = mvfx_gasDensity; f <= mvfx_gasVelocityZ; ++f)
    {
        def.read.field = (mvfxGasField)f;
        refused = refused && mvfxGpuSim_AddGasRead(sim, &def) == mvfx_errorInvalid;
    }
    def.read.field = mvfx_gasDistance;
    def.read.first[0] = 21;
    refused = refused && mvfxGpuSim_AddGasRead(sim, &def) == mvfx_errorInvalid;
    mvfxGpuGasSamplesDef query = mvfxDefaultGpuGasSamplesDef();
    query.points = (mrhiResourceId){.index1 = 1};
    query.target = (mrhiResourceId){.index1 = 2};
    refused = refused && mvfxGpuSim_AddGasSamples(sim, &query) == mvfx_errorInvalid;
    CHECK(refused, "a surface's other fields and a read past its box refused");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
}

// The most a test mesh has, each path's mesh, and the device's field.
#define MESH_VERTICES (1 << 16)
#define MESH_INDICES  (1 << 18)
#define MESH_CELLS    (32 * 32 * 16)
#define MARK          0xFFFFFFFFu

typedef struct Mesh
{
    float positions[3 * MESH_VERTICES];
    float normals[3 * MESH_VERTICES];
    uint32_t indices[MESH_INDICES];
} Mesh;

static Mesh s_cpuMesh;
static Mesh s_gpuMesh;
static float s_field[MESH_CELLS];
static unsigned char s_meshScratch[1 << 16];

// The device's distances read back whole and written into the CPU
// instance's state, so both paths mesh the same values.
static bool AdoptField(Gpu* gpu, mvfxGpuSim* sim, const mvfxGasDef* gas)
{
    const int32_t* n = gas->size;
    int64_t cells = (int64_t)n[0] * n[1] * n[2];
    const mvfxGasRead read = {
        .field = mvfx_gasDistance, .format = mvfx_formatFloat32, .size = {n[0], n[1], n[2]}};
    mvfxResult found = mvfx_success;
    mvfxInstance* live = (mvfxInstance*)mvfxFindGas(RunContext(), CpuInstance(), 0, &found);
    if (cells > MESH_CELLS || live == NULL ||
        !ReadBytes(gpu, sim, &read, 4u * (uint64_t)cells, s_field))
    {
        return false;
    }
    mvfxGasState* state = &live->gases[0];
    memcpy(state->scalars[0][state->current], s_field, (size_t)cells * sizeof(float));
    return true;
}

// The bytes before each of the device's buffers, so the offsets count.
static const uint64_t s_meshOffsets[4] = {8, 16, 12, 4};

// Meshes the planned instance's surface into buffers of the def's
// capacities (each past its offset) and reads them back into s_gpuMesh and
// counts.
static bool MeshGpu(Gpu* gpu, mvfxGpuSim* sim, mvfxGpuSurfaceMeshDef def,
                    uint32_t counts[MVFX_GPU_SURFACE_MESH_COUNTS])
{
    const uint64_t bytes[4] = {12u * (uint64_t)def.vertexCapacity,
                               12u * (uint64_t)def.vertexCapacity, 4u * (uint64_t)def.indexCapacity,
                               4u * MVFX_GPU_SURFACE_MESH_COUNTS};
    void* outs[4] = {s_gpuMesh.positions, s_gpuMesh.normals, s_gpuMesh.indices, counts};
    mrhiBufferId buffers[4] = {{0}, {0}, {0}, {0}};
    mrhiResourceId ids[4] = {{0}, {0}, {0}, {0}};
    mrhiRequestId requests[4] = {{0}, {0}, {0}, {0}};
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.usage = mrhi_bufferStorage | mrhi_bufferCopySource;
    bool made = true;
    for (int32_t b = 0; made && b < 4; ++b)
    {
        bufferDef.size = s_meshOffsets[b] + (bytes[b] > 0 ? bytes[b] : 4u);
        made = mrhiCreateBuffer(gpu->device, &bufferDef, &buffers[b]) == mrhi_success;
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    bool recorded = made && mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success;
    for (int32_t b = 0; recorded && b < 4; ++b)
    {
        recorded = mrhiImportBuffer(gpu->device, buffers[b], &ids[b]) == mrhi_success;
    }
    def.positions = ids[0];
    def.positionOffset = s_meshOffsets[0];
    def.normals = ids[1];
    def.normalOffset = s_meshOffsets[1];
    def.indices = ids[2];
    def.indexOffset = s_meshOffsets[2];
    def.counts = ids[3];
    def.countsOffset = s_meshOffsets[3];
    const mrhiAccess taken[4] = {{.resource = ids[0], .kind = mrhi_accessCopySource},
                                 {.resource = ids[1], .kind = mrhi_accessCopySource},
                                 {.resource = ids[2], .kind = mrhi_accessCopySource},
                                 {.resource = ids[3], .kind = mrhi_accessCopySource}};
    mrhiPassDef passDef = mrhiDefaultPassDef();
    passDef.passClass = mrhi_passTransfer;
    passDef.accesses = taken;
    passDef.accessCount = 4;
    passDef.neverCull = true;
    mrhiPassId pass = {0};
    recorded = recorded && mvfxGpuSim_AddSurfaceMesh(sim, &def) == mvfx_success &&
               mrhiAddPass(gpu->device, &passDef, &pass) == mrhi_success &&
               mrhiDevice_CompileFrame(gpu->device) == mrhi_success &&
               mvfxGpuSim_Record(sim) == mvfx_success &&
               mrhiPass_Begin(gpu->device, pass) == mrhi_success;
    for (int32_t b = 0; recorded && b < 4; ++b)
    {
        recorded = bytes[b] == 0 || mrhiPass_ReadBuffer(gpu->device, pass, ids[b], s_meshOffsets[b],
                                                   bytes[b], &requests[b]) == mrhi_success;
    }
    recorded = recorded && mrhiPass_End(gpu->device, pass) == mrhi_success;
    bool done = recorded && Finish(gpu);
    for (int32_t b = 0; done && b < 4; ++b)
    {
        size_t got = 0;
        done = bytes[b] == 0 || (mrhiRequest_TakeReadback(gpu->device, requests[b], outs[b],
                                                  (size_t)bytes[b], &got) == mrhi_success &&
                                 got == bytes[b]);
    }
    if (made && !recorded)
    {
        (void)mrhiDevice_DropFrame(gpu->device);
    }
    for (int32_t b = 0; b < 4; ++b)
    {
        if (buffers[b].index1 != 0)
        {
            (void)mrhiDestroyBuffer(gpu->device, buffers[b]);
        }
    }
    return done;
}

// Whether the device's mesh of a level is the CPU path's from the same
// distances, into buffers of the capacities: the counts (the indices
// written as the CPU path's, which stops at the first primitive that does
// not fit) and the indices exact, places and normals relative to one more
// than their size within the tolerance. Its counts into counts.
static bool SameMesh(Gpu* gpu, mvfxGpuSim* sim, float level, int64_t vertexCapacity,
                     int64_t indexCapacity, uint32_t counts[MVFX_GPU_SURFACE_MESH_COUNTS])
{
    mvfxGasInfo info;
    int64_t scratch = 0;
    if (mvfxInstance_GetGasInfo(RunContext(), CpuInstance(), 0, &info) != mvfx_success ||
        mvfxInstance_GetSurfaceMeshScratchBytes(RunContext(), CpuInstance(), 0, &scratch) !=
            mvfx_success ||
        scratch > (int64_t)sizeof s_meshScratch)
    {
        return false;
    }
    const double origin[3] = {info.center[0] + 0.25, info.center[1] - 0.5, info.center[2] + 1.0};
    memset(s_cpuMesh.indices, 0xFF, sizeof s_cpuMesh.indices);
    const mvfxSurfaceMeshBuffers buffers = {.positions = s_cpuMesh.positions,
                                            .normals = s_cpuMesh.normals,
                                            .vertexCapacity = vertexCapacity,
                                            .indices = s_cpuMesh.indices,
                                            .indexCapacity = indexCapacity,
                                            .scratch = s_meshScratch,
                                            .scratchBytes = scratch};
    int64_t vertices = 0;
    int64_t indices = 0;
    mvfxGpuSurfaceMeshDef def = mvfxDefaultGpuSurfaceMeshDef();
    def.level = level;
    memcpy(def.origin, origin, sizeof def.origin);
    def.vertexCapacity = vertexCapacity;
    def.indexCapacity = indexCapacity;
    if (mvfxInstance_WriteSurfaceMesh(RunContext(), CpuInstance(), 0, origin, level, &buffers,
                                      &vertices, &indices) != mvfx_success ||
        !MeshGpu(gpu, sim, def, counts))
    {
        printf("  the mesh not made\n");
        return false;
    }
    int64_t written = 0;
    while (written < indexCapacity && s_cpuMesh.indices[written] != MARK)
    {
        ++written;
    }
    int64_t kept = vertices < vertexCapacity ? vertices : vertexCapacity;
    bool same = counts[0] == (uint32_t)vertices && counts[1] == (uint32_t)indices &&
                counts[2] == (uint32_t)kept && counts[3] == (uint32_t)written;
    if (!same)
    {
        printf("  counts: GPU %u %u %u %u, CPU %lld %lld %lld %lld\n", counts[0], counts[1],
               counts[2], counts[3], (long long)vertices, (long long)indices, (long long)kept,
               (long long)written);
    }
    for (int64_t i = 0; same && i < written; ++i)
    {
        same = s_gpuMesh.indices[i] == s_cpuMesh.indices[i];
        if (!same)
        {
            printf("  index %lld: %u on the GPU, %u on the CPU\n", (long long)i,
                   s_gpuMesh.indices[i], s_cpuMesh.indices[i]);
        }
    }
    float worst = 0.0f;
    for (int64_t i = 0; same && i < 3 * kept; ++i)
    {
        float place = fabsf(s_gpuMesh.positions[i] - s_cpuMesh.positions[i]) /
                      (1.0f + fabsf(s_cpuMesh.positions[i]));
        float normal = fabsf(s_gpuMesh.normals[i] - s_cpuMesh.normals[i]);
        worst = fmaxf(worst, fmaxf(place, normal));
        same = place <= 1e-4f && normal <= 1e-4f;
        if (!same)
        {
            printf("  vertex word %lld apart: place %g, normal %g\n", (long long)i, (double)place,
                   (double)normal);
        }
    }
    printf("  level %g: %lld vertices, %lld indices (%lld and %lld written), largest relative "
           "difference %g\n",
           (double)level, (long long)vertices, (long long)indices, (long long)kept,
           (long long)written, (double)worst);
    return same;
}

// A level a sample lies at exactly: the value of the sample nearest the
// surface with a lower neighbour along X, after it (step 1) or before it
// (step -1), so the walk meets the sample's own vertex through an edge
// from it or one into it.
static float SampleLevel(const mvfxGasDef* gas, int32_t step)
{
    const int32_t* n = gas->size;
    int64_t cells = (int64_t)n[0] * n[1] * n[2];
    float level = 0.0f;
    float nearest = INFINITY;
    for (int64_t i = 1; i + 1 < cells; ++i)
    {
        int64_t j = i + step;
        bool row = step > 0 ? i % n[0] != n[0] - 1 : i % n[0] != 0;
        if (row && s_field[j] < s_field[i] && fabsf(s_field[i]) < nearest)
        {
            nearest = fabsf(s_field[i]);
            level = s_field[i];
        }
    }
    return level;
}

// A sample's part of a level's mesh: into edges, its edges toward +X, +Y
// and +Z crossing the level with neither end at it; and whether it has its
// own vertex (it lies at the level, an edge from it crossing it).
static bool SampleCounts(const mvfxGasDef* gas, float level, const int32_t p[3], int64_t* edges)
{
    const int32_t* n = gas->size;
    const int64_t step[3] = {1, n[0], (int64_t)n[0] * n[1]};
    int64_t i = ((int64_t)p[2] * n[1] + p[1]) * n[0] + p[0];
    float a = s_field[i] - level;
    bool own = false;
    for (int32_t k = 0; k < (n[2] > 1 ? 3 : 2); ++k)
    {
        if (p[k] + 1 < n[k])
        {
            float up = s_field[i + step[k]] - level;
            *edges += a != 0.0f && up != 0.0f && (a < 0.0f) != (up < 0.0f) ? 1 : 0;
            own = own || (a == 0.0f && up < 0.0f);
        }
        own = own || (a == 0.0f && p[k] > 0 && s_field[i - step[k]] - level < 0.0f);
    }
    return own;
}

// The vertices of a level's mesh that are cubes' centers: the CPU path's
// vertices less those on edges and at samples.
static int64_t CenterCount(const mvfxGasDef* gas, float level)
{
    const int32_t* n = gas->size;
    int64_t made = 0;
    for (int64_t i = 0; i < (int64_t)n[0] * n[1] * n[2]; ++i)
    {
        const int32_t p[3] = {(int32_t)(i % n[0]), (int32_t)(i / n[0] % n[1]),
                              (int32_t)(i / ((int64_t)n[0] * n[1]))};
        bool own = SampleCounts(gas, level, p, &made);
        made += own ? 1 : 0;
    }
    int64_t vertices = 0;
    int64_t indices = 0;
    const mvfxSurfaceMeshBuffers none = {.scratch = s_meshScratch,
                                         .scratchBytes = (int64_t)sizeof s_meshScratch};
    const double origin[3] = {0.0, 0.0, 0.0};
    if (mvfxInstance_WriteSurfaceMesh(RunContext(), CpuInstance(), 0, origin, level, &none,
                                      &vertices, &indices) != mvfx_success)
    {
        return -1;
    }
    return vertices - made;
}

// A level whose mesh has a cube's center (a loop no triangles close off
// the cube's faces), searched over the reach's span; NAN for none.
static float CenterLevel(const mvfxGasDef* gas)
{
    float reach = gas->surface.reach * gas->surface.radius;
    for (int32_t j = 0; j < 400; ++j)
    {
        float level = -reach + 2.0f * reach * ((float)j + 0.37f) / 400.0f;
        if (CenterCount(gas, level) > 0)
        {
            return level;
        }
    }
    return NAN;
}

// Steps drops wrapped by a gas in an instance at place, turned and
// scaled, moved on by a third of a world unit along X and Z from half the
// steps; then meshes it on both paths from the device's distances: at the
// surface, off it, at two samples' own values, at the reach (where every
// sample far from the drops lies, so many primitives repeat a vertex), at
// a level with a cube's center (3D), and into buffers too small for the
// vertices, then for the indices; the surface has at least least
// vertices.
static void Meshed(Gpu* gpu, mvfxGpu* kernels, const mvfxGasDef* gas, const double place[3],
                   float scale, uint32_t least, const char* what)
{
    mvfxEmitterDef emitter = Drops();
    mvfxEffectId effect = {0};
    const Run run = {.emitters = &emitter, .emitterCount = 1, .gases = gas, .gasCount = 1};
    mvfxGpuSim* sim = OpenRun(kernels, &run, &effect);
    mvfxContext* context = RunContext();
    const float turn[4] = {0.0f, sinf(0.3f), 0.0f, cosf(0.3f)};
    const double later[3] = {place[0] + 0.3, place[1], place[2] + 0.3};
    bool stepped = sim != NULL;
    LongFrames(true);
    for (int32_t s = 0; stepped && s < 20; ++s)
    {
        const double* at = s < 10 ? place : later;
        stepped = (s != 0 && s != 10) ||
                  (mvfxInstance_SetTransform(context, CpuInstance(), at, turn, scale, s == 0) ==
                       mvfx_success &&
                   mvfxInstance_SetTransform(context, PlannedInstance(), at, turn, scale, s == 0) ==
                       mvfx_success);
        stepped = stepped && mvfxContext_StepTicks(context, StepTicks()) == mvfx_success &&
                  Simulate(gpu, sim) && ReportGases(gpu, sim);
        LongFrames(false);
    }
    bool same = stepped && AdoptField(gpu, sim, gas);
    printf("%s\n", what);
    uint32_t whole[MVFX_GPU_SURFACE_MESH_COUNTS] = {0, 0, 0, 0};
    uint32_t counts[MVFX_GPU_SURFACE_MESH_COUNTS] = {0, 0, 0, 0};
    same = same && SameMesh(gpu, sim, 0.0f, MESH_VERTICES, MESH_INDICES, whole) &&
           SameMesh(gpu, sim, 0.01f, MESH_VERTICES, MESH_INDICES, counts) &&
           SameMesh(gpu, sim, SampleLevel(gas, 1), MESH_VERTICES, MESH_INDICES, counts) &&
           SameMesh(gpu, sim, SampleLevel(gas, -1), MESH_VERTICES, MESH_INDICES, counts) &&
           SameMesh(gpu, sim, gas->surface.reach * gas->surface.radius, MESH_VERTICES, MESH_INDICES,
                    counts) &&
           (gas->size[2] == 1 || !isnan(CenterLevel(gas))) &&
           (gas->size[2] == 1 ||
            SameMesh(gpu, sim, CenterLevel(gas), MESH_VERTICES, MESH_INDICES, counts)) &&
           SameMesh(gpu, sim, 0.0f, whole[0] / 2, MESH_INDICES, counts) &&
           SameMesh(gpu, sim, 0.0f, MESH_VERTICES, whole[1] / 3 + 1, counts);
    CHECK(same && whole[0] >= least && whole[3] == whole[1], what);
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
}

static void TestMesh(Gpu* gpu, mvfxGpu* kernels)
{
    // A box far off, turned and doubled, so its map out of its frame counts;
    // a 2D box where it began.
    const double far[3] = {1000.0, 20.0, -500.0};
    const double near[3] = {0.0, 0.0, 0.0};
    mvfxGasDef gas = Wrap(24, 24, 16);
    Meshed(gpu, kernels, &gas, far, 2.0f, 100, "a surface far off meshed on both paths");
    gas = Wrap(32, 32, 1);
    Meshed(gpu, kernels, &gas, near, 1.0f, 20, "a 2D surface meshed on both paths");
}

static void TestMeshRefusals(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Drops();
    mvfxGasDef gas = Wrap(8, 8, 8);
    mvfxEffectId effect = {0};
    const Run run = {.emitters = &emitter, .emitterCount = 1, .gases = &gas, .gasCount = 1};
    mvfxGpuSim* sim = OpenRun(kernels, &run, &effect);
    mvfxGpuSurfaceMeshDef good = mvfxDefaultGpuSurfaceMeshDef();
    good.positions = (mrhiResourceId){.index1 = 1};
    good.indices = (mrhiResourceId){.index1 = 2};
    good.vertexCapacity = 16;
    good.indexCapacity = 48;
    mvfxGpuSurfaceMeshDef bad[14];
    for (int32_t b = 0; b < 14; ++b)
    {
        bad[b] = good;
    }
    bad[0].cookie = 0;
    bad[1].gas = 1;
    bad[2].gas = -1;
    bad[3].level = (float)NAN;
    bad[4].origin[2] = 0x1p53;
    bad[5].positions = (mrhiResourceId){0};
    bad[6].indices = (mrhiResourceId){0};
    bad[7].vertexCapacity = -1;
    bad[8].indexCapacity = -1;
    bad[9].positionOffset = 2;
    bad[10].normals = (mrhiResourceId){.index1 = 3};
    bad[10].normalOffset = 6;
    bad[11].indexOffset = 4u * (uint64_t)UINT32_MAX;
    bad[12].counts = (mrhiResourceId){.index1 = 4};
    bad[12].countsOffset = 10;
    bad[13].positionOffset = 4u * ((uint64_t)UINT32_MAX - 40u);
    bool refused = mvfxGpuSim_AddSurfaceMesh(NULL, &good) == mvfx_errorInvalid &&
                   mvfxGpuSim_AddSurfaceMesh(sim, NULL) == mvfx_errorInvalid;
    for (int32_t b = 0; b < 14; ++b)
    {
        bool one = mvfxGpuSim_AddSurfaceMesh(sim, &bad[b]) == mvfx_errorInvalid;
        if (!one)
        {
            printf("  spoiled mesh %d taken\n", b);
        }
        refused = refused && one;
    }
    CHECK(sim != NULL && refused, "spoiled surface meshes refused");
    // A frame's meshes past MVFX_GPU_SURFACE_MESHES.
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 256;
    bufferDef.usage = mrhi_bufferStorage;
    mrhiBufferId buffers[2] = {{0}, {0}};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    bool opened = sim != NULL &&
                  mrhiCreateBuffer(gpu->device, &bufferDef, &buffers[0]) == mrhi_success &&
                  mrhiCreateBuffer(gpu->device, &bufferDef, &buffers[1]) == mrhi_success &&
                  mrhiDevice_BeginFrame(gpu->device, &frame) == mrhi_success;
    bool taken = opened &&
                 mrhiImportBuffer(gpu->device, buffers[0], &good.positions) == mrhi_success &&
                 mrhiImportBuffer(gpu->device, buffers[1], &good.indices) == mrhi_success;
    for (int32_t m = 0; taken && m < MVFX_GPU_SURFACE_MESHES; ++m)
    {
        taken = mvfxGpuSim_AddSurfaceMesh(sim, &good) == mvfx_success;
    }
    CHECK(taken && mvfxGpuSim_AddSurfaceMesh(sim, &good) == mvfx_errorLimit,
          "a surface's meshes past the most a frame");
    if (opened)
    {
        (void)mrhiDevice_DropFrame(gpu->device);
    }
    for (int32_t b = 0; b < 2; ++b)
    {
        if (buffers[b].index1 != 0)
        {
            (void)mrhiDestroyBuffer(gpu->device, buffers[b]);
        }
    }
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestPasses(kernels);
    TestWrapped(gpu, kernels);
    TestPlanar(gpu, kernels);
    TestBeyond(gpu, kernels);
    TestMoving(gpu, kernels);
    TestCleared(gpu, kernels);
    TestOmitted(gpu, kernels);
    TestFinishing(gpu, kernels);
    TestReads(gpu, kernels);
    TestMesh(gpu, kernels);
    TestMeshRefusals(gpu, kernels);
}

int main(void)
{
    return RunSuite(Suite, &s_failures);
}
