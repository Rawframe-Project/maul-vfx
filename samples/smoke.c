// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Smoke from a fire: a gas of 10 by 20 by 10 cells over a burning source
// that feeds it smoke, fuel and heat. The fuel burns into heat and smoke, the
// heat lifts the gas, vortex confinement keeps its curls, and embers ride
// its velocity up. Each second the host reads the density as unorm8, as
// it would fill a volume texture, and finds how high the smoke has risen;
// given a path it writes the last middle slice there as a PGM image. It
// returns 0 when the smoke rises through the box over time and the embers
// rise with it.

// Before every include: the C library's fopen without MSVC's warning.
#define _CRT_SECURE_NO_WARNINGS

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define NX   10
#define NY   20
#define NZ   10
#define CELL 0.24f

static uint8_t s_density[NZ][NY][NX];
static float s_embers[256][3];

static mvfxEffectId MakeEffect(mvfxContext* context)
{
    // The fire: smoke, fuel and heat at the bottom of the box.
    static const mvfxGasSource fire = {.shape = mvfx_gasSphere,
                                       .center = {0.0f, -2.0f, 0.0f},
                                       .radius = 0.3f,
                                       .density = 2.0f,
                                       .temperature = 4.0f,
                                       .fuel = 2.0f};
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = NX;
    gas.size[1] = NY;
    gas.size[2] = NZ;
    gas.cellSize = CELL;
    gas.center[1] = 0.5f * CELL * NY;
    gas.space = mvfx_gasWorld;
    gas.sources = &fire;
    gas.sourceCount = 1;
    gas.densityDecay = 0.1f;
    gas.temperatureDecay = 0.5f;
    gas.heatLift = 0.2f;
    gas.velocityDecay = 0.3f;
    gas.densityWeight = 0.1f;
    gas.vorticity = 2.0f;
    gas.advection = mvfx_gasMacCormack;
    gas.ignitionTemperature = 1.0f;
    gas.burnRate = 4.0f;
    gas.burnHeat = 3.0f;
    gas.burnSmoke = 1.0f;
    gas.burnExpansion = 0.1f;

    // Embers born in the fire, carried by the gas.
    static const mvfxForce carried = {.kind = mvfx_forceGas,
                                      .strength = {.kind = mvfx_sourceRange, .range = {1.0f, 1.0f}},
                                      .gas = 0};
    mvfxEmitterDef embers = mvfxDefaultEmitterDef();
    embers.capacity = 256;
    embers.rate = 40.0f;
    embers.lifetime = mvfxRangeSource(2.5f, 3.0f);
    embers.shape.kind = mvfx_shapeSphere;
    embers.shape.offset[1] = 0.4f;
    embers.shape.radius = 0.25f;
    embers.shape.thickness = 1.0f;
    embers.motion.drag = 1.0f;
    embers.forces = &carried;
    embers.forceCount = 1;

    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &embers;
    effectDef.emitterCount = 1;
    effectDef.gases = &gas;
    effectDef.gasCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(context, &effectDef, &effect) != mvfx_success)
    {
        printf("the effect was refused\n");
    }
    return effect;
}

// The highest row of cells any smoke (a tenth of full density or more)
// has reached, and the cells holding it.
static void Measure(int32_t* rowOut, int64_t* cellsOut)
{
    *rowOut = -1;
    *cellsOut = 0;
    for (int32_t z = 0; z < NZ; ++z)
    {
        for (int32_t y = 0; y < NY; ++y)
        {
            for (int32_t x = 0; x < NX; ++x)
            {
                if (s_density[z][y][x] >= 26)
                {
                    *rowOut = y > *rowOut ? y : *rowOut;
                    ++*cellsOut;
                }
            }
        }
    }
}

// The embers' mean height.
static bool EmberHeight(mvfxContext* context, mvfxInstanceId instance, mvfxLayoutId layout,
                        double* heightOut, int64_t* countOut)
{
    const mvfxView view = {.forward = {0.0f, 0.0f, -1.0f}, .up = {0.0f, 1.0f, 0.0f}};
    if (mvfxInstance_WriteParticles(context, instance, 0, layout, &view, NULL, 0, s_embers, 256,
                                    countOut) != mvfx_success)
    {
        return false;
    }
    double sum = 0.0;
    for (int64_t i = 0; i < *countOut; ++i)
    {
        sum += (double)s_embers[i][1];
    }
    *heightOut = *countOut > 0 ? sum / (double)*countOut : 0.0;
    return true;
}

// Writes the middle slice (Z = NZ / 2) as a binary PGM, top row first.
static bool WritePgm(const char* path)
{
    FILE* file = fopen(path, "wb");
    if (file == NULL)
    {
        printf("cannot write %s\n", path);
        return false;
    }
    bool written = fprintf(file, "P5\n%d %d\n255\n", NX, NY) > 0;
    for (int32_t y = NY - 1; written && y >= 0; --y)
    {
        written = fwrite(s_density[NZ / 2][y], 1, NX, file) == NX;
    }
    written = fclose(file) == 0 && written;
    printf(written ? "wrote %s\n" : "cannot write %s\n", path);
    return written;
}

int main(int argc, char** argv)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    if (mvfxCreateContext(&contextDef, &context) != mvfx_success)
    {
        printf("no context\n");
        return 1;
    }
    static const mvfxLayoutField place = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &place;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 12;
    mvfxLayoutId layout = {0};
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = MakeEffect(context);
    mvfxInstanceId instance = {0};
    bool ok = mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_success &&
              mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success;
    // The whole box, density 0 to 1 as 0 to 255.
    const mvfxGasRead read = {.field = mvfx_gasDensity,
                              .format = mvfx_formatUnorm8,
                              .size = {NX, NY, NZ},
                              .low = 0.0f,
                              .high = 1.0f};
    int32_t lastRow = -1;
    bool rising = true;
    double emberHeight = 0.0;
    // Three seconds at 60 steps a second.
    for (int32_t step = 1; ok && step <= 3 * 60; ++step)
    {
        ok = mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success;
        if (ok && step % 60 == 0)
        {
            int32_t row = 0;
            int64_t cells = 0;
            int64_t embers = 0;
            ok = mvfxInstance_ReadGasAs(context, instance, 0, &read, s_density,
                                        (int64_t)sizeof s_density) == mvfx_success &&
                 EmberHeight(context, instance, layout, &emberHeight, &embers);
            Measure(&row, &cells);
            printf("%d s: smoke in %lld cells, up to row %d of %d (%.1f up); %lld embers, "
                   "%.2f up on average\n",
                   step / 60, (long long)cells, row, NY, (double)(CELL * (float)(row + 1)),
                   (long long)embers, emberHeight);
            rising = rising && row >= lastRow && row > 0;
            lastRow = row;
        }
    }
    mvfxDestroyContext(context);
    ok = ok && rising && lastRow >= NY / 2 && emberHeight > 1.0 && (argc < 2 || WritePgm(argv[1]));
    printf(ok ? "the smoke rises\n" : "the smoke failed\n");
    return ok ? 0 : 1;
}
