// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Water surfaces on a real device (lavapipe on CI) beside the CPU path,
// compared after every step through the harness's runs: each field the
// GPU writes through mvfxGpuSim_AddWaterRead against
// mvfxInstance_ReadWater's, relative to its size. Waves: the host's
// pushes, sinking and lifting, one past an edge; closed walls, open ones
// and both, every corner's chain of open edges; a grid of two cells
// across; a moving instance and a surface where it began; a clear;
// frames dropped; two surfaces; a hitch slowed past the most substeps.
// Shallow water: a basin filled and pushed; springs on a terrain of the
// effect's, level and leaning, and on the instance's, two walls open; a
// clear; frames dropped; a moving instance. Both beside a liquid gas
// splitting the frames; the record read back and reported, a stopped
// instance finishing; reads in every format over a box with padded rows,
// the padding kept; the refusals.
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "rhi_harness.h"
#include "rhi_waters.h"
#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// A sprite beside the surface, a short burst: an effect has an emitter.
static mvfxEmitterDef Beside(void)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.rate = 0.0f;
    return emitter;
}

// A pond of x by z cells of h about the instance's origin, waves at speed
// a second.
static mvfxWaterDef Pond(int32_t x, int32_t z, float h, float speed, uint8_t closed)
{
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = x;
    water.size[1] = z;
    water.cellSize = h;
    water.center[1] = 0.25f;
    water.waveSpeed = speed;
    water.damping = 0.3f;
    water.closedWalls = closed;
    return water;
}

// Runs a pond beside a sprite, compared after every step.
static void Ponds(Gpu* gpu, mvfxGpu* kernels, const mvfxWaterDef* waters, int32_t count, Run run,
                  const char* what)
{
    mvfxEmitterDef emitter = Beside();
    run.emitters = &emitter;
    run.emitterCount = 1;
    run.least = -1;
    run.waters = waters;
    run.waterCount = count;
    Compare(gpu, kernels, &run, what);
}

static void TestWaves(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxWaterDef water = Pond(48, 40, 0.1f, 1.5f, 15);
    Ponds(gpu, kernels, &water, 1, (Run){0}, "a pond, closed");
    // Open but for its min Z wall: its max X corners' chains through the
    // open edges before them.
    water.closedWalls = mvfx_waterWallMinZ;
    Ponds(gpu, kernels, &water, 1, (Run){0}, "a pond open but for its min Z wall");
    water.closedWalls = 0;
    Ponds(gpu, kernels, &water, 1, (Run){0}, "a pond, open");
    // Two cells across, every wall open: each edge cell's inward one an
    // edge's too.
    mvfxWaterDef strip = Pond(2, 7, 0.5f, 1.0f, 0);
    Ponds(gpu, kernels, &strip, 1, (Run){0}, "a strip two cells across, open");
    strip.closedWalls = mvfx_waterWallMaxX | mvfx_waterWallMinZ;
    Ponds(gpu, kernels, &strip, 1, (Run){0}, "a strip two cells across, half open");
    // The instance moving and turning: the pushes mapped into its frame,
    // or into where it began.
    water.closedWalls = mvfx_waterWallMinX;
    Ponds(gpu, kernels, &water, 1, (Run){.moving = true}, "a pond on a moving instance");
    water.space = mvfx_spaceWorld;
    Ponds(gpu, kernels, &water, 1, (Run){.moving = true}, "a pond where a moving instance began");
    water.space = mvfx_spaceLocal;
    Ponds(gpu, kernels, &water, 1, (Run){.clearing = true}, "a pond cleared at half time");
    Ponds(gpu, kernels, &water, 1, (Run){.dropping = true}, "a pond's frames dropped once");
    const mvfxWaterDef two[2] = {water, Pond(24, 30, 0.15f, 0.8f, 5)};
    Ponds(gpu, kernels, two, 2, (Run){0}, "two ponds");
}

static void TestHitch(Gpu* gpu, mvfxGpu* kernels)
{
    // Steps of a tenth of a second at a wave speed of 50: 79 substeps of
    // 0.9 h / (c sqrt 2) a step, past the most (64), each step slowed.
    mvfxWaterDef water = Pond(32, 32, 0.1f, 50.0f, 3);
    water.damping = 2.0f;
    SetStepTicks(MVFX_TICKS_PER_SECOND / 10);
    Ponds(gpu, kernels, &water, 1, (Run){.steps = 12}, "a pond slowed");
    SetStepTicks(STEP_TICKS);
}

// A basin of shallow water: x by z cells of h about the instance's origin,
// its walls closed, filled to level over a flat floor, its flows slowed.
static mvfxWaterDef Basin(int32_t x, int32_t z, float h, float level)
{
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.model = mvfx_waterShallow;
    water.size[0] = x;
    water.size[1] = z;
    water.cellSize = h;
    water.center[1] = 0.25f;
    water.closedWalls = 15;
    water.fillLevel = level;
    water.friction = 0.2f;
    return water;
}

// Heights over 9 by 9 points: a slope down along X with a ridge across it.
static float s_heights[81];

static mvfxGridDef Ground(void)
{
    for (int32_t p = 0; p < 81; ++p)
    {
        float x = (float)(p % 9) / 8.0f;
        float z = (float)(p / 9) / 8.0f;
        s_heights[p] = 0.6f * (1.0f - x) + 0.3f * expf(-40.0f * (z - 0.5f) * (z - 0.5f));
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 9;
    def.size[2] = 9;
    def.values = s_heights;
    return def;
}

// A terrain over the grid's middle, the run's first grid, turned about Z
// by turn radians.
static mvfxCollider Terrain(float turn)
{
    return (mvfxCollider){.kind = mvfx_colliderHeightfield,
                          .space = 1,
                          .center = {0.1f, -0.3f, 0},
                          .halfExtents = {1.4f, 0.4f, 1.2f},
                          .rotation = {0, 0, sinf(0.5f * turn), cosf(0.5f * turn)},
                          .grid = {.index1 = 1}};
}

// A spring of a sphere's footprint and one of a box's.
static const mvfxWaterSource s_springs[2] = {
    {.shape = mvfx_gasSphere, .center = {-0.6f, 0, 0.4f}, .radius = 0.3f, .rate = 0.4f},
    {.shape = mvfx_gasBox,
     .center = {0.5f, 0, -0.5f},
     .halfExtents = {0.25f, 0.1f, 0.15f},
     .rate = 0.3f}};

static void TestShallow(Gpu* gpu, mvfxGpu* kernels)
{
    // A basin filled and pushed, sinking and lifting, a push's ring past
    // an edge.
    mvfxWaterDef water = Basin(32, 28, 0.1f, 0.2f);
    Ponds(gpu, kernels, &water, 1, (Run){0}, "a basin pushed");
    // A terrain of the effect's sloping down into the basin, two springs
    // pouring, its far walls open: water runs downhill and out; finer
    // cells, more substeps. Its fast flow draws the paths' roundings apart
    // over the run (1.1e-4 at the last steps): the liquids' tolerance.
    const mvfxGridDef ground = Ground();
    water = Basin(48, 40, 0.06f, 0.05f);
    water.closedWalls = mvfx_waterWallMinX | mvfx_waterWallMinZ;
    water.hasTerrain = true;
    water.terrain = Terrain(0.0f);
    water.sources = s_springs;
    water.sourceCount = 2;
    Run run = {.colliderGrids = &ground, .colliderGridCount = 1, .tolerance = 1e-3f};
    Ponds(gpu, kernels, &water, 1, run, "springs on a terrain, two walls open");
    // The terrain leaning: each cell's ground where its vertical meets it.
    water.terrain = Terrain(0.3f);
    Ponds(gpu, kernels, &water, 1, run, "springs on a leaning terrain");
    // The instance's own heightfield as the terrain.
    const mvfxCollider host = Terrain(0.0f);
    water.terrain = (mvfxCollider){0};
    water.hasTerrain = false;
    water.hostTerrain = true;
    run.hostColliders = &host;
    run.hostColliderCount = 1;
    run.hostGridBytes = 4 * 81;
    Ponds(gpu, kernels, &water, 1, run, "springs on the instance's terrain");
    // A basin cleared at half time and filled again; its frames dropped
    // once; where a moving instance began.
    water = Basin(32, 28, 0.1f, 0.2f);
    Ponds(gpu, kernels, &water, 1, (Run){.clearing = true}, "a basin cleared at half time");
    Ponds(gpu, kernels, &water, 1, (Run){.dropping = true}, "a basin's frames dropped once");
    water.space = mvfx_spaceWorld;
    Ponds(gpu, kernels, &water, 1, (Run){.moving = true}, "a basin where a moving instance began");
}

static const mvfxCollider s_tank = {.kind = mvfx_colliderBox,
                                    .inside = true,
                                    .halfExtents = {0.8f, 0.8f, 1.0f},
                                    .rotation = {0, 0, 0, 1}};

static void TestSplit(Gpu* gpu, mvfxGpu* kernels)
{
    // A liquid gas of a courant number of 0.2 beside a pond and a basin:
    // every frame splits in two or more steps, each with the surfaces'
    // steps after the gas's; then each frame dropped once first.
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.kind = mvfx_gasLiquid;
    gas.size[0] = 16;
    gas.size[1] = 16;
    gas.size[2] = 1;
    gas.cellSize = 0.1f;
    gas.closedWalls = 63;
    gas.courant = 0.2f;
    mvfxEmitterDef emitter = Sprite();
    emitter.capacity = 512;
    emitter.rate = 300.0f;
    emitter.burstCount = 0;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.shape.radius = 0.1f;
    emitter.velocity.direction[1] = -1.0f;
    emitter.velocity.speed = (mvfxRange){1.0f, 1.5f};
    emitter.motion.drag = 0.0f;
    emitter.colliders = &s_tank;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enabled = true};
    emitter.liquid = mvfxDefaultEmitterDef().liquid;
    emitter.liquid.enabled = true;
    emitter.liquid.solver = mvfx_liquidGrid;
    emitter.liquid.gas = 0;
    emitter.liquid.spacing = 0.05f;
    const mvfxWaterDef waters[2] = {Pond(40, 40, 0.1f, 1.5f, mvfx_waterWallMaxZ),
                                    Basin(32, 28, 0.1f, 0.2f)};
    Run run = {.mostApart = 4,
               .emitters = &emitter,
               .emitterCount = 1,
               .gases = &gas,
               .gasCount = 1,
               .waters = waters,
               .waterCount = 2,
               .adopting = true,
               .tolerance = 1e-3f};
    Compare(gpu, kernels, &run, "a pond and a basin beside a liquid gas, in steps");
    run.dropping = true;
    Compare(gpu, kernels, &run, "a pond and a basin beside a liquid gas, frames dropped");
}

static void TestFinishing(Gpu* gpu, mvfxGpu* kernels)
{
    // Pushed once and stopped: the planned instance finishes at the step
    // the CPU path's does, once its surface's record says it is still.
    mvfxEmitterDef emitter = Beside();
    emitter.burstCount = 0;
    // Small, stiff and damped: its slowest wave dies by e^-4t, still past
    // 160 steps or so.
    mvfxWaterDef water = Pond(16, 16, 0.1f, 3.0f, 15);
    water.damping = 8.0f;
    mvfxEffectId effect = {0};
    const Run run = {.emitters = &emitter, .emitterCount = 1, .waters = &water, .waterCount = 1};
    mvfxGpuSim* sim = OpenRun(kernels, &run, &effect);
    mvfxContext* context = RunContext();
    PushWaters(&run, 0);
    bool same = sim != NULL &&
                mvfxInstance_Stop(context, CpuInstance(), mvfx_stopEmitting) == mvfx_success &&
                mvfxInstance_Stop(context, PlannedInstance(), mvfx_stopEmitting) == mvfx_success;
    int32_t finished = -1;
    LongFrames(true);
    for (int32_t s = 0; same && finished < 0 && s < 400; ++s)
    {
        same = mvfxContext_StepTicks(context, StepTicks()) == mvfx_success && Simulate(gpu, sim) &&
               ReportWaters(gpu, sim);
        LongFrames(false);
        bool ended = mvfxInstance_IsFinished(context, CpuInstance());
        if (same && ended != mvfxInstance_IsFinished(context, PlannedInstance()))
        {
            printf("  step %d: finished on one path\n", s);
            same = false;
        }
        finished = ended ? s : -1;
    }
    printf("a pond pushed once: finished at step %d on both paths\n", finished);
    CHECK(same && finished > 10, "a stopped pond finishing");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
}

// The bytes of a read in each path's buffer, filled alike first.
static uint8_t s_cpuBytes[1 << 14];
static uint8_t s_gpuBytes[1 << 14];

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
static bool SameRead(Gpu* gpu, mvfxGpuSim* sim, const mvfxWaterRead* read, uint64_t offset,
                     uint64_t bytes, int32_t size)
{
    memset(s_cpuBytes, 0xA5, sizeof s_cpuBytes);
    if (mvfxInstance_ReadWater(RunContext(), CpuInstance(), 0, read, s_cpuBytes + offset,
                               (int64_t)(bytes - offset)) != mvfx_success ||
        !ReadWaterGpu(gpu, sim, 0, read, bytes, offset, 0xA5, s_gpuBytes))
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
        uint64_t row = rel % read->rowBytes;
        bool value = b >= offset && rel / read->rowBytes < (uint64_t)read->size[1] &&
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
                read->format == mvfx_formatFloat16 ? 1e-3f * (1.0f + fabsf(c)) : step + 1e-4f;
            same = fabsf(c - g) <= allowed;
        }
    }
    return same;
}

// The reads refused: a NULL argument, a def without its cookie, a surface
// out of range, a field, box, range or pitch out of range, an offset not a
// multiple of 4, bytes too few, a null buffer; a surface's passes, its
// buffers and its report.
static void Refusals(mvfxGpuSim* sim)
{
    mvfxGpuWaterReadDef def = mvfxDefaultGpuWaterReadDef();
    def.target = (mrhiResourceId){.index1 = 1};
    def.bytes = 4;
    mvfxGpuWaterReadDef bad[10] = {def, def, def, def, def, def, def, def, def, def};
    bad[0].cookie = 0;
    bad[1].water = 1;
    bad[2].read.field = mvfx_waterDepth + 1;
    bad[3].read.size[0] = 33;
    bad[4].read.first[1] = -1;
    bad[5].read.format = mvfx_formatUnorm8;
    bad[6].offset = 2;
    bad[7].bytes = 3;
    bad[8].target = (mrhiResourceId){0};
    bad[9].read.rowBytes = 3;
    bool refused = mvfxGpuSim_AddWaterRead(NULL, &def) == mvfx_errorInvalid &&
                   mvfxGpuSim_AddWaterRead(sim, NULL) == mvfx_errorInvalid;
    for (int32_t b = 0; b < 10; ++b)
    {
        refused = refused && mvfxGpuSim_AddWaterRead(sim, &bad[b]) == mvfx_errorInvalid;
    }
    CHECK(refused && mvfxGpuSim_WaterPasses(sim, 1) == 0 && mvfxGpuSim_WaterPasses(NULL, 0) == 0 &&
              mvfxGpuSim_WaterPasses(sim, 0) == 67,
          "a water read's refusals, a surface's passes");
    mvfxGpuWater buffers = {.statistics = {1, 1}};
    const uint32_t record[MVFX_GPU_WATER_STATISTICS_WORDS] = {0};
    mvfxContext* other = NULL;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxGpuSim_GetWater(NULL, 0, &buffers) == mvfx_errorInvalid &&
              buffers.statistics.index1 == 0 &&
              mvfxGpuSim_GetWater(sim, 1, &buffers) == mvfx_errorInvalid &&
              mvfxGpuSim_GetWater(sim, 0, NULL) == mvfx_errorInvalid &&
              mvfxGpuSim_GetWater(sim, 0, &buffers) == mvfx_success &&
              buffers.statistics.index1 != 0 &&
              mvfxCreateContext(&contextDef, &other) == mvfx_success &&
              mvfxGpuSim_ReportWater(sim, other, 0, record) == mvfx_errorInvalid &&
              mvfxGpuSim_ReportWater(NULL, RunContext(), 0, record) == mvfx_errorInvalid &&
              mvfxGpuSim_ReportWater(sim, RunContext(), 1, record) == mvfx_errorInvalid &&
              mvfxGpuSim_ReportWater(sim, RunContext(), 0, NULL) == mvfx_errorInvalid,
          "a surface's buffers and report refused");
    mvfxDestroyContext(other);
}

static void TestReads(Gpu* gpu, mvfxGpu* kernels)
{
    // After 9 steps of a pond pushed: each field in turn in each format
    // over a box from (3, 2) of 9 by 6 cells, its rows padded by 3 bytes,
    // from byte 8 of a buffer of whole words.
    mvfxEmitterDef emitter = Beside();
    mvfxWaterDef water = Pond(32, 24, 0.1f, 1.5f, 6);
    mvfxEffectId effect = {0};
    const Run run = {.emitters = &emitter, .emitterCount = 1, .waters = &water, .waterCount = 1};
    mvfxGpuSim* sim = OpenRun(kernels, &run, &effect);
    bool stepped = sim != NULL;
    LongFrames(true);
    for (int32_t s = 0; stepped && s < 9; ++s)
    {
        PushWaters(&run, s);
        stepped =
            mvfxContext_StepTicks(RunContext(), StepTicks()) == mvfx_success && Simulate(gpu, sim);
        LongFrames(false);
    }
    static const mvfxFormat formats[6] = {mvfx_formatFloat32, mvfx_formatFloat16,
                                          mvfx_formatUnorm8,  mvfx_formatSnorm8,
                                          mvfx_formatUnorm16, mvfx_formatSnorm16};
    static const int32_t sizes[6] = {4, 2, 1, 1, 2, 2};
    bool same = stepped;
    for (int32_t f = 0; same && f < 6; ++f)
    {
        for (int32_t field = mvfx_waterHeight; same && field <= mvfx_waterDepth; ++field)
        {
            uint32_t row = (uint32_t)(9 * sizes[f] + 3);
            const mvfxWaterRead read = {.field = (mvfxWaterField)field,
                                        .format = formats[f],
                                        .first = {3, 2},
                                        .size = {9, 6},
                                        .low = -0.5f,
                                        .high = 1.5f,
                                        .rowBytes = row};
            same = SameRead(gpu, sim, &read, 8, (8u + 6u * row + 3u) & ~3u, sizes[f]);
            if (!same)
            {
                printf("  format %d, field %d apart\n", f, field);
            }
        }
    }
    CHECK(same, "a surface's reads in every format");
    if (sim != NULL)
    {
        Refusals(sim);
    }
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestWaves(gpu, kernels);
    TestHitch(gpu, kernels);
    TestShallow(gpu, kernels);
    TestSplit(gpu, kernels);
    TestFinishing(gpu, kernels);
    TestReads(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
