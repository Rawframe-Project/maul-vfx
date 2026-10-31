// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Timings of the library's steps, with fixed inputs. Gases: a 2D gas of
// 256 by 256 cells, closed, a sphere pouring smoke and heat and a box
// blowing a jet into it, rising by its heat, stepped at 60 a second: by
// default (one substep, 4 V cycles of pressure), with 2 cycles, and with
// MacCormack's advection; and a 3D gas of 64 by 64 by 64 cells the same
// way. Rain: a waves surface of 256 by 256 cells of 0.05 under rain of
// 6,000 and 60,000 drops a second from a box above it, falling at 4,
// each crossing pushing it, against the same rain not meeting it, the
// heavier meeting it without pushing, and the surface alone. Each prints
// what it steps as a comment line, then its milliseconds a step, the
// best of five runs of 50 steps after 10 to warm up (rain: after 120, so
// the drops fill their lives).

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

enum
{
    RUNS = 5,
    STEPS = 50
};

// The time now in seconds.
static double Now(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + 1e-9 * (double)now.tv_nsec;
}

// A gas of n cells along each axis (2D for one along Z) as the cases
// take it.
static mvfxGasDef Gas(int32_t n, int32_t layers, int32_t cycles, mvfxGasAdvection advection)
{
    static const mvfxGasSource sources[2] = {
        {.shape = mvfx_gasSphere,
         .center = {0, -0.8f, 0},
         .radius = 0.2f,
         .density = 3,
         .temperature = 5},
        {.shape = mvfx_gasBox,
         .center = {0, -0.8f, 0},
         .halfExtents = {0.2f, 0.2f, 0.2f},
         .setsVelocity = true,
         .velocity = {0.3f, 4, 0}},
    };
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = n;
    gas.size[1] = n;
    gas.size[2] = layers;
    gas.cellSize = 2.0f / (float)n;
    gas.closedWalls = 63;
    gas.pressureCycles = cycles;
    gas.advection = advection;
    gas.heatLift = 0.05f;
    gas.sources = sources;
    gas.sourceCount = 2;
    return gas;
}

// Prints an effect's milliseconds a step, the best of its runs, after
// warm steps.
static int Run(const char* name, const mvfxEffectDef* def, int32_t warm)
{
    double best = 1e30;
    for (int32_t r = 0; r < RUNS; ++r)
    {
        mvfxContextDef contextDef = mvfxDefaultContextDef();
        mvfxContext* context = NULL;
        mvfxEffectId effect = {0};
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        mvfxInstanceId instance = {0};
        if (mvfxCreateContext(&contextDef, &context) != mvfx_success ||
            mvfxCreateEffect(context, def, &effect, NULL) != mvfx_success)
        {
            return 1;
        }
        instanceDef.effect = effect;
        if (mvfxCreateInstance(context, &instanceDef, &instance) != mvfx_success)
        {
            return 1;
        }
        double start = 0.0;
        for (int32_t s = 0; s < warm + STEPS; ++s)
        {
            start = s == warm ? Now() : start;
            if (mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) != mvfx_success)
            {
                return 1;
            }
        }
        double each = (Now() - start) / STEPS;
        best = each < best ? each : best;
        mvfxDestroyContext(context);
    }
    printf("%-32s %10.3f ms\n", name, 1e3 * best);
    return 0;
}

// Prints a gas case's cells and its milliseconds a step.
static int RunGas(const char* name, const mvfxGasDef* gas)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = gas;
    def.gasCount = 1;
    printf("# %s: %d by %d by %d cells\n", name, gas->size[0], gas->size[1], gas->size[2]);
    return Run(name, &def, 10);
}

// Prints a rain case: a surface of 256 by 256 cells of 0.05, under rate
// drops a second (none for 0) meeting it or not, pushing it or not.
static int RunRain(const char* name, float rate, bool meets, bool pushes)
{
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 256;
    water.size[1] = 256;
    water.cellSize = 0.05f;
    mvfxEmitterDef rain = mvfxDefaultEmitterDef();
    rain.capacity = (int32_t)(rate * 2.0f) + 1;
    rain.rate = rate;
    rain.lifetime = mvfxMakeRangeSource(1.5f, 2.0f);
    rain.shape = (mvfxShape){.kind = mvfx_shapeBox,
                             .offset = {0.0f, 1.0f, 0.0f},
                             .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
                             .halfExtents = {6.0f, 0.1f, 6.0f},
                             .thickness = 1.0f};
    rain.velocity = (mvfxVelocity){
        .mode = mvfx_directionFixed, .direction = {0.0f, -1.0f, 0.0f}, .speed = {4.0f, 4.0f}};
    rain.water = (mvfxWaterCoupling){.enable = meets,
                                     .radius = 0.01f,
                                     .push = pushes ? 0.002f : 0.0f,
                                     .buoyancy = 0.5f,
                                     .drag = 2.0f};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &rain;
    def.emitterCount = rate > 0.0f ? 1 : 0;
    def.waters = &water;
    def.waterCount = 1;
    printf("# %s: 256 by 256 cells, %.0f drops a second%s\n", name, (double)rate,
           meets ? (pushes ? ", meeting and pushing it" : ", meeting it") : "");
    return Run(name, &def, 120);
}

int main(void)
{
    const mvfxGasDef flat = Gas(256, 1, 4, mvfx_gasSemiLagrangian);
    const mvfxGasDef light = Gas(256, 1, 2, mvfx_gasSemiLagrangian);
    const mvfxGasDef sharp = Gas(256, 1, 4, mvfx_gasMacCormack);
    const mvfxGasDef deep = Gas(64, 64, 4, mvfx_gasSemiLagrangian);
    int failed = RunGas("gas 2D 256", &flat);
    failed |= RunGas("gas 2D 256, 2 cycles", &light);
    failed |= RunGas("gas 2D 256, MacCormack", &sharp);
    failed |= RunGas("gas 3D 64", &deep);
    failed |= RunRain("water 256", 0.0f, false, false);
    failed |= RunRain("water 256, rain 6k apart", 6000.0f, false, false);
    failed |= RunRain("water 256, rain 6k", 6000.0f, true, true);
    failed |= RunRain("water 256, rain 60k apart", 60000.0f, false, false);
    failed |= RunRain("water 256, rain 60k, no push", 60000.0f, true, false);
    failed |= RunRain("water 256, rain 60k", 60000.0f, true, true);
    return failed;
}
