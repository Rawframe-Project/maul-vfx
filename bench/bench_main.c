// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Timings of the library's steps, with fixed inputs. Gases: a 2D gas of
// 256 by 256 cells, closed, a sphere pouring smoke and heat and a box
// blowing a jet into it, rising by its heat, stepped at 60 a second: by
// default (one substep, 4 V cycles of pressure), with 2 cycles, and with
// MacCormack's advection; and a 3D gas of 64 by 64 by 64 cells the same
// way. Each prints its cells as a comment line, then its milliseconds a
// step, the best of five runs of 50 steps after 10 to warm up.

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

enum
{
    RUNS = 5,
    WARM = 10,
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

// Prints a case's milliseconds a step, the best of its runs.
static int Run(const char* name, const mvfxGasDef* gas)
{
    double best = 1e30;
    for (int32_t r = 0; r < RUNS; ++r)
    {
        mvfxContextDef contextDef = mvfxDefaultContextDef();
        mvfxContext* context = NULL;
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.gases = gas;
        def.gasCount = 1;
        mvfxEffectId effect = {0};
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        mvfxInstanceId instance = {0};
        if (mvfxCreateContext(&contextDef, &context) != mvfx_success ||
            mvfxCreateEffect(context, &def, &effect) != mvfx_success)
        {
            return 1;
        }
        instanceDef.effect = effect;
        if (mvfxCreateInstance(context, &instanceDef, &instance) != mvfx_success)
        {
            return 1;
        }
        double start = 0.0;
        for (int32_t s = 0; s < WARM + STEPS; ++s)
        {
            start = s == WARM ? Now() : start;
            if (mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) != mvfx_success)
            {
                return 1;
            }
        }
        double each = (Now() - start) / STEPS;
        best = each < best ? each : best;
        mvfxDestroyContext(context);
    }
    printf("# %s: %d by %d by %d cells\n", name, gas->size[0], gas->size[1], gas->size[2]);
    printf("%-32s %10.3f ms\n", name, 1e3 * best);
    return 0;
}

int main(void)
{
    const mvfxGasDef flat = Gas(256, 1, 4, mvfx_gasSemiLagrangian);
    const mvfxGasDef light = Gas(256, 1, 2, mvfx_gasSemiLagrangian);
    const mvfxGasDef sharp = Gas(256, 1, 4, mvfx_gasMacCormack);
    const mvfxGasDef deep = Gas(64, 64, 4, mvfx_gasSemiLagrangian);
    int failed = Run("gas 2D 256", &flat);
    failed |= Run("gas 2D 256, 2 cycles", &light);
    failed |= Run("gas 2D 256, MacCormack", &sharp);
    failed |= Run("gas 3D 64", &deep);
    return failed;
}
