// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particle liquids on a real device (lavapipe on CI) beside the CPU path,
// compared a step at a time from the same state (rhi_harness.h): places
// and velocities within the tolerance (the kernels' square roots and
// divisions are within their rounding of the core's). Covered: a 3D
// liquid poured onto a floor with viscosity, confinement and the tensile
// correction, and with four neighbors at most; a planar liquid in the
// world's space under a moving, turning, growing instance (its kernels
// scaled with it); the passes a liquid's step takes; a liquid gas poured
// into a tank (its particles carried by the gas's velocity, their affine
// matrices read back, the fastest speed reported and splitting the next
// step as the CPU path's), and onto its floor; frames split into
// substeps, the tank's alone (and with each frame dropped once first),
// beside a smoke gas splatted into, beside an emitter's events gathered
// over the substeps and the births they give, and its records after the
// last substep.
// Skips (77) without an adapter, unless MVFX_RHI_REQUIRED is set.

#include "gas_liquid.h"
#include "instance.h"
#include "instance_api.h"
#include "rhi_harness.h"
#include "test_harness.h"

#include "maul-vfx/plan.h"

#include <string.h>

static const mvfxCollider s_floor = {
    .kind = mvfx_colliderPlane, .center = {0, -0.4f, 0}, .normal = {0, 1, 0}};

// A liquid poured from a small sphere onto a floor, four substeps of three
// iterations.
static mvfxEmitterDef Poured(void)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.rate = 400.0f;
    emitter.burstCount = 0;
    emitter.lifetime = mvfxRangeSource(1.5f, 2.0f);
    emitter.shape.radius = 0.08f;
    emitter.velocity.speed = (mvfxRange){0.2f, 0.4f};
    emitter.velocity.direction[1] = -1.0f;
    emitter.motion.drag = 0.0f;
    emitter.colliders = &s_floor;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enabled = true};
    emitter.liquid = mvfxDefaultEmitterDef().liquid;
    emitter.liquid.enabled = true;
    emitter.liquid.spacing = 0.04f;
    emitter.liquid.viscosity = 0.1f;
    emitter.liquid.vorticity = 0.5f;
    emitter.liquid.tensile = 0.2f;
    return emitter;
}

// Compares a liquid a step at a time from the same state: a particle at a
// neighbor's radius may count it on one path alone, which parts the two
// (rhi_apart.h), at most the scene's bound (none on lavapipe; the
// harness's least, 4).
static void Compared(Gpu* gpu, mvfxGpu* kernels, const Run* scene, const char* what)
{
    Run run = *scene;
    run.emitterCount = 1;
    run.colliding = true;
    run.adopting = true;
    run.tolerance = 1e-3f;
    Compare(gpu, kernels, &run, what);
}

static void TestPoured(Gpu* gpu, mvfxGpu* kernels)
{
    mvfxEmitterDef emitter = Poured();
    Compared(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter},
             "a liquid poured onto a floor");
    // At most four neighbors: most particles are capped.
    emitter.liquid.maxNeighbors = 4;
    Compared(gpu, kernels, &(Run){.mostApart = 4, .emitters = &emitter},
             "a liquid of four neighbors at most");
    // Planar, in the world's space under a moving, turning, growing
    // instance.
    emitter = Poured();
    emitter.liquid.planar = true;
    emitter.space = mvfx_spaceWorld;
    Compared(gpu, kernels,
             &(Run){.mostApart = 4, .emitters = &emitter, .moving = true, .pace = 0.001},
             "a planar liquid under a moving instance");
}

static const mvfxCollider s_tank = {.kind = mvfx_colliderBox,
                                    .inside = true,
                                    .halfExtents = {0.8f, 0.8f, 1.0f},
                                    .rotation = {0, 0, 0, 1}};

// A 2D liquid gas of 16 by 16 cells of 0.1 about the origin, closed on
// every side, of a courant number.
static mvfxGasDef Tank(float courant)
{
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.kind = mvfx_gasLiquid;
    gas.size[0] = 16;
    gas.size[1] = 16;
    gas.size[2] = 1;
    gas.cellSize = 0.1f;
    gas.closedWalls = 63;
    gas.courant = courant;
    return gas;
}

// A liquid poured into the tank's gas from a small sphere, kept in its
// box.
static mvfxEmitterDef Pour(void)
{
    mvfxEmitterDef emitter = Sprite();
    emitter.capacity = 512;
    emitter.rate = 300.0f;
    emitter.burstCount = 0;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.shape.radius = 0.1f;
    emitter.velocity.speed = (mvfxRange){0.1f, 0.3f};
    emitter.motion.drag = 0.0f;
    emitter.colliders = &s_tank;
    emitter.colliderCount = 1;
    emitter.collision = (mvfxCollision){.enabled = true};
    emitter.liquid = mvfxDefaultEmitterDef().liquid;
    emitter.liquid.enabled = true;
    emitter.liquid.solver = mvfx_liquidGrid;
    emitter.liquid.gas = 0;
    emitter.liquid.spacing = 0.05f;
    return emitter;
}

static void TestLiquidGas(Gpu* gpu, mvfxGpu* kernels)
{
    // One substep a step (TestSplit's split them), and a fill time past
    // the step's, which the expansion grows over.
    mvfxGasDef gas = Tank(4.0f);
    gas.fillTime = 0.05f;
    mvfxEmitterDef emitter = Pour();
    Compared(gpu, kernels,
             &(Run){.mostApart = 4, .emitters = &emitter, .gases = &gas, .gasCount = 1},
             "a liquid gas poured into a tank");
    // Poured down onto the tank's floor, where the particles pool and
    // slide along it.
    emitter.velocity.direction[1] = -1.0f;
    emitter.velocity.speed = (mvfxRange){1.0f, 1.5f};
    Compared(gpu, kernels,
             &(Run){.mostApart = 4, .emitters = &emitter, .gases = &gas, .gasCount = 1},
             "a liquid gas poured onto a tank's floor");
}

static void TestSplit(Gpu* gpu, mvfxGpu* kernels)
{
    // A courant number of 0.2: gravity's pull over five cells alone
    // crosses 0.37 of a cell a step, so every step splits in two or more
    // (up to eight here), each substep the emitter's and then the gas's.
    // Poured down onto the floor; in the short substeps the gas's closed
    // faces hold the particles off the tank, so the run asks for no
    // contacts (the floor scene's slide is TestLiquidGas's).
    mvfxGasDef gas = Tank(0.2f);
    mvfxEmitterDef emitter = Pour();
    emitter.velocity.direction[1] = -1.0f;
    emitter.velocity.speed = (mvfxRange){1.0f, 1.5f};
    Run run = {.mostApart = 4,
               .emitters = &emitter,
               .emitterCount = 1,
               .gases = &gas,
               .gasCount = 1,
               .adopting = true,
               .tolerance = 1e-3f};
    Compare(gpu, kernels, &run, "a liquid gas poured into a tank in substeps");
    // Each frame's passes added and dropped once first: the dropped
    // frame's steps undone.
    run.dropping = true;
    Compare(gpu, kernels, &run, "a liquid gas in substeps, each frame dropped once first");
}

static void TestSplitRecords(Gpu* gpu, mvfxGpu* kernels)
{
    // A split frame's records, written after its last step from that
    // step's plan: places and velocities, ages, and sizes and colours
    // over life (each at the frame's end), of the tank's poured liquid.
    static const mvfxLayoutField fields[5] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
        {.field = mvfx_recordAge, .format = mvfx_formatFloat32, .components = 1, .offset = 24},
        {.field = mvfx_recordSize, .format = mvfx_formatFloat32, .components = 3, .offset = 28},
        {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 4, .offset = 40}};
    static const mvfxCurveKey keys[2] = {
        {.time = 0, .value = 1, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 0.25f, .interpolation = mvfx_interpolationLinear}};
    static const mvfxColorKey colors[2] = {{0, 1, 0.5f, 0}, {1, 0, 0.25f, 1}};
    static const mvfxAlphaKey alphas[2] = {{0, 1}, {1, 0.5f}};
    const mvfxCurve curve = {.keys = keys, .keyCount = 2};
    mvfxLayoutDef layout = mvfxDefaultLayoutDef();
    layout.fields = fields;
    layout.fieldCount = 5;
    layout.stride = 56;
    mvfxGasDef gas = Tank(0.2f);
    mvfxEmitterDef emitter = Pour();
    emitter.velocity.direction[1] = -1.0f;
    emitter.velocity.speed = (mvfxRange){1.0f, 1.5f};
    emitter.lifetime = mvfxRangeSource(2.0f, 3.0f);
    emitter.overLife =
        (mvfxOverLife){.color = {.enabled = true,
                                 .input = mvfx_overAge,
                                 .gradient = {.colorKeys = colors,
                                              .colorKeyCount = 2,
                                              .alphaKeys = alphas,
                                              .alphaKeyCount = 2}},
                       .size = {.enabled = true, .size = {{.value = 2, .curve = curve}}}};
    Run run = {.mostApart = 4,
               .emitters = &emitter,
               .emitterCount = 1,
               .gases = &gas,
               .gasCount = 1,
               .adopting = true,
               .tolerance = 1e-3f,
               .layout = &layout,
               .recordCapacity = 512};
    Compare(gpu, kernels, &run, "a split frame's records");
}

// A 2D smoke gas of 40 by 32 cells of 0.1 about the origin, rising
// from a hot box at its floor, which it holds closed.
static mvfxGasDef Smoke(void)
{
    static const mvfxGasSource source = {.shape = mvfx_gasBox,
                                         .center = {-0.4f, -1.0f, 0.0f},
                                         .halfExtents = {0.3f, 0.2f, 0.5f},
                                         .density = 3.0f,
                                         .temperature = 1.0f};
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 40;
    gas.size[1] = 32;
    gas.size[2] = 1;
    gas.cellSize = 0.1f;
    gas.substeps = 2;
    gas.pressureCycles = 2;
    gas.closedWalls = mvfx_gasWallMinY;
    gas.heatLift = 1.5f;
    gas.sources = &source;
    gas.sourceCount = 1;
    return gas;
}

static void TestSplitBeside(Gpu* gpu, mvfxGpu* kernels)
{
    // A split frame of a liquid gas beside a smoke gas, and an emitter
    // splatting into the smoke and pushed by it: each substep the smoke's
    // passes splat that substep's particles, and the emitter reads the
    // faces the substep before left.
    static const mvfxForce pushed = {
        .kind = mvfx_forceGas, .strength = 2.0f, .fieldMode = mvfx_fieldAcceleration, .gas = 1};
    mvfxGasDef gases[2] = {Tank(0.2f), Smoke()};
    mvfxEmitterDef emitters[2] = {Pour(), Sprite()};
    emitters[1].capacity = 512;
    emitters[1].rate = 300.0f;
    emitters[1].burstCount = 0;
    emitters[1].lifetime = mvfxRangeSource(0.3f, 0.6f);
    emitters[1].shape.radius = 0.3f;
    emitters[1].velocity.speed = (mvfxRange){0.5f, 1.0f};
    emitters[1].motion.gravity[1] = 0.0f;
    emitters[1].forces = &pushed;
    emitters[1].forceCount = 1;
    emitters[1].gasSplat = (mvfxGasSplat){
        .enabled = true, .gas = 1, .density = 2.0f, .temperature = 1.0f, .drag = 4.0f};
    Run run = {.mostApart = 4,
               .emitters = emitters,
               .emitterCount = 2,
               .gases = gases,
               .gasCount = 2,
               .adopting = true,
               .tolerance = 1e-3f};
    Compare(gpu, kernels, &run, "a split liquid gas beside a smoke splatted into");
}

static void TestSplitEvents(Gpu* gpu, mvfxGpu* kernels)
{
    // A split frame's events gathered over its substeps: an emitter's
    // births and deaths, each substep's after those before and given their
    // sizes over life once; a spawner taking two children a death from
    // its source's substep's deaths alone. Then a bound of 7 the substeps
    // fill together, the rest dropped.
    static const mvfxCurveKey keys[2] = {
        {.time = 0, .value = 1, .interpolation = mvfx_interpolationLinear},
        {.time = 1, .value = 0.25f, .interpolation = mvfx_interpolationLinear}};
    const mvfxCurve curve = {.keys = keys, .keyCount = 2};
    mvfxGasDef gas = Tank(0.2f);
    mvfxEmitterDef emitters[3] = {Pour(), Sprite(), Sprite()};
    emitters[0].velocity.direction[1] = -1.0f;
    emitters[0].velocity.speed = (mvfxRange){1.0f, 1.5f};
    emitters[1].capacity = 512;
    emitters[1].rate = 600.0f;
    emitters[1].burstCount = 0;
    emitters[1].lifetime = mvfxRangeSource(0.02f, 0.05f);
    emitters[1].events = mvfx_eventMaskBirth | mvfx_eventMaskDeath;
    emitters[1].eventCapacity = 256;
    emitters[1].overLife =
        (mvfxOverLife){.size = {.enabled = true, .size = {{.value = 2, .curve = curve}}}};
    emitters[2].capacity = 1024;
    emitters[2].rate = 0.0f;
    emitters[2].burstCount = 0;
    emitters[2].lifetime = mvfxRangeSource(0.2f, 0.2f);
    emitters[2].eventSpawn = (mvfxEventSpawn){.enabled = true,
                                              .source = 1,
                                              .kind = mvfx_eventDeath,
                                              .minCount = 2,
                                              .maxCount = 2,
                                              .capacity = 256};
    // The short lives hold about 20 at once.
    Run run = {.mostApart = 4,
               .emitters = emitters,
               .emitterCount = 3,
               .gases = &gas,
               .gasCount = 1,
               .secondLeast = 10,
               .adopting = true,
               .tolerance = 1e-3f};
    Compare(gpu, kernels, &run, "a split frame's events and the births they give");
    emitters[1].eventCapacity = 7;
    Compare(gpu, kernels, &run, "a split frame's events past the bound dropped");
}

static void TestFastest(Gpu* gpu, mvfxGpu* kernels)
{
    // The fastest speed the device reports after the first step splits
    // the next as the CPU path's particles do: into the same substeps,
    // more than one and fewer than the most (none reported, the most).
    // No gravity, whose pull over five cells counts besides.
    mvfxGasDef gas = Tank(0.0125f);
    mvfxEmitterDef emitter = Pour();
    memset(emitter.motion.gravity, 0, sizeof emitter.motion.gravity);
    mvfxEffectId effect = {0};
    const Run run = {.emitters = &emitter, .emitterCount = 1, .gases = &gas, .gasCount = 1};
    mvfxGpuSim* sim = OpenRun(kernels, &run, &effect);
    mvfxContext* context = RunContext();
    bool stepped = sim != NULL && mvfxContext_StepTicks(context, StepTicks()) == mvfx_success &&
                   Simulate(gpu, sim) && ReportGases(gpu, sim);
    int32_t planned =
        mvfxLiquidSubsteps(mvfxContextInstance(context, PlannedInstance()), StepTicks());
    int32_t cpu = mvfxLiquidSubsteps(mvfxContextInstance(context, CpuInstance()), StepTicks());
    CHECK(stepped && planned == cpu && planned > 1 && planned < MVFX_MAX_PLAN_STEPS,
          "a liquid gas's fastest speed reported");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(context);
}

static void TestPasses(mvfxGpu* kernels)
{
    // A liquid's passes a step, for a host to size its device by: the
    // emitter's own 21, the liquid's write, then each of its 4 substeps
    // 11, 3 for each of 3 iterations, 2 for viscosity and 3 for
    // confinement; one without them 1 + 4 (11 + 9); none for an emitter
    // out of range.
    mvfxEmitterDef emitter = Poured();
    mvfxEffectId effect = {0};
    mvfxGpuSim* sim = OpenRun(kernels, &(Run){.emitters = &emitter, .emitterCount = 1}, &effect);
    CHECK(mvfxGpuSim_EmitterPasses(sim, 0) == 21 + 1 + 4 * (11 + 9 + 2 + 3) &&
              mvfxGpuSim_EmitterPasses(sim, 1) == 0 && mvfxGpuSim_EmitterPasses(NULL, 0) == 0,
          "a liquid's passes");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
    emitter.liquid.viscosity = 0.0f;
    emitter.liquid.vorticity = 0.0f;
    sim = OpenRun(kernels, &(Run){.emitters = &emitter, .emitterCount = 1}, &effect);
    CHECK(mvfxGpuSim_EmitterPasses(sim, 0) == 21 + 1 + 4 * (11 + 9),
          "a liquid's passes without viscosity or confinement");
    mvfxDestroyGpuSim(sim);
    mvfxDestroyContext(RunContext());
}

static void Suite(Gpu* gpu, mvfxGpu* kernels)
{
    TestPasses(kernels);
    TestPoured(gpu, kernels);
    TestLiquidGas(gpu, kernels);
    TestSplit(gpu, kernels);
    TestSplitBeside(gpu, kernels);
    TestSplitEvents(gpu, kernels);
    TestSplitRecords(gpu, kernels);
    TestFastest(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
