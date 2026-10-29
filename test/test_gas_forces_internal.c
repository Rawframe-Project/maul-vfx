// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The gas forces module's own buffers: confinement's work holding the
// curl at the cells' centers, confinement alone at the least cell and the
// fastest faces, and the work buffer sized only for what needs it. Neither reaches a step's results
// but through the force.

#include "gas_forces.h"
#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// Opens an instance of an effect of one gas, its origin at 0, turned by
// rotation and of scale; gives its compiled gas and state.
static mvfxGasState* Open(const mvfxGasDef* gas, const float rotation[4], float scale,
                          const mvfxCompiledGas** compiled)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    memcpy(instanceDef.rotation, rotation, sizeof instanceDef.rotation);
    instanceDef.scale = scale;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxInstance* live = mvfxContextInstance(s_context, s_instance);
    *compiled = &live->effect->gases[0];
    return &live->gases[0];
}

static const float s_unturned[4] = {0.0f, 0.0f, 0.0f, 1.0f};

static void TestCurl(void)
{
    // A 2D gas of 12 by 10 cells of 0.1 with u = 2 X Y (X and Y from its
    // corner), v = 0: after confinement its work holds the curl at each
    // cell's center, -dU/dy = -2 X there (the faces' mean placed at the
    // center, half a cell from its low face), one-sided at the edges and
    // as exact.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 12;
    def.size[1] = 10;
    def.vorticity = 1.0f;
    const mvfxCompiledGas* gas = NULL;
    mvfxGasState* state = Open(&def, s_unturned, 1.0f, &gas);
    float* u = state->velocity[0][state->current];
    for (int32_t y = 0; y < 10; ++y)
    {
        for (int32_t x = 0; x <= 12; ++x)
        {
            u[y * 13 + x] = 2.0f * ((float)x * 0.1f) * (((float)y + 0.5f) * 0.1f);
        }
    }
    const mvfxGasLattice l = {.n = {12, 10, 1}, .cell = 0.1f, .h = 1.0f / 64.0f};
    float* velocity[3] = {u, state->velocity[1][state->current],
                          state->velocity[2][state->current]};
    mvfxGasConfine(gas, &l, velocity, state->work);
    bool right = true;
    for (int32_t i = 0; i < 120; ++i)
    {
        float center = ((float)(i % 12) + 0.5f) * 0.1f;
        right = right && fabsf(state->work[i] + 2.0f * center) <= 1e-5f;
    }
    CHECK(right, "the curl at the cells' centers");
    mvfxDestroyContext(s_context);
}

// A Gaussian vortex in a 2D gas of 32 by 32 cells, its velocities times
// scale, into velocity's faces.
static void Vortex(float* const velocity[3], float scale)
{
    for (int32_t y = 0; y < 32; ++y)
    {
        for (int32_t x = 0; x <= 32; ++x)
        {
            float dx = (float)x - 16.0f;
            float dy = (float)y + 0.5f - 16.0f;
            velocity[0][y * 33 + x] = -scale * dy * expf(-(dx * dx + dy * dy) / 32.0f);
            velocity[1][x * 32 + y] = scale * dy * expf(-(dx * dx + dy * dy) / 32.0f);
        }
    }
}

static void TestFastest(void)
{
    // Confinement at 2 a second over a substep of 1/64 s on a Gaussian
    // vortex: in cells of MVFX_MIN_GAS_CELL with velocities 2e8 times
    // those in cells of 1 (up to about 5e8), its curl's size changes by
    // about 10^21 a cell, whose square passes binary32's largest, yet each
    // face's change is 2e8 times, within 1e-4 of the largest. (A step
    // carries such velocities across the box in the same substep, so only
    // the force alone shows it.)
    static float changes[2][2][33 * 32];
    for (int32_t r = 0; r < 2; ++r)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 32;
        def.size[1] = 32;
        def.vorticity = 2.0f;
        def.cellSize = r == 0 ? 1.0f : MVFX_MIN_GAS_CELL;
        const mvfxCompiledGas* gas = NULL;
        mvfxGasState* state = Open(&def, s_unturned, 1.0f, &gas);
        float* velocity[3] = {state->velocity[0][state->current],
                              state->velocity[1][state->current],
                              state->velocity[2][state->current]};
        Vortex(velocity, r == 0 ? 1.0f : 2e8f);
        memcpy(changes[r][0], velocity[0], sizeof changes[r][0]);
        memcpy(changes[r][1], velocity[1], sizeof changes[r][1]);
        const mvfxGasLattice l = {.n = {32, 32, 1}, .cell = def.cellSize, .h = 1.0f / 64.0f};
        mvfxGasConfine(gas, &l, velocity, state->work);
        for (int32_t i = 0; i < 33 * 32; ++i)
        {
            changes[r][0][i] = velocity[0][i] - changes[r][0][i];
            changes[r][1][i] = velocity[1][i] - changes[r][1][i];
        }
        mvfxDestroyContext(s_context);
    }
    double largest = 0.0;
    double apart = 0.0;
    for (int32_t k = 0; k < 2; ++k)
    {
        for (int32_t i = 0; i < 33 * 32; ++i)
        {
            double slow = 2e8 * (double)changes[0][k][i];
            largest = fmax(largest, fabs(slow));
            apart = fmax(apart, fabs((double)changes[1][k][i] - slow));
        }
    }
    CHECK(largest > 0.0 && apart <= 1e-4 * largest, "confinement at the least cell, the fastest");
}

static void TestWork(void)
{
    // The work buffer: none by default; a 3D gas's curl, three arrays; a
    // 2D gas's, one; MacCormack, the largest faces array it carries (a 2D
    // gas's Y faces, not its Z); expansion, the cells.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 10;
    def.size[1] = 6;
    def.size[2] = 4;
    mvfxCompiledGas gas = mvfxCompileGas(&def);
    bool right = gas.work == 0;
    def.vorticity = 1.0f;
    gas = mvfxCompileGas(&def);
    right = right && gas.work == 3 * 240;
    def.size[2] = 1;
    gas = mvfxCompileGas(&def);
    right = right && gas.work == 60;
    def.vorticity = 0.0f;
    def.advection = mvfx_gasMacCormack;
    gas = mvfxCompileGas(&def);
    right = right && gas.work == 70;
    def.advection = mvfx_gasSemiLagrangian;
    def.burnRate = 1.0f;
    gas = mvfxCompileGas(&def);
    right = right && gas.work == 0;
    def.burnExpansion = 1.0f;
    gas = mvfxCompileGas(&def);
    right = right && gas.work == 60;
    CHECK(right, "the work buffer sized for its uses");
}

int main(void)
{
    TestCurl();
    TestFastest();
    TestWork();
    return s_failures;
}
