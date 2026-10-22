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
// the refusals.
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
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
    bool recorded = mrhiBeginFrame(gpu->device, &frame) == mrhi_success &&
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
               mrhiCompileFrame(gpu->device) == mrhi_success &&
               mvfxGpuSim_Record(sim) == mvfx_success &&
               mrhiBeginPass(gpu->device, pass) == mrhi_success &&
               mrhiReadBuffer(gpu->device, pass, target, 0, bytes, &request) == mrhi_success &&
               mrhiEndPass(gpu->device, pass) == mrhi_success;
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

static void TestReads(Gpu* gpu, mvfxGpu* kernels)
{
    // After a few steps, a box of the distance in 16-bit units over the
    // reach's span on both paths, each value within a unit; a surface's
    // other fields and a read past the box refused.
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
    CHECK(refused, "a surface's other fields and a read past its box refused");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
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
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
