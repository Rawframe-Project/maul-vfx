// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's forces, combustion and advection: buoyancy's size and direction
// in the world's gravity (its instance turned and scaled) and in the
// box's own; vorticity confinement strengthening a vortex about its peak,
// in proportion to its strength, the same whatever the cells' size, about
// each axis in 3D, and nothing in an even curl; a wall face's force its
// one cell's; fuel burning where it is hot enough (at the ignition
// temperature too), to heat, smoke and expansion; the projection keeping
// the expansion's divergence (less its mean in a sealed box); MacCormack's
// advection sharper than semi-Lagrangian, making no new extremes, and
// nearer a moved Gaussian; faces held within the speed bound at the
// forces' limits; the def's refusals. A gas's faces are read and
// written, and its scalars written, through the test hooks; confinement
// is seen through a step of 1/64 s in cells of at least 4e5, which
// carries nothing, without a projection.

#include "test_harness.h"
#include "test_hooks.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

enum
{
    // The most cells, and faces along an axis, a test's gas has.
    MOST = 17 * 16 * 16
};

// The gas's faces along each axis, as read or to be written.
static float s_faces[3][MOST];

// Opens an instance of an effect of one gas, its origin at 0, turned by
// rotation and of scale.
static void Open(const mvfxGasDef* gas, const float rotation[4], float scale)
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
}

// The count of a gas's faces along k.
static int64_t FaceCount(const int32_t* n, int32_t k)
{
    return (int64_t)(n[0] + (k == 0 ? 1 : 0)) * (n[1] + (k == 1 ? 1 : 0)) *
           (n[2] + (k == 2 ? 1 : 0));
}

// Reads the gas's faces into s_faces.
static void Read(const int32_t* n)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        CHECK(mvfxTestReadGasFaces(s_context, s_instance, 0, k, s_faces[k], MOST) ==
                  FaceCount(n, k),
              "faces read");
    }
}

// Writes s_faces into the gas's faces.
static void Write(const int32_t* n)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        CHECK(mvfxTestWriteGasFaces(s_context, s_instance, 0, k, s_faces[k], FaceCount(n, k)),
              "faces written");
    }
}

// Writes a value into every cell of a gas's scalar field.
static void Uniform(const int32_t* n, mvfxGasField field, float value)
{
    static float cells[MOST];
    int64_t count = (int64_t)n[0] * n[1] * n[2];
    for (int64_t i = 0; i < count; ++i)
    {
        cells[i] = value;
    }
    CHECK(mvfxTestWriteGasScalar(s_context, s_instance, 0, field, cells, count), "scalar written");
}

// Reads a gas's scalar field into out.
static void Field(const int32_t* n, mvfxGasField field, float* out)
{
    CHECK(mvfxInstance_ReadGas(s_context, s_instance, 0, field, out, (int64_t)n[0] * n[1] * n[2]) ==
              mvfx_success,
          "scalar read");
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
        Open(&def, cases[c].rotation, cases[c].scale);
        Uniform(def.size, mvfx_gasDensity, 0.5f);
        Uniform(def.size, mvfx_gasTemperature, 2.0f);
        Steps(1);
        Read(def.size);
        int32_t a = cases[c].axis;
        right = right && AllNear(s_faces[a], FaceCount(def.size, a), cases[c].speed, 1e-5f) &&
                AllNear(s_faces[1 - a], FaceCount(def.size, 1 - a), 0.0f, 1e-5f);
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "buoyancy's size and direction");
}

// Sets s_faces to a Gaussian vortex about the middle of a 2D gas of n by
// n cells, turning counterclockwise, its speed peaking 4 cells out.
static void Vortex(int32_t n)
{
    memset(s_faces, 0, sizeof s_faces);
    float* u = s_faces[0];
    float* v = s_faces[1];
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
}

// The angular momentum about a 2D gas's middle, counterclockwise, from
// the change of its velocity since before, over the faces within 5 cells
// of the middle, as read.
static double Spin(const float* beforeU, const float* beforeV, int32_t n)
{
    const float* u = s_faces[0];
    const float* v = s_faces[1];
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
    // cells it turns the other way), the second twice the first's. With
    // cells of 4e5 instead of 1e6 the same velocities, cell for cell, get
    // the same spin: the curl goes as 1 / h and the force as h times it.
    static float beforeU[33 * 32];
    static float beforeV[32 * 33];
    double spins[3];
    for (int32_t r = 0; r < 3; ++r)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 32;
        def.size[1] = 32;
        def.vorticity = r == 1 ? 4.0f : 2.0f;
        def.cellSize = r == 2 ? 4e5f : 1e6f;
        def.pressureCycles = 0;
        Open(&def, s_unturned, 1.0f);
        Vortex(32);
        Write(def.size);
        memcpy(beforeU, s_faces[0], sizeof beforeU);
        memcpy(beforeV, s_faces[1], sizeof beforeV);
        Steps(1);
        Read(def.size);
        spins[r] = Spin(beforeU, beforeV, 32);
        mvfxDestroyContext(s_context);
    }
    CHECK(spins[0] > 0.0 && fabs(spins[1] / spins[0] - 2.0) <= 1e-3 &&
              fabs(spins[2] / spins[0] - 1.0) <= 1e-3,
          "confinement spins up");
}

// Whether a 2D gas of 16 by 16 cells of cell at the forces' limits holds
// each face within MVFX_MAX_SPEED, reaching it, and every field finite
// over two steps of a second, with cycles of pressure: a source pouring
// density at MVFX_MAX_EXTENT a second, buoyancy at gravity and weight
// MVFX_MAX_EXTENT, confinement at MVFX_MAX_EXTENT, eight substeps.
static bool Held(float cell, int32_t cycles)
{
    const mvfxGasSource source = {.shape = mvfx_gasSphere,
                                  .center = {0.0f, -4.0f * cell, 0.0f},
                                  .radius = 3.0f * cell,
                                  .density = MVFX_MAX_EXTENT};
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 16;
    def.size[1] = 16;
    def.cellSize = cell;
    def.substeps = 8;
    def.pressureCycles = cycles;
    def.sources = &source;
    def.sourceCount = 1;
    def.gravitySpace = mvfx_gasLocal;
    def.gravity[1] = -MVFX_MAX_EXTENT;
    def.densityWeight = MVFX_MAX_EXTENT;
    def.vorticity = MVFX_MAX_EXTENT;
    Open(&def, s_unturned, 1.0f);
    bool held = true;
    float fastest = 0.0f;
    for (int32_t s = 0; s < 2; ++s)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
        Read(def.size);
        for (int32_t k = 0; k < 2; ++k)
        {
            for (int64_t f = 0; f < FaceCount(def.size, k); ++f)
            {
                held = held && fabsf(s_faces[k][f]) <= MVFX_MAX_SPEED;
                fastest = fmaxf(fastest, fabsf(s_faces[k][f]));
            }
        }
        static float cells[16 * 16];
        for (int32_t field = 0; field < 3; ++field)
        {
            Field(def.size, (mvfxGasField)field, cells);
            for (int32_t i = 0; i < 16 * 16; ++i)
            {
                held = held && isfinite(cells[i]);
            }
        }
    }
    mvfxDestroyContext(s_context);
    return held && fastest == MVFX_MAX_SPEED;
}

static void TestHeld(void)
{
    // Buoyancy alone would make the faces about 10^16 in the first
    // substep, and confinement squares their curl, a velocity over the
    // cell: in cells of 1 and of MVFX_MIN_GAS_CELL, and without a solve.
    CHECK(Held(1.0f, 4) && Held(MVFX_MIN_GAS_CELL, 4) && Held(1.0f, 0), "held at the limits");
}

static void TestExpansionHeld(void)
{
    // Fuel of 10^30 in a cell (stored unburnt over far longer than any run,
    // written here) lit at once over a substep of a tick at the largest
    // burn rate and expansion: the divergence it asks for, about 6e41,
    // passes binary32's largest. Held within the most a cell's faces carry
    // out of it, each within MVFX_MAX_SPEED, the solve and every face stay
    // finite and held.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 16;
    def.size[1] = 16;
    def.cellSize = 1.0f;
    def.pressureCycles = 4;
    def.burnRate = MVFX_MAX_EXTENT;
    def.burnExpansion = MVFX_MAX_EXTENT;
    Open(&def, s_unturned, 1.0f);
    Uniform(def.size, mvfx_gasFuel, 0.0f);
    static float fuel[256];
    for (int32_t i = 0; i < 256; ++i)
    {
        fuel[i] = i % 16 == 7 && i / 16 == 7 ? 1e30f : 0.0f;
    }
    CHECK(mvfxTestWriteGasScalar(s_context, s_instance, 0, mvfx_gasFuel, fuel, 256), "fuel");
    Uniform(def.size, mvfx_gasTemperature, 1.0f);
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "step");
    Read(def.size);
    bool held = true;
    float fastest = 0.0f;
    for (int32_t k = 0; k < 2; ++k)
    {
        for (int64_t f = 0; f < FaceCount(def.size, k); ++f)
        {
            held = held && fabsf(s_faces[k][f]) <= MVFX_MAX_SPEED;
            fastest = fmaxf(fastest, fabsf(s_faces[k][f]));
        }
    }
    CHECK(held && fastest > 1e8f, "a burn's expansion held");
    mvfxDestroyContext(s_context);
}

// The spin about axis a through a 16-cell cube's middle of its velocity's
// change since before, over the faces within 4 cells of that axis, and
// the largest change along a.
static double Spin3D(float* const velocity[3], float* const before[3], int32_t a, float* along)
{
    int32_t b = (a + 1) % 3;
    int32_t c = (a + 2) % 3;
    double spin = 0.0;
    *along = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        for (int32_t i = 0; i < 17 * 16 * 16; ++i)
        {
            const int32_t m[3] = {16 + (k == 0 ? 1 : 0), 16 + (k == 1 ? 1 : 0),
                                  16 + (k == 2 ? 1 : 0)};
            const int32_t at[3] = {i % m[0], i / m[0] % m[1], i / (m[0] * m[1])};
            double p[3];
            for (int32_t j = 0; j < 3; ++j)
            {
                p[j] = (double)at[j] + (j == k ? 0.0 : 0.5) - 8.0;
            }
            double change = (double)(velocity[k][i] - before[k][i]);
            *along = k == a ? fmaxf(*along, fabsf((float)change)) : *along;
            bool core = p[b] * p[b] + p[c] * p[c] < 16.0;
            spin += core && k == c ? p[b] * change : 0.0;
            spin -= core && k == b ? p[c] * change : 0.0;
        }
    }
    return spin;
}

static void TestConfinement3D(void)
{
    // A Gaussian vortex about each axis in turn through the middle of a 3D
    // gas of 16 cells along each: confinement adds spin about that axis to
    // its core, the same about each (the cube's symmetry), nothing along
    // the axis.
    static float faces[3][17 * 16 * 16];
    static float before[3][17 * 16 * 16];
    double spins[3];
    bool right = true;
    for (int32_t a = 0; a < 3; ++a)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 16;
        def.size[1] = 16;
        def.size[2] = 16;
        def.vorticity = 3.0f;
        def.cellSize = 1e6f;
        def.pressureCycles = 0;
        Open(&def, s_unturned, 1.0f);
        int32_t b = (a + 1) % 3;
        int32_t c = (a + 2) % 3;
        for (int32_t k = 0; k < 3; ++k)
        {
            for (int32_t i = 0; i < 17 * 16 * 16; ++i)
            {
                const int32_t m[3] = {16 + (k == 0 ? 1 : 0), 16 + (k == 1 ? 1 : 0),
                                      16 + (k == 2 ? 1 : 0)};
                const int32_t at[3] = {i % m[0], i / m[0] % m[1], i / (m[0] * m[1])};
                float p[3];
                for (int32_t j = 0; j < 3; ++j)
                {
                    p[j] = (float)at[j] + (j == k ? 0.0f : 0.5f) - 8.0f;
                }
                float g = expf(-(p[b] * p[b] + p[c] * p[c]) / 16.0f);
                faces[k][i] = k == b ? -p[c] * g : (k == c ? p[b] * g : 0.0f);
            }
            memcpy(s_faces[k], faces[k], sizeof faces[k]);
            memcpy(before[k], faces[k], sizeof faces[k]);
        }
        Write(def.size);
        Steps(1);
        Read(def.size);
        float* velocity[3] = {s_faces[0], s_faces[1], s_faces[2]};
        float* was[3] = {before[0], before[1], before[2]};
        float along = 0.0f;
        spins[a] = Spin3D(velocity, was, a, &along);
        right = right && spins[a] > 0.0 && along == 0.0f;
        mvfxDestroyContext(s_context);
    }
    right =
        right && fabs(spins[1] / spins[0] - 1.0) <= 1e-4 && fabs(spins[2] / spins[0] - 1.0) <= 1e-4;
    CHECK(right, "confinement in 3D");
}

static void TestEvenCurl(void)
{
    // A 2D gas of 24 by 16 in a uniform shear, u rising by 0.3 a cell up
    // Y: its curl, -0.3 over the cell's size, the same at every cell
    // (one-sided at the edges, as exact for it as the central differences
    // inside). Confinement at 2 a second changes no face by more than a
    // hundredth of epsilon h |w| over the substep: float's noise in an
    // even curl makes no direction.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 24;
    def.size[1] = 16;
    def.cellSize = 1e6f;
    def.pressureCycles = 0;
    def.vorticity = 2.0f;
    Open(&def, s_unturned, 1.0f);
    memset(s_faces, 0, sizeof s_faces);
    float* u = s_faces[0];
    for (int32_t y = 0; y < 16; ++y)
    {
        for (int32_t x = 0; x <= 24; ++x)
        {
            u[y * 25 + x] = 0.3f * ((float)y + 0.5f);
        }
    }
    static float before[25 * 16];
    memcpy(before, u, sizeof before);
    Write(def.size);
    Steps(1);
    Read(def.size);
    float* velocity[3] = {u, s_faces[1], s_faces[2]};
    float bound = 0.01f * 2.0f * 0.3f / 64.0f;
    float most = 0.0f;
    for (int32_t i = 0; i < 25 * 16; ++i)
    {
        most = fmaxf(most, fabsf(u[i] - before[i]));
    }
    for (int32_t i = 0; i < 24 * 17; ++i)
    {
        most = fmaxf(most, fabsf(velocity[1][i]));
    }
    CHECK(most <= bound, "an even curl not confined");
    mvfxDestroyContext(s_context);
}

static void TestColumns(void)
{
    // A 2D gas of 12 by 10 cells with v = X + X^2 / 2 (X from its
    // corner), u = 0, open walls: its curl, 1 + X, grows along X alone, so
    // each cell's force is its column's, along -Y. Every Y face of a
    // column changes alike, the wall faces (their one cell's force) as the
    // faces between two cells (their mean), and by something.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 12;
    def.size[1] = 10;
    def.cellSize = 1e6f;
    def.pressureCycles = 0;
    def.vorticity = 2.0f;
    Open(&def, s_unturned, 1.0f);
    memset(s_faces, 0, sizeof s_faces);
    float* v = s_faces[1];
    for (int32_t y = 0; y <= 10; ++y)
    {
        for (int32_t x = 0; x < 12; ++x)
        {
            float place = ((float)x + 0.5f) * 0.1f;
            v[y * 12 + x] = place + 0.5f * place * place;
        }
    }
    static float before[12 * 11];
    memcpy(before, v, sizeof before);
    Write(def.size);
    Steps(1);
    Read(def.size);
    bool right = true;
    for (int32_t x = 0; x < 12; ++x)
    {
        float change = v[x] - before[x];
        right = right && change < 0.0f;
        for (int32_t y = 1; y <= 10; ++y)
        {
            right = right && v[y * 12 + x] - before[y * 12 + x] == change;
        }
    }
    CHECK(right, "a wall face's force its cell's");
    mvfxDestroyContext(s_context);
}

// The divergence of a 2D gas's cell, its faces' outflow, as read.
static float Outflow(const int32_t n[3], int32_t x, int32_t y)
{
    const float* u = s_faces[0];
    const float* v = s_faces[1];
    return u[y * (n[0] + 1) + x + 1] - u[y * (n[0] + 1) + x] + v[(y + 1) * n[0] + x] -
           v[y * n[0] + x];
}

static void TestBurn(void)
{
    // Two cells of fuel 2, one at 5 degrees and one at 3, ignition at 4,
    // a burn rate of 3 over a step of 1/32 s, still, open walls: the hot
    // cell burns 2 (1 - exp(-3/32)), each unit adding 10 of heat, 0.5 of
    // smoke and expansion 0.25 of its volume, which the projection leaves
    // as its divergence (0.25 0.1 32 a unit in the projection's units);
    // the cool cell is untouched; a cell at 4 exactly burns.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 2;
    def.size[1] = 2;
    def.pressureCycles = 8;
    def.ignitionTemperature = 4.0f;
    def.burnRate = 3.0f;
    def.burnHeat = 10.0f;
    def.burnSmoke = 0.5f;
    def.burnExpansion = 0.25f;
    Open(&def, s_unturned, 1.0f);
    const float heat[4] = {5.0f, 3.0f, 4.0f, 0.0f};
    const float fuel[4] = {2.0f, 2.0f, 2.0f, 0.0f};
    CHECK(mvfxTestWriteGasScalar(s_context, s_instance, 0, mvfx_gasTemperature, heat, 4) &&
              mvfxTestWriteGasScalar(s_context, s_instance, 0, mvfx_gasFuel, fuel, 4),
          "fuel");
    CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 32) == mvfx_success, "step");
    float scalars[3][4];
    Field(def.size, mvfx_gasDensity, scalars[0]);
    Field(def.size, mvfx_gasTemperature, scalars[1]);
    Field(def.size, mvfx_gasFuel, scalars[2]);
    Read(def.size);
    double burned = 2.0 * (1.0 - exp(-3.0 / 32.0));
    double grown = 0.25 * 0.1 * 32.0 * burned;
    bool right = fabs((double)scalars[2][0] - (2.0 - burned)) <= 1e-5 &&
                 fabs((double)scalars[1][0] - (5.0 + 10.0 * burned)) <= 1e-4 &&
                 fabs((double)scalars[0][0] - 0.5 * burned) <= 1e-5 &&
                 fabs((double)Outflow(def.size, 0, 0) - grown) <= 1e-5;
    right = right && scalars[2][1] == 2.0f && scalars[1][1] == 3.0f && scalars[0][1] == 0.0f &&
            fabsf(Outflow(def.size, 1, 0)) <= 1e-5f && scalars[2][2] == scalars[2][0];
    CHECK(right, "fuel burned where hot");
    mvfxDestroyContext(s_context);
}

static void TestExpansion(void)
{
    // A still 2D gas of 16 by 16 cells, fuel of 1 burning in a 2 by 2
    // patch, each of those cells growing by burnExpansion over the step for
    // each unit burned (the fuel the step took from it): 8 V cycles leave
    // that divergence there and none elsewhere, within 1e-3; sealed, each
    // cell is left their mean less (it has nowhere to go).
    static float fuel[2][256];
    static float expansion[256];
    bool right = true;
    for (int32_t sealed = 0; sealed < 2; ++sealed)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 16;
        def.size[1] = 16;
        def.pressureCycles = 8;
        def.closedWalls = sealed != 0 ? 63 : 0;
        def.burnRate = 10.0f;
        def.burnExpansion = 1.0f;
        Open(&def, s_unturned, 1.0f);
        for (int32_t i = 0; i < 256; ++i)
        {
            int32_t x = i % 16;
            int32_t y = i / 16;
            fuel[0][i] = (x == 7 || x == 8) && (y == 7 || y == 8) ? 1.0f : 0.0f;
        }
        CHECK(mvfxTestWriteGasScalar(s_context, s_instance, 0, mvfx_gasFuel, fuel[0], 256), "fuel");
        Uniform(def.size, mvfx_gasTemperature, 1.0f);
        Steps(1);
        Field(def.size, mvfx_gasFuel, fuel[1]);
        Read(def.size);
        // A unit burned grows its cell by burnExpansion over the step of
        // 1/64 s, in the projection's units its edge times that over the
        // step.
        float sum = 0.0f;
        for (int32_t i = 0; i < 256; ++i)
        {
            expansion[i] = def.burnExpansion * def.cellSize * 64.0f * (fuel[0][i] - fuel[1][i]);
            sum += expansion[i];
        }
        float mean = sealed != 0 ? sum / 256.0f : 0.0f;
        right = right && sum > 0.0f;
        for (int32_t i = 0; i < 256; ++i)
        {
            float d = Outflow(def.size, i % 16, i / 16);
            right = right && fabsf(d - (expansion[i] - mean)) <= 1e-3f;
        }
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "the expansion's divergence kept");
}

// Sets a 2D gas's X faces to carry it 0.37 cells of 0.1 a step of 1/64 s
// to the right, its Y faces still.
static void Carrying(const int32_t* n)
{
    memset(s_faces, 0, sizeof s_faces);
    for (int64_t f = 0; f < FaceCount(n, 0); ++f)
    {
        s_faces[0][f] = 0.37f * 0.1f * 64.0f;
    }
    Write(n);
}

static void TestMacCormack(void)
{
    // A square of density 1 in a 2D gas of 48 by 16, carried 0.37 cells a
    // step to the right for 40 steps, no pressure, closed walls: with
    // MacCormack its peak stays higher than semi-Lagrangian's, its values
    // within 0 and 1 (no new extremes), its sum within 5% of its start.
    static float density[48 * 16];
    float peaks[2];
    bool right = true;
    for (int32_t r = 0; r < 2; ++r)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 48;
        def.size[1] = 16;
        def.pressureCycles = 0;
        def.advection = r == 0 ? mvfx_gasSemiLagrangian : mvfx_gasMacCormack;
        Open(&def, s_unturned, 1.0f);
        memset(density, 0, sizeof density);
        for (int32_t y = 5; y < 11; ++y)
        {
            for (int32_t x = 4; x < 10; ++x)
            {
                density[y * 48 + x] = 1.0f;
            }
        }
        CHECK(mvfxTestWriteGasScalar(s_context, s_instance, 0, mvfx_gasDensity, density, 48 * 16),
              "square");
        Carrying(def.size);
        Steps(40);
        Field(def.size, mvfx_gasDensity, density);
        double sum = 0.0;
        float least = INFINITY;
        peaks[r] = -INFINITY;
        for (int32_t i = 0; i < 48 * 16; ++i)
        {
            peaks[r] = fmaxf(peaks[r], density[i]);
            least = fminf(least, density[i]);
            sum += (double)density[i];
        }
        right = right && peaks[r] <= 1.0f && least >= 0.0f && fabs(sum - 36.0) <= 0.05 * 36.0;
        mvfxDestroyContext(s_context);
    }
    CHECK(right && peaks[1] > peaks[0] + 0.05f, "MacCormack sharper, bounded");
}

static void TestMacCormackAccuracy(void)
{
    // A Gaussian of density in a 2D gas of 64 by 16, carried 0.37 cells a
    // step to the right for 40 steps, no pressure: MacCormack's error from
    // the Gaussian moved 14.8 cells is at most half semi-Lagrangian's
    // (0.62 against 1.49 seen; taking back the whole error, 2.6).
    static float density[64 * 16];
    double errors[2];
    for (int32_t r = 0; r < 2; ++r)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 64;
        def.size[1] = 16;
        def.pressureCycles = 0;
        def.advection = r == 0 ? mvfx_gasSemiLagrangian : mvfx_gasMacCormack;
        Open(&def, s_unturned, 1.0f);
        for (int32_t i = 0; i < 64 * 16; ++i)
        {
            float dx = (float)(i % 64) - 12.0f;
            float dy = (float)(i / 64) - 7.5f;
            density[i] = expf(-(dx * dx + dy * dy) / 18.0f);
        }
        CHECK(mvfxTestWriteGasScalar(s_context, s_instance, 0, mvfx_gasDensity, density, 64 * 16),
              "Gaussian");
        Carrying(def.size);
        Steps(40);
        Field(def.size, mvfx_gasDensity, density);
        double sum = 0.0;
        for (int32_t i = 0; i < 64 * 16; ++i)
        {
            double dx = (double)(i % 64) - 12.0 - 14.8;
            double dy = (double)(i / 64) - 7.5;
            double e = exp(-(dx * dx + dy * dy) / 18.0) - (double)density[i];
            sum += e * e;
        }
        errors[r] = sqrt(sum);
        mvfxDestroyContext(s_context);
    }
    CHECK(errors[1] <= 0.5 * errors[0], "MacCormack more accurate");
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
    TestHeld();
    TestConfinement();
    TestConfinement3D();
    TestEvenCurl();
    TestColumns();
    TestBurn();
    TestExpansion();
    TestExpansionHeld();
    TestMacCormack();
    TestMacCormackAccuracy();
    TestRefusals();
    return s_failures;
}
