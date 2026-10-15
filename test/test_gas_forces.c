// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's forces, combustion and advection: buoyancy's size and direction
// in the world's gravity (its instance turned and scaled) and in the
// box's own; vorticity confinement strengthening a vortex about its peak,
// in proportion to its strength; fuel burning where it is hot enough, to
// heat, smoke and expansion; the projection keeping the expansion's
// divergence (less its mean in a sealed box); MacCormack's advection
// sharper than semi-Lagrangian and making no new extremes; the work
// buffer only for what needs it; the def's refusals.

#include "gas_forces.h"
#include "gas_pressure.h"
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

// Steps the context by n steps of 1/64 s.
static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
    }
}

// Whether every value of an array is within tolerance of value.
static bool AllNear(const float* values, int64_t count, float value, float tolerance)
{
    bool near = true;
    for (int64_t i = 0; i < count; ++i)
    {
        near = near && fabsf(values[i] - value) <= tolerance;
    }
    return near;
}

static void TestBuoyancy(void)
{
    // A still 2D gas of 16 by 12 cells, its temperature 2 and density 0.5
    // everywhere, no pressure: one step of 1/64 s by a heat lift of 0.3
    // and a density weight of 0.2 speeds every face up by 9.81 (0.3 2 -
    // 0.2 0.5) / 64 against the world's gravity. Turned a quarter about Z,
    // the box's +X points up the world and the lift is along it; at scale
    // 2, half of it; with
    // gravity in the box's own frame, along its Y whatever the turn.
    const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    const float lift = 9.81f * (0.3f * 2.0f - 0.2f * 0.5f) / 64.0f;
    struct Case
    {
        const float* rotation;
        float scale;
        mvfxGasSpace space;
        int32_t axis;
        float speed;
    } cases[4] = {{s_unturned, 1.0f, mvfx_gasWorld, 1, lift},
                  {quarter, 1.0f, mvfx_gasWorld, 0, lift},
                  {s_unturned, 2.0f, mvfx_gasWorld, 1, 0.5f * lift},
                  {quarter, 1.0f, mvfx_gasLocal, 1, lift}};
    bool right = true;
    for (int32_t c = 0; c < 4; ++c)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 16;
        def.size[1] = 12;
        def.pressureCycles = 0;
        def.heatLift = 0.3f;
        def.densityWeight = 0.2f;
        def.gravitySpace = cases[c].space;
        const mvfxCompiledGas* gas = NULL;
        mvfxGasState* state = Open(&def, cases[c].rotation, cases[c].scale, &gas);
        for (int64_t i = 0; i < gas->cells; ++i)
        {
            state->scalars[0][0][i] = 0.5f;
            state->scalars[1][0][i] = 2.0f;
        }
        Steps(1);
        int32_t a = cases[c].axis;
        right = right &&
                AllNear(state->velocity[a][state->current], gas->faces[a], cases[c].speed, 1e-5f) &&
                AllNear(state->velocity[1 - a][state->current], gas->faces[1 - a], 0.0f, 1e-5f);
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "buoyancy's size and direction");
}

// Sets a 2D gas of n by n cells to a Gaussian vortex about its middle,
// turning counterclockwise, its speed peaking 4 cells out.
static void Vortex(const mvfxCompiledGas* gas, mvfxGasState* state, int32_t n)
{
    float* u = state->velocity[0][state->current];
    float* v = state->velocity[1][state->current];
    float middle = 0.5f * (float)n;
    for (int32_t y = 0; y < n; ++y)
    {
        for (int32_t x = 0; x <= n; ++x)
        {
            float dx = (float)x - middle;
            float dy = (float)y + 0.5f - middle;
            u[y * (n + 1) + x] = -dy * expf(-(dx * dx + dy * dy) / 32.0f);
        }
    }
    for (int32_t y = 0; y <= n; ++y)
    {
        for (int32_t x = 0; x < n; ++x)
        {
            float dx = (float)x + 0.5f - middle;
            float dy = (float)y - middle;
            v[y * n + x] = dx * expf(-(dx * dx + dy * dy) / 32.0f);
        }
    }
    (void)gas;
}

// The angular momentum about a 2D gas's middle, counterclockwise, from
// the change of its velocity since before, over the faces within 5 cells
// of the middle.
static double Spin(const mvfxGasState* state, const float* beforeU, const float* beforeV, int32_t n)
{
    const float* u = state->velocity[0][state->current];
    const float* v = state->velocity[1][state->current];
    double middle = 0.5 * n;
    double spin = 0.0;
    for (int32_t y = 0; y < n; ++y)
    {
        for (int32_t x = 0; x <= n; ++x)
        {
            int32_t i = y * (n + 1) + x;
            double dx = (double)x - middle;
            double dy = (double)y + 0.5 - middle;
            spin -= dx * dx + dy * dy < 25.0 ? dy * (double)(u[i] - beforeU[i]) : 0.0;
        }
    }
    for (int32_t y = 0; y <= n; ++y)
    {
        for (int32_t x = 0; x < n; ++x)
        {
            int32_t i = y * n + x;
            double dx = (double)x + 0.5 - middle;
            double dy = (double)y - middle;
            spin += dx * dx + dy * dy < 25.0 ? dx * (double)(v[i] - beforeV[i]) : 0.0;
        }
    }
    return spin;
}

static void TestConfinement(void)
{
    // A Gaussian vortex in a 2D gas of 32 by 32 cells, its curl
    // e (2 - r^2 / 16) at r cells out (e its Gaussian) peaking in the
    // middle: confinement at 2 and at 4 a second over a substep of 1/64 s
    // each adds spin to the core within 5 cells in its own sense (N points
    // in to the peak, N x w along its turn; out past the curl's ring at 8
    // cells it turns the other way), the second twice the first's.
    static float beforeU[33 * 32];
    static float beforeV[32 * 33];
    double spins[2];
    for (int32_t r = 0; r < 2; ++r)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 32;
        def.size[1] = 32;
        def.vorticity = r == 0 ? 2.0f : 4.0f;
        const mvfxCompiledGas* gas = NULL;
        mvfxGasState* state = Open(&def, s_unturned, 1.0f, &gas);
        Vortex(gas, state, 32);
        memcpy(beforeU, state->velocity[0][state->current], sizeof beforeU);
        memcpy(beforeV, state->velocity[1][state->current], sizeof beforeV);
        const mvfxGasLattice l = {.n = {32, 32, 1}, .cell = def.cellSize, .h = 1.0f / 64.0f};
        float* velocity[3] = {state->velocity[0][state->current],
                              state->velocity[1][state->current],
                              state->velocity[2][state->current]};
        mvfxGasConfine(gas, &l, velocity, state->work);
        spins[r] = Spin(state, beforeU, beforeV, 32);
        mvfxDestroyContext(s_context);
    }
    CHECK(spins[0] > 0.0 && fabs(spins[1] / spins[0] - 2.0) <= 1e-3, "confinement spins up");
}

static void TestConfinement3D(void)
{
    // The same vortex in each XY layer of a 3D gas of 24 by 24 by 4: the
    // curl is along Z alone, and confinement adds the same spin about Z to
    // every layer's core, nothing along Z.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 24;
    def.size[1] = 24;
    def.size[2] = 4;
    def.vorticity = 3.0f;
    const mvfxCompiledGas* gas = NULL;
    mvfxGasState* state = Open(&def, s_unturned, 1.0f, &gas);
    float* u = state->velocity[0][state->current];
    float* v = state->velocity[1][state->current];
    for (int32_t z = 0; z < 4; ++z)
    {
        for (int32_t y = 0; y < 24; ++y)
        {
            for (int32_t x = 0; x <= 24; ++x)
            {
                float dx = (float)x - 12.0f;
                float dy = (float)y + 0.5f - 12.0f;
                u[(z * 24 + y) * 25 + x] = -dy * expf(-(dx * dx + dy * dy) / 32.0f);
            }
        }
        for (int32_t y = 0; y <= 24; ++y)
        {
            for (int32_t x = 0; x < 24; ++x)
            {
                float dx = (float)x + 0.5f - 12.0f;
                float dy = (float)y - 12.0f;
                v[(z * 25 + y) * 24 + x] = dx * expf(-(dx * dx + dy * dy) / 32.0f);
            }
        }
    }
    static float beforeU[25 * 24 * 4];
    static float beforeV[24 * 25 * 4];
    memcpy(beforeU, u, sizeof beforeU);
    memcpy(beforeV, v, sizeof beforeV);
    const mvfxGasLattice l = {.n = {24, 24, 4}, .cell = def.cellSize, .h = 1.0f / 64.0f};
    float* velocity[3] = {u, v, state->velocity[2][state->current]};
    mvfxGasConfine(gas, &l, velocity, state->work);
    // Each layer's spin, from its own faces.
    bool right = AllNear(velocity[2], gas->faces[2], 0.0f, 0.0f);
    double first = 0.0;
    for (int32_t z = 0; z < 4; ++z)
    {
        double spin = 0.0;
        for (int32_t y = 0; y < 24; ++y)
        {
            for (int32_t x = 0; x <= 24; ++x)
            {
                int32_t i = (z * 24 + y) * 25 + x;
                double dx = (double)x - 12.0;
                double dy = (double)y + 0.5 - 12.0;
                spin -= dx * dx + dy * dy < 25.0 ? dy * (double)(u[i] - beforeU[i]) : 0.0;
            }
        }
        for (int32_t y = 0; y <= 24; ++y)
        {
            for (int32_t x = 0; x < 24; ++x)
            {
                int32_t i = (z * 25 + y) * 24 + x;
                double dx = (double)x + 0.5 - 12.0;
                double dy = (double)y - 12.0;
                spin += dx * dx + dy * dy < 25.0 ? dx * (double)(v[i] - beforeV[i]) : 0.0;
            }
        }
        first = z == 0 ? spin : first;
        right = right && spin > 0.0 && spin == first;
    }
    CHECK(right, "confinement in 3D");
    mvfxDestroyContext(s_context);
}

static void TestBurn(void)
{
    // Two cells of fuel 2, one at 5 degrees and one at 3, ignition at 4,
    // a burn rate of 3 over a substep of 1/32 s: the hot cell burns 2 (1 -
    // exp(-3/32)), each unit adding 10 of heat, 0.5 of smoke and
    // expansion 0.25 of its volume (0.25 0.1 32 a unit in the projection's
    // units); the cool cell is untouched.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 2;
    def.size[1] = 2;
    def.ignitionTemperature = 4.0f;
    def.burnRate = 3.0f;
    def.burnHeat = 10.0f;
    def.burnSmoke = 0.5f;
    def.burnExpansion = 0.25f;
    const mvfxCompiledGas* gas = NULL;
    mvfxGasState* state = Open(&def, s_unturned, 1.0f, &gas);
    float* scalars[3] = {state->scalars[0][0], state->scalars[1][0], state->scalars[2][0]};
    scalars[1][0] = 5.0f;
    scalars[1][1] = 3.0f;
    scalars[2][0] = 2.0f;
    scalars[2][1] = 2.0f;
    static float expansion[4];
    const mvfxGasLattice l = {.n = {2, 2, 1}, .cell = 0.1f, .h = 1.0f / 32.0f};
    mvfxGasBurn(gas, &l, scalars, expansion);
    double burned = 2.0 * (1.0 - exp(-3.0 / 32.0));
    bool right = fabs((double)scalars[2][0] - (2.0 - burned)) <= 1e-5 &&
                 fabs((double)scalars[1][0] - (5.0 + 10.0 * burned)) <= 1e-4 &&
                 fabs((double)scalars[0][0] - 0.5 * burned) <= 1e-5 &&
                 fabs((double)expansion[0] - 0.25 * 0.1 * 32.0 * burned) <= 1e-5;
    right = right && scalars[2][1] == 2.0f && scalars[1][1] == 3.0f && scalars[0][1] == 0.0f &&
            expansion[1] == 0.0f;
    CHECK(right, "fuel burned where hot");
    mvfxDestroyContext(s_context);
}

// The divergence of a 2D gas's cell, its faces' outflow.
static float Outflow(const mvfxGasState* state, const int32_t n[3], int32_t x, int32_t y)
{
    const float* u = state->velocity[0][state->current];
    const float* v = state->velocity[1][state->current];
    return u[y * (n[0] + 1) + x + 1] - u[y * (n[0] + 1) + x] + v[(y + 1) * n[0] + x] -
           v[y * n[0] + x];
}

static void TestExpansion(void)
{
    // A still 2D gas of 16 by 16 cells asked to expand by 1 in a 2 by 2
    // patch: 8 V cycles leave that divergence there and none elsewhere,
    // within 1e-3; sealed, each cell is left 1/64 less (the mean, which
    // has nowhere to go).
    static float expansion[256];
    bool right = true;
    for (int32_t sealed = 0; sealed < 2; ++sealed)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 16;
        def.size[1] = 16;
        def.pressureCycles = 8;
        def.closedWalls = sealed != 0 ? 63 : 0;
        const mvfxCompiledGas* gas = NULL;
        mvfxGasState* state = Open(&def, s_unturned, 1.0f, &gas);
        for (int32_t i = 0; i < 256; ++i)
        {
            int32_t x = i % 16;
            int32_t y = i / 16;
            expansion[i] = (x == 7 || x == 8) && (y == 7 || y == 8) ? 1.0f : 0.0f;
        }
        mvfxProjectGas(gas, state, expansion);
        float mean = sealed != 0 ? 4.0f / 256.0f : 0.0f;
        for (int32_t i = 0; i < 256; ++i)
        {
            float d = Outflow(state, def.size, i % 16, i / 16);
            right = right && fabsf(d - (expansion[i] - mean)) <= 1e-3f;
        }
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "the expansion's divergence kept");
}

// The largest value and the sum of a 2D gas's density.
static void Spread(const mvfxCompiledGas* gas, const mvfxGasState* state, float* most, double* sum)
{
    *most = -INFINITY;
    *sum = 0.0;
    for (int64_t i = 0; i < gas->cells; ++i)
    {
        float d = state->scalars[0][state->current][i];
        *most = fmaxf(*most, d);
        *sum += (double)d;
    }
}

static void TestMacCormack(void)
{
    // A square of density 1 in a 2D gas of 48 by 16, carried 0.37 cells a
    // step to the right for 40 steps, no pressure, closed walls: with
    // MacCormack its peak stays higher than semi-Lagrangian's, its values
    // within 0 and 1 (no new extremes), its sum within 5% of its start.
    float peaks[2];
    bool right = true;
    for (int32_t r = 0; r < 2; ++r)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 48;
        def.size[1] = 16;
        def.pressureCycles = 0;
        def.advection = r == 0 ? mvfx_gasSemiLagrangian : mvfx_gasMacCormack;
        const mvfxCompiledGas* gas = NULL;
        mvfxGasState* state = Open(&def, s_unturned, 1.0f, &gas);
        for (int32_t y = 5; y < 11; ++y)
        {
            for (int32_t x = 4; x < 10; ++x)
            {
                state->scalars[0][0][y * 48 + x] = 1.0f;
            }
        }
        for (int64_t f = 0; f < gas->faces[0]; ++f)
        {
            state->velocity[0][0][f] = 0.37f * 0.1f * 64.0f;
        }
        Steps(40);
        double sum = 0.0;
        Spread(gas, state, &peaks[r], &sum);
        float least = INFINITY;
        for (int64_t i = 0; i < gas->cells; ++i)
        {
            least = fminf(least, state->scalars[0][state->current][i]);
        }
        right = right && peaks[r] <= 1.0f && least >= 0.0f && fabs(sum - 36.0) <= 0.05 * 36.0;
        mvfxDestroyContext(s_context);
    }
    CHECK(right && peaks[1] > peaks[0] + 0.05f, "MacCormack sharper, bounded");
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

static void TestRefusals(void)
{
    // Each new field out of range, the rest valid.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    bool right = true;
    for (int32_t c = 0; c < 10; ++c)
    {
        mvfxGasDef gas = mvfxDefaultGasDef();
        float* weights[7] = {&gas.densityWeight, &gas.heatLift,  &gas.vorticity,    &gas.burnRate,
                             &gas.burnHeat,      &gas.burnSmoke, &gas.burnExpansion};
        if (c < 7)
        {
            *weights[c] = -0.5f;
        }
        gas.advection = c == 7 ? 2 : gas.advection;
        gas.gravitySpace = c == 8 ? 2 : gas.gravitySpace;
        gas.gravity[2] = c == 9 ? NAN : gas.gravity[2];
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.gases = &gas;
        def.gasCount = 1;
        mvfxEffectId effect = {0};
        right = right && mvfxCreateEffect(s_context, &def, &effect) == mvfx_errorInvalid;
    }
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.ignitionTemperature = -20.0f;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    right = right && mvfxCreateEffect(s_context, &def, &effect) == mvfx_success;
    CHECK(right, "the forces' refusals");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestBuoyancy();
    TestConfinement();
    TestConfinement3D();
    TestBurn();
    TestExpansion();
    TestMacCormack();
    TestWork();
    TestRefusals();
    return s_failures;
}
