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
// step as the CPU path's). Skips (77) without an adapter, unless
// MVFX_RHI_REQUIRED is set.

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
    // One substep a step: a step split into substeps is refused yet. A
    // fill time past the step's, which the expansion grows over.
    mvfxGasDef gas = Tank(4.0f);
    gas.fillTime = 0.05f;
    mvfxEmitterDef emitter = Pour();
    Compared(gpu, kernels,
             &(Run){.mostApart = 4, .emitters = &emitter, .gases = &gas, .gasCount = 1},
             "a liquid gas poured into a tank");
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
    TestFastest(gpu, kernels);
}

int main(void)
{
    int result = RunSuite(Suite);
    return result == 77 ? 77 : (result == 0 && s_failures == 0 ? 0 : 1);
}
