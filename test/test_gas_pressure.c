// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's pressure projection: the multigrid's pressure against a direct
// solve of the same equations on small grids; the divergence after 4 and
// 8 V cycles, and its fall in the early cycles, over 2D and 3D boxes of even and
// odd sizes with open, closed and mixed walls; closed walls' faces held
// at 0; the Taylor-Green vortex in a closed box kept free of divergence,
// its energy falling every step and by under a tenth in a second.

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

// Opens an instance of an effect of one gas, giving its compiled gas and
// state.
static mvfxGasState* Open(const mvfxGasDef* gas, const mvfxCompiledGas** compiled)
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
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxInstance* live = mvfxContextInstance(s_context, s_instance);
    *compiled = &live->effect->gases[0];
    return &live->gases[0];
}

// A hash's float in [-1, 1).
static float Noise(uint32_t i)
{
    uint32_t h = (i + 1u) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (float)(h & 0xFFFFFFu) / 8388608.0f - 1.0f;
}

// Fills the current velocity with noise and the pressure with 0.
static void Fill(const mvfxCompiledGas* gas, mvfxGasState* state)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        for (int64_t f = 0; f < gas->faces[k]; ++f)
        {
            state->velocity[k][state->current][f] = Noise((uint32_t)(f * 3 + k));
        }
    }
    memset(state->levels[0].pressure, 0, (size_t)gas->cells * sizeof(float));
}

// The faces' array index of the face across k before cell (x, y, z).
static int64_t Face(const int32_t* n, int32_t k, int32_t x, int32_t y, int32_t z)
{
    const int32_t m[3] = {n[0] + (k == 0 ? 1 : 0), n[1] + (k == 1 ? 1 : 0),
                          n[2] + (k == 2 ? 1 : 0)};
    return ((int64_t)z * m[1] + y) * m[0] + x;
}

// A cell's divergence: its faces' outflow.
static float DivergenceAt(const int32_t* n, float* const v[3], int32_t x, int32_t y, int32_t z)
{
    float d = v[0][Face(n, 0, x + 1, y, z)] - v[0][Face(n, 0, x, y, z)];
    d += v[1][Face(n, 1, x, y + 1, z)] - v[1][Face(n, 1, x, y, z)];
    return d + (n[2] > 1 ? v[2][Face(n, 2, x, y, z + 1)] - v[2][Face(n, 2, x, y, z)] : 0.0f);
}

// The largest divergence's size.
static float MaxDivergence(const mvfxCompiledGas* gas, const mvfxGasState* state)
{
    const int32_t* n = gas->def.size;
    float* const v[3] = {state->velocity[0][state->current], state->velocity[1][state->current],
                         state->velocity[2][state->current]};
    float most = 0.0f;
    for (int32_t z = 0; z < n[2]; ++z)
    {
        for (int32_t y = 0; y < n[1]; ++y)
        {
            for (int32_t x = 0; x < n[0]; ++x)
            {
                most = fmaxf(most, fabsf(DivergenceAt(n, v, x, y, z)));
            }
        }
    }
    return most;
}

// Solves a dense system a x = b of n unknowns by Gaussian elimination with
// partial pivoting, in place.
static void Solve(double* a, double* b, int32_t n)
{
    for (int32_t c = 0; c < n; ++c)
    {
        int32_t pivot = c;
        for (int32_t r = c + 1; r < n; ++r)
        {
            pivot = fabs(a[r * n + c]) > fabs(a[pivot * n + c]) ? r : pivot;
        }
        for (int32_t k = 0; k < n; ++k)
        {
            double swap = a[c * n + k];
            a[c * n + k] = a[pivot * n + k];
            a[pivot * n + k] = swap;
        }
        double swap = b[c];
        b[c] = b[pivot];
        b[pivot] = swap;
        for (int32_t r = c + 1; r < n; ++r)
        {
            double f = a[r * n + c] / a[c * n + c];
            for (int32_t k = c; k < n; ++k)
            {
                a[r * n + k] -= f * a[c * n + k];
            }
            b[r] -= f * b[c];
        }
    }
    for (int32_t r = n - 1; r >= 0; --r)
    {
        for (int32_t k = r + 1; k < n; ++k)
        {
            b[r] -= a[r * n + k] * b[k];
        }
        b[r] /= a[r * n + r];
    }
}

static void TestDirect(void)
{
    // A 2D gas of 7 by 5 cells, all walls open, and with its low X and
    // high Y walls closed: the equations written here (each neighbor
    // inside weighs 1, an open wall 2 with the pressure 0 on it, a closed
    // one 0) solved directly, against 16 V cycles from 0.
    static double a[35 * 35];
    static double b[35];
    static const mvfxGasWalls walls[2] = {0, mvfx_gasWallMinX | mvfx_gasWallMaxY};
    bool right = true;
    for (int32_t w = 0; w < 2; ++w)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 7;
        def.size[1] = 5;
        def.closedWalls = walls[w];
        def.pressureCycles = 16;
        const mvfxCompiledGas* gas = NULL;
        mvfxGasState* state = Open(&def, &gas);
        Fill(gas, state);
        mvfxCloseGasWalls(gas, (float* const[3]){state->velocity[0][state->current],
                                                 state->velocity[1][state->current],
                                                 state->velocity[2][state->current]});
        memset(a, 0, sizeof a);
        for (int32_t y = 0; y < 5; ++y)
        {
            for (int32_t x = 0; x < 7; ++x)
            {
                int32_t i = y * 7 + x;
                float* const v[3] = {state->velocity[0][state->current],
                                     state->velocity[1][state->current],
                                     state->velocity[2][state->current]};
                b[i] = (double)DivergenceAt(def.size, v, x, y, 0);
                const int32_t near[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
                const mvfxGasWalls sides[4] = {mvfx_gasWallMinX, mvfx_gasWallMaxX, mvfx_gasWallMinY,
                                               mvfx_gasWallMaxY};
                for (int32_t s = 0; s < 4; ++s)
                {
                    bool inside =
                        near[s][0] >= 0 && near[s][0] < 7 && near[s][1] >= 0 && near[s][1] < 5;
                    double c = inside ? 1.0 : ((def.closedWalls & sides[s]) != 0 ? 0.0 : 2.0);
                    a[i * 35 + i] -= c;
                    if (inside)
                    {
                        a[i * 35 + near[s][1] * 7 + near[s][0]] += c;
                    }
                }
            }
        }
        Solve(a, b, 35);
        mvfxProjectGas(gas, state);
        double most = 0.0;
        double error = 0.0;
        for (int32_t i = 0; i < 35; ++i)
        {
            most = fmax(most, fabs(b[i]));
            error = fmax(error, fabs((double)state->levels[0].pressure[i] - b[i]));
        }
        right = right && error <= 1e-4 * most && MaxDivergence(gas, state) <= 1e-4f;
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "the multigrid's pressure the direct solve's");
}

static void TestDivergence(void)
{
    // Noise for a velocity over boxes of even and odd sizes in 2D and 3D,
    // all walls open, all closed and some of each: 4 V cycles from 0 leave
    // at most 1e-3 of the largest divergence, 8 at most 5e-5 (by then
    // near float's round-off), and cycles 2 to 4 each leave at most 0.4
    // of it (about 0.3 seen).
    static const int32_t sizes[4][3] = {{64, 64, 1}, {33, 20, 1}, {32, 24, 16}, {17, 9, 5}};
    static const mvfxGasWalls walls[3] = {0, 63,
                                          mvfx_gasWallMinY | mvfx_gasWallMaxX | mvfx_gasWallMinZ};
    bool right = true;
    for (int32_t s = 0; s < 4; ++s)
    {
        for (int32_t w = 0; w < 3; ++w)
        {
            mvfxGasDef def = mvfxDefaultGasDef();
            memcpy(def.size, sizes[s], sizeof def.size);
            def.closedWalls = walls[w];
            const mvfxCompiledGas* gas = NULL;
            mvfxGasState* state = Open(&def, &gas);
            mvfxGasDef* cycles = (mvfxGasDef*)&gas->def;
            float after[9];
            cycles->pressureCycles = 0;
            Fill(gas, state);
            mvfxProjectGas(gas, state);
            after[0] = MaxDivergence(gas, state);
            for (int32_t c = 1; c <= 8; ++c)
            {
                cycles->pressureCycles = c;
                Fill(gas, state);
                mvfxProjectGas(gas, state);
                after[c] = MaxDivergence(gas, state);
                right = right && (c < 2 || c > 4 || after[c] <= 0.4f * after[c - 1]);
            }
            right = right && after[4] <= 1e-3f * after[0] && after[8] <= 5e-5f * after[0];
            mvfxDestroyContext(s_context);
        }
    }
    CHECK(right, "the divergence removed");
}

static void TestWalls(void)
{
    // A 2D gas, its low X and high Y walls closed: after a projection those
    // walls' faces are 0, the open ones' not, and its Z faces are 0.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 12;
    def.size[1] = 10;
    def.closedWalls = mvfx_gasWallMinX | mvfx_gasWallMaxY;
    const mvfxCompiledGas* gas = NULL;
    mvfxGasState* state = Open(&def, &gas);
    Fill(gas, state);
    mvfxProjectGas(gas, state);
    float* const* v =
        (float* const[3]){state->velocity[0][state->current], state->velocity[1][state->current],
                          state->velocity[2][state->current]};
    bool closed = true;
    bool open = false;
    for (int32_t y = 0; y < 10; ++y)
    {
        closed = closed && v[0][Face(def.size, 0, 0, y, 0)] == 0.0f;
        open = open || v[0][Face(def.size, 0, 12, y, 0)] != 0.0f;
    }
    for (int32_t x = 0; x < 12; ++x)
    {
        closed = closed && v[1][Face(def.size, 1, x, 10, 0)] == 0.0f;
        open = open || v[1][Face(def.size, 1, x, 0, 0)] != 0.0f;
    }
    for (int64_t f = 0; f < gas->faces[2]; ++f)
    {
        closed = closed && v[2][f] == 0.0f;
    }
    CHECK(closed && open, "closed walls' faces held at 0");
    mvfxDestroyContext(s_context);
}

// The kinetic energy's sum over the faces.
static double Energy(const mvfxCompiledGas* gas, const mvfxGasState* state)
{
    double e = 0.0;
    for (int32_t k = 0; k < 2; ++k)
    {
        for (int64_t f = 0; f < gas->faces[k]; ++f)
        {
            double w = (double)state->velocity[k][state->current][f];
            e += w * w;
        }
    }
    return e;
}

static void TestTaylorGreen(void)
{
    // The Taylor-Green vortex u = sin x cos y, v = -cos x sin y in a closed
    // box of side pi (32 by 32 cells), which holds it still but for the
    // advection's own dissipation: a second of steps of 1/64 s keeps it
    // free of divergence (at most 1e-5), its energy falling every step,
    // by at most a tenth in all (8% at 32 cells).
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 32;
    def.cellSize = 3.14159265f / 32.0f;
    def.closedWalls = 63;
    const mvfxCompiledGas* gas = NULL;
    mvfxGasState* state = Open(&def, &gas);
    const float h = def.cellSize;
    float* u = state->velocity[0][state->current];
    float* v = state->velocity[1][state->current];
    for (int32_t y = 0; y < 32; ++y)
    {
        for (int32_t x = 0; x <= 32; ++x)
        {
            u[y * 33 + x] = sinf((float)x * h) * cosf(((float)y + 0.5f) * h);
        }
    }
    for (int32_t y = 0; y <= 32; ++y)
    {
        for (int32_t x = 0; x < 32; ++x)
        {
            v[y * 32 + x] = -cosf(((float)x + 0.5f) * h) * sinf((float)y * h);
        }
    }
    double start = Energy(gas, state);
    double last = start;
    bool falling = true;
    bool free = true;
    for (int32_t i = 0; i < 64; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
        double e = Energy(gas, state);
        falling = falling && e < last;
        free = free && MaxDivergence(gas, state) <= 1e-5f;
        last = e;
    }
    CHECK(falling && free && last >= 0.9 * start, "the Taylor-Green vortex kept");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestDirect();
    TestDivergence();
    TestWalls();
    TestTaylorGreen();
    return s_failures;
}
