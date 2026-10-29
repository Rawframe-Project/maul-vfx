// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gases: every refusal of a gas def, an effect of a gas alone; a source
// filling the cells it holds and nothing else; a puff carried one cell a
// step by a uniform velocity, its mass kept, in 2D and in 3D; decay at
// its rate, of the scalars and the velocity; a linear field sampled
// exactly from each array; tracing back by the midpoint rule; velocities
// read at the cells' centers; a local box following its instance and a
// world box staying; an instance with a gas finishing once stopped and
// empty, cleared at once by stopping and clearing, and emptied by
// restarting; an instance's bounds holding its gases' boxes; sources
// pouring into exactly the cells they hold, their edges on cells' centers
// too, and a field of negative values carried; a gas prewarmed as stepped
// with its context; the reading's refusals.

#include "test_harness.h"
#include "test_hooks.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxEffectId effect;
    mvfxInstanceId instance;
} Scene;

static Scene s_scene;

// Opens a scene of an effect of one gas alone.
static void Open(const mvfxGasDef* gas)
{
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = gas;
    def.gasCount = 1;
    CHECK(mvfxCreateEffect(s_scene.context, &def, &s_scene.effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = s_scene.effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
}

// Steps of 1/64 s.
static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_scene.context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success,
              "step");
    }
}

static float s_field[4096];

// Seconds in ticks, rounded down.
static int64_t TicksOf(double seconds)
{
    return (int64_t)(seconds * MVFX_TICKS_PER_SECOND);
}

// Reads a field of the scene's gas into s_field.
static void Read(mvfxGasField field, int64_t cells)
{
    CHECK(mvfxInstance_ReadGas(s_scene.context, s_scene.instance, 0, field, s_field, cells) ==
              mvfx_success,
          "read");
}

static void Stop(mvfxStopMode mode)
{
    CHECK(mvfxInstance_Stop(s_scene.context, s_scene.instance, mode) == mvfx_success, "stopped");
}

static void TestDefs(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    static const mvfxGasSource sphere = {.shape = mvfx_gasSphere, .radius = 0.2f, .density = 1};
    bool refused = true;
    for (int32_t c = 0; c < 22; ++c)
    {
        mvfxGasDef gas = mvfxDefaultGasDef();
        mvfxGasSource source = sphere;
        gas.sources = &source;
        gas.sourceCount = 1;
        int32_t sizes[5][3] = {{1, 8, 1}, {8, 1, 1}, {8, 8, 0}, {513, 8, 1}, {256, 256, 64}};
        switch (c)
        {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
            memcpy(gas.size, sizes[c], sizeof gas.size);
            break;
        case 5:
            gas.cookie = 0;
            break;
        case 6:
            gas.cellSize = 0.0f;
            break;
        case 7:
            gas.space = 2;
            break;
        case 8:
            gas.substeps = 0;
            break;
        case 9:
            gas.substeps = MVFX_MAX_GAS_SUBSTEPS + 1;
            break;
        case 10:
            gas.densityDecay = -1.0f;
            break;
        case 11:
            gas.velocityDecay = (float)NAN;
            break;
        case 12:
            gas.center[1] = (float)INFINITY;
            break;
        case 13:
            gas.sourceCount = MVFX_MAX_GAS_SOURCES + 1;
            break;
        case 14:
            gas.sources = NULL;
            break;
        case 15:
            source.shape = 2;
            break;
        case 16:
            source.radius = 0.0f;
            break;
        case 17:
            gas.fillTime = 0.0f;
            break;
        case 18:
            gas.fillTime = 1.01f * (float)MVFX_MAX_STEP_SECONDS;
            break;
        case 19:
            gas.courant = 0.0f;
            break;
        case 20:
            gas.courant = (float)MVFX_MAX_GAS_SIZE + 1.0f;
            break;
        default:
            source.shape = mvfx_gasBox;
            source.halfExtents[0] = 1.0f;
            source.halfExtents[1] = 1.0f;
            source.halfExtents[2] = -1.0f;
            break;
        }
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.gases = &gas;
        def.gasCount = 1;
        mvfxEffectId effect = {0};
        // Past a named bound: mvfx_errorLimit.
        mvfxResult want = c == 3 || c == 4 || c == 9 || c == 13 || c == 18 || c == 20
                              ? mvfx_errorLimit
                              : mvfx_errorInvalid;
        refused = refused && mvfxCreateEffect(context, &def, &effect) == want;
    }
    mvfxEffectDef none = mvfxDefaultEffectDef();
    mvfxEffectId effect = {0};
    bool empty = mvfxCreateEffect(context, &none, &effect) == mvfx_errorInvalid;
    mvfxGasDef gases[MVFX_MAX_GASES + 1];
    for (int32_t g = 0; g <= MVFX_MAX_GASES; ++g)
    {
        gases[g] = mvfxDefaultGasDef();
    }
    none.gases = gases;
    none.gasCount = MVFX_MAX_GASES + 1;
    bool many = mvfxCreateEffect(context, &none, &effect) == mvfx_errorLimit;
    none.gasCount = MVFX_MAX_GASES;
    bool most = mvfxCreateEffect(context, &none, &effect) == mvfx_success;
    CHECK(refused && empty && many && most, "gas defs checked");
    mvfxDestroyContext(context);
}

// A 2D gas of 16 by 8 cells of 0.1, a puff source of density 64 a second
// in a sphere of 0.12 at (-0.35, -0.05) (the cells about (4, 3)), and a
// box over the whole gas setting the velocity (6.4, 0): one cell a step
// of 1/64 s.
static mvfxGasDef Puff(void)
{
    static const mvfxGasSource sources[2] = {
        {.shape = mvfx_gasSphere, .center = {-0.35f, -0.05f, 0}, .radius = 0.12f, .density = 64},
        {.shape = mvfx_gasBox,
         .halfExtents = {10, 10, 10},
         .setsVelocity = true,
         .velocity = {6.4f, 0, 0}},
    };
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 16;
    gas.size[1] = 8;
    gas.sources = sources;
    gas.sourceCount = 2;
    return gas;
}

static void TestFill(void)
{
    // A step of the puff source alone, no velocity: density 1 in the
    // cells whose centers lie within its 0.12, none elsewhere.
    mvfxGasDef gas = Puff();
    gas.sourceCount = 1;
    Open(&gas);
    Steps(1);
    Read(mvfx_gasDensity, 128);
    bool right = true;
    for (int32_t i = 0; i < 128; ++i)
    {
        float x = ((float)(i % 16) + 0.5f - 8.0f) * 0.1f + 0.35f;
        float y = ((float)(i / 16) + 0.5f - 4.0f) * 0.1f + 0.05f;
        bool inside = x * x + y * y <= 0.12f * 0.12f;
        right = right && s_field[i] == (inside ? 1.0f : 0.0f);
    }
    CHECK(right, "a source fills what it holds");
    mvfxDestroyContext(s_scene.context);
}

// The density's sum and its mean place along X.
static void Mass(int32_t width, int64_t cells, float* sum, float* place)
{
    *sum = 0.0f;
    *place = 0.0f;
    for (int64_t i = 0; i < cells; ++i)
    {
        *sum += s_field[i];
        *place += s_field[i] * (float)(i % width);
    }
    *place /= *sum;
}

static void TestCarried(void)
{
    // The puff poured for one step and the sources stopped: each step
    // after carries it one cell along X, its mass kept, in 2D and in 3D
    // (16 by 8 by 4, the puff a sphere about a cell's center).
    for (int32_t solid = 0; solid < 2; ++solid)
    {
        mvfxGasDef gas = Puff();
        mvfxGasSource sources[2] = {Puff().sources[0], Puff().sources[1]};
        if (solid != 0)
        {
            gas.size[2] = 4;
            sources[0].center[2] = -0.05f;
        }
        gas.sources = sources;
        Open(&gas);
        int64_t cells = (int64_t)gas.size[0] * gas.size[1] * gas.size[2];
        Steps(1);
        Stop(mvfx_stopEmitting);
        Read(mvfx_gasDensity, cells);
        float sum = 0.0f;
        float start = 0.0f;
        Mass(16, cells, &sum, &start);
        Steps(6);
        Read(mvfx_gasDensity, cells);
        float kept = 0.0f;
        float place = 0.0f;
        Mass(16, cells, &kept, &place);
        Read(mvfx_gasVelocityX, cells);
        bool uniform = true;
        for (int64_t i = 0; i < cells; ++i)
        {
            uniform = uniform && fabsf(s_field[i] - 6.4f) <= 1e-5f;
        }
        CHECK(fabsf(kept - sum) <= 1e-4f * sum && fabsf(place - start - 6.0f) <= 1e-3f && uniform,
              "a puff carried a cell a step");
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestReadVelocity(void)
{
    // A 2D gas a few steps after a box of wind blew through part of it,
    // its velocity uneven: each cell's component read is the mean of its
    // two faces' across it.
    static const mvfxGasSource sources[2] = {
        {.shape = mvfx_gasSphere, .center = {-0.35f, -0.05f, 0}, .radius = 0.12f, .density = 64},
        {.shape = mvfx_gasBox,
         .center = {-0.2f, 0.1f, 0},
         .halfExtents = {0.3f, 0.2f, 1},
         .setsVelocity = true,
         .velocity = {1.3f, 0.45f, 0}},
    };
    mvfxGasDef gas = Puff();
    gas.sources = sources;
    Open(&gas);
    Steps(5);
    bool right = true;
    bool uneven = false;
    for (int32_t k = 0; k < 2; ++k)
    {
        Read((mvfxGasField)(mvfx_gasVelocityX + k), 128);
        static float faces[17 * 16];
        CHECK(mvfxTestReadGasFaces(s_scene.context, s_scene.instance, 0, k, faces, 17 * 16) ==
                  (k == 0 ? 17 * 8 : 16 * 9),
              "the faces read");
        for (int32_t i = 0; i < 128; ++i)
        {
            int32_t x = i % 16;
            int32_t y = i / 16;
            int32_t at = k == 0 ? y * 17 + x : y * 16 + x;
            float mean = 0.5f * (faces[at] + faces[at + (k == 0 ? 1 : 16)]);
            right = right && s_field[i] == mean;
            uneven = uneven || faces[at] != faces[at + (k == 0 ? 1 : 16)];
        }
    }
    CHECK(right && uneven, "velocities read at the cells' centers");
    mvfxDestroyContext(s_scene.context);
}

static void TestDecay(void)
{
    // Density decaying at 3 a second: after a step of pouring, 20 steps
    // later the puff is exp(-3 20 / 64) of itself, in 2 substeps a step.
    mvfxGasDef gas = Puff();
    gas.sourceCount = 1;
    gas.densityDecay = 3.0f;
    gas.substeps = 2;
    Open(&gas);
    Steps(1);
    Stop(mvfx_stopEmitting);
    Read(mvfx_gasDensity, 128);
    float sum = 0.0f;
    float place = 0.0f;
    Mass(16, 128, &sum, &place);
    Steps(20);
    Read(mvfx_gasDensity, 128);
    float later = 0.0f;
    Mass(16, 128, &later, &place);
    CHECK(fabsf(later / sum - expf(-3.0f * 20.0f / 64.0f)) <= 1e-5f, "decay at its rate");
    mvfxDestroyContext(s_scene.context);
    // The velocity decaying at 2 a second, its box stopped after a step:
    // 10 steps later it is exp(-2 10 / 64) of itself.
    gas = Puff();
    gas.velocityDecay = 2.0f;
    Open(&gas);
    Steps(1);
    Stop(mvfx_stopEmitting);
    Read(mvfx_gasVelocityX, 128);
    float before = s_field[40];
    Steps(10);
    Read(mvfx_gasVelocityX, 128);
    CHECK(fabsf(s_field[40] / before - expf(-2.0f * 10.0f / 64.0f)) <= 1e-5f,
          "the velocity decays at its rate");
    mvfxDestroyContext(s_scene.context);
}

// A 5 by 4 by 3 gas of cells of 0.1 alone, still, at the origin: the
// world's place of a point of its lattice, in cells from its box's least
// corner.
static void LatticePlace(const mvfxGasInfo* info, const float p[3], double out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] =
            info->center[k] + ((double)p[k] - 0.5 * (double)info->size[k]) * (double)info->cellSize;
    }
}

static void TestSampling(void)
{
    // A linear field over each array of a 5 by 4 by 3 gas (its cells' and
    // its faces' across each axis), sampled anywhere among its points
    // through the public sampling: reproduced, as trilinear weights
    // reproduce a linear field, each array's points in their own place (a
    // face on its cell's side).
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 5;
    gas.size[1] = 4;
    gas.size[2] = 3;
    gas.cellSize = 0.1f;
    Open(&gas);
    mvfxGasInfo info = {0};
    CHECK(mvfxInstance_GetGasInfo(s_scene.context, s_scene.instance, 0, &info) == mvfx_success,
          "info");
    static float values[6 * 5 * 4];
    bool right = true;
    for (int32_t axis = -1; axis < 3; ++axis)
    {
        const int32_t m[3] = {5 + (axis == 0 ? 1 : 0), 4 + (axis == 1 ? 1 : 0),
                              3 + (axis == 2 ? 1 : 0)};
        int32_t i = 0;
        for (int32_t z = 0; z < m[2]; ++z)
        {
            for (int32_t y = 0; y < m[1]; ++y)
            {
                for (int32_t x = 0; x < m[0]; ++x)
                {
                    values[i++] = 1.0f + 2.0f * (float)x + 3.0f * (float)y - 0.5f * (float)z;
                }
            }
        }
        CHECK(axis < 0
                  ? mvfxTestWriteGasScalar(s_scene.context, s_scene.instance, 0, mvfx_gasDensity,
                                           values, i)
                  : mvfxTestWriteGasFaces(s_scene.context, s_scene.instance, 0, axis, values, i),
              "the field written");
        mvfxGasField field = axis < 0 ? mvfx_gasDensity : (mvfxGasField)(mvfx_gasVelocityX + axis);
        for (int32_t s = 0; s < 40; ++s)
        {
            float c[3];
            float p[3];
            for (int32_t k = 0; k < 3; ++k)
            {
                c[k] = (float)(m[k] - 1) * (float)((s * (7 + 3 * k)) % 41) / 40.0f;
                p[k] = c[k] + (k == axis ? 0.0f : 0.5f);
            }
            double place[3];
            LatticePlace(&info, p, place);
            float sampled = 0.0f;
            CHECK(mvfxInstance_SampleGas(s_scene.context, s_scene.instance, 0, field, place, 1,
                                         &sampled) == mvfx_success,
                  "sampled");
            float expected = 1.0f + 2.0f * c[0] + 3.0f * c[1] - 0.5f * c[2];
            right = right && fabsf(sampled - expected) <= 1e-4f;
        }
    }
    CHECK(right, "a linear field sampled");
    mvfxDestroyContext(s_scene.context);
}

static void TestTraceBack(void)
{
    // A 2D gas of 16 by 16 cells of 0.1 turning at 2 radians a second about
    // its middle, its density linear, one step of 0.1 s with nothing but
    // the carrying: the density at a cell's center off the middle is the
    // field's where the midpoint rule traces it back, p - h v(p - h/2
    // v(p)), the velocity and the density linear and so sampled exactly;
    // Euler's p - h v(p) would be 0.0125 off it.
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 16;
    gas.size[1] = 16;
    gas.size[2] = 1;
    gas.cellSize = 0.1f;
    gas.substeps = 1;
    gas.pressureCycles = 0;
    gas.advection = mvfx_gasSemiLagrangian;
    gas.densityDecay = 0.0f;
    gas.velocityDecay = 0.0f;
    Open(&gas);
    static float u[17 * 16];
    static float v[16 * 17];
    static float density[16 * 16];
    const float omega = 2.0f;
    for (int32_t y = 0; y < 16; ++y)
    {
        for (int32_t x = 0; x <= 16; ++x)
        {
            u[y * 17 + x] = -omega * ((float)y + 0.5f - 8.0f) * 0.1f;
        }
    }
    for (int32_t y = 0; y <= 16; ++y)
    {
        for (int32_t x = 0; x < 16; ++x)
        {
            v[y * 16 + x] = omega * ((float)x + 0.5f - 8.0f) * 0.1f;
        }
    }
    // The density 1 + 0.5 x + 0.25 y at cell (x, y)'s center, in cells.
    for (int32_t i = 0; i < 256; ++i)
    {
        density[i] = 1.0f + 0.5f * ((float)(i % 16) + 0.5f) + 0.25f * ((float)(i / 16) + 0.5f);
    }
    CHECK(mvfxTestWriteGasFaces(s_scene.context, s_scene.instance, 0, 0, u, 17 * 16) &&
              mvfxTestWriteGasFaces(s_scene.context, s_scene.instance, 0, 1, v, 16 * 17) &&
              mvfxTestWriteGasScalar(s_scene.context, s_scene.instance, 0, mvfx_gasDensity, density,
                                     256),
          "the fields written");
    CHECK(mvfxContext_StepTicks(s_scene.context, TicksOf(0.1)) == mvfx_success, "step");
    Read(mvfx_gasDensity, 256);
    // Cell (9, 7)'s center, in cells from the lattice's corner.
    const double px = 9.5;
    const double py = 7.5;
    double o = (double)omega;
    double h = (double)TicksOf(0.1) / MVFX_TICKS_PER_SECOND;
    // v(p) = omega (-(y - 8), x - 8) cells a second; the midpoint p - h/2 v(p).
    double mx = px + 0.5 * h * o * (py - 8.0);
    double my = py - 0.5 * h * o * (px - 8.0);
    double ex = px + h * o * (my - 8.0);
    double ey = py - h * o * (mx - 8.0);
    double expected = 1.0 + 0.5 * ex + 0.25 * ey;
    CHECK(fabs((double)s_field[7 * 16 + 9] - expected) <= 1e-5, "traced back by the midpoint rule");
    mvfxDestroyContext(s_scene.context);
}

static void TestPlaces(void)
{
    // A local box centered 1 along X of its instance moves with it,
    // turned a quarter about Z; a world box stays where it began.
    for (int32_t world = 0; world < 2; ++world)
    {
        mvfxGasDef gas = mvfxDefaultGasDef();
        gas.center[0] = 1.0f;
        gas.size[1] = 32;
        gas.space = world != 0 ? mvfx_gasWorld : mvfx_gasLocal;
        Open(&gas);
        static const double origin[3] = {10, 0, 0};
        const float turn[4] = {0, 0, sinf(0.78539816f), cosf(0.78539816f)};
        CHECK(mvfxInstance_SetTransform(s_scene.context, s_scene.instance, origin, turn, 2.0f,
                                        false) == mvfx_success,
              "moved");
        Steps(1);
        mvfxGasInfo info;
        CHECK(mvfxInstance_GetGasInfo(s_scene.context, s_scene.instance, 0, &info) == mvfx_success,
              "info");
        bool right = info.size[0] == 64 && info.size[1] == 32 && info.size[2] == 1;
        if (world != 0)
        {
            right = right && info.center[0] == 1.0 && info.center[1] == 0.0 &&
                    info.rotation[3] == 1.0f && info.cellSize == 0.1f && info.scale == 1.0f;
        }
        else
        {
            right = right && fabs(info.center[0] - 10.0) <= 1e-5 &&
                    fabs(info.center[1] - 2.0) <= 1e-5 && info.rotation[2] == turn[2] &&
                    fabsf(info.cellSize - 0.2f) <= 1e-7f && info.scale == 2.0f;
        }
        // Its bounds hold its box: unturned, 6.4 by 3.2 by 0.1 about its
        // center; scaled by 2 and turned a quarter, 6.4 by 12.8.
        mvfxBounds bounds;
        CHECK(mvfxInstance_GetBounds(s_scene.context, s_scene.instance, -1, &bounds) ==
                  mvfx_success,
              "bounds");
        double across = 3.2;
        double up = world != 0 ? 1.6 : 6.4;
        right = right && !bounds.empty && fabs(bounds.min[0] - (info.center[0] - across)) <= 1e-4 &&
                fabs(bounds.max[1] - (info.center[1] + up)) <= 1e-4;
        CHECK(right, "a gas's box placed");
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestFinish(void)
{
    // A gas alone keeps its instance going until it stops; stopped, until
    // its density decays away (rate 50), and then its gas stays as it is;
    // stopped and cleared, it is done at once; restarted, it is empty and
    // runs again.
    mvfxGasDef gas = Puff();
    gas.sourceCount = 1;
    gas.densityDecay = 50.0f;
    Open(&gas);
    Steps(30);
    bool going = !mvfxInstance_IsFinished(s_scene.context, s_scene.instance);
    Stop(mvfx_stopEmitting);
    Steps(1);
    bool fading = !mvfxInstance_IsFinished(s_scene.context, s_scene.instance);
    Steps(64);
    bool done = mvfxInstance_IsFinished(s_scene.context, s_scene.instance);
    // Nothing happens to a finished instance's gas: a velocity left in it
    // stays as it was.
    static float faces[16 * 9];
    for (int32_t f = 0; f < 16 * 9; ++f)
    {
        faces[f] = (float)(f % 5) - 2.0f;
    }
    CHECK(mvfxTestWriteGasFaces(s_scene.context, s_scene.instance, 0, 1, faces, 16 * 9),
          "a velocity left");
    Steps(2);
    CHECK(mvfxTestReadGasFaces(s_scene.context, s_scene.instance, 0, 1, faces, 16 * 9) == 16 * 9,
          "read again");
    bool still = true;
    for (int32_t f = 0; f < 16 * 9; ++f)
    {
        still = still && faces[f] == (float)(f % 5) - 2.0f;
    }
    CHECK(mvfxInstance_Restart(s_scene.context, s_scene.instance) == mvfx_success, "restart");
    Read(mvfx_gasDensity, 128);
    bool emptied = s_field[3 * 16 + 4] == 0.0f;
    Steps(1);
    bool again = !mvfxInstance_IsFinished(s_scene.context, s_scene.instance);
    Stop(mvfx_stopAndClear);
    Read(mvfx_gasDensity, 128);
    bool cleared =
        s_field[3 * 16 + 4] == 0.0f && mvfxInstance_IsFinished(s_scene.context, s_scene.instance);
    CHECK(going && fading && done && still && emptied && again && cleared,
          "a gas finishes when empty");
    mvfxDestroyContext(s_scene.context);
}

static void TestSourceCells(void)
{
    // Sources at places across a 2D gas of 32 by 24 cells, still, three
    // steps of 1/64 s: a sphere of 0.37 at (0.41, -0.23) and a box of
    // half sizes (0.26, 0.5) at (-1.1, 0.7), reaching past the box's edge,
    // pour into exactly the cells whose centers they hold (2 a second, 3
    // steps); a third pouring -1 a second into a sphere of 0.3 at (-0.5,
    // -0.6) leaves a field of its own negative values carried, not
    // dropped as empty.
    static const mvfxGasSource sources[2] = {
        {.shape = mvfx_gasSphere, .center = {0.41f, -0.23f, 0}, .radius = 0.37f, .density = 2},
        {.shape = mvfx_gasBox,
         .center = {-1.1f, 0.7f, 0},
         .halfExtents = {0.26f, 0.5f, 1},
         .density = 2,
         .fuel = 0},
    };
    static const mvfxGasSource drain = {
        .shape = mvfx_gasSphere, .center = {-0.5f, -0.6f, 0}, .radius = 0.3f, .temperature = -1};
    mvfxGasSource all[3] = {sources[0], sources[1], drain};
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 32;
    gas.size[1] = 24;
    gas.pressureCycles = 0;
    gas.sources = all;
    gas.sourceCount = 3;
    Open(&gas);
    Steps(3);
    Read(mvfx_gasDensity, 768);
    bool right = true;
    for (int32_t i = 0; i < 768; ++i)
    {
        float x = ((float)(i % 32) + 0.5f - 16.0f) * 0.1f;
        float y = ((float)(i / 32) + 0.5f - 12.0f) * 0.1f;
        float dx = x - 0.41f;
        float dy = y + 0.23f;
        bool sphere = dx * dx + dy * dy <= 0.37f * 0.37f;
        bool box = fabsf(x + 1.1f) <= 0.26f && fabsf(y - 0.7f) <= 0.5f;
        float expected = (sphere ? 6.0f / 64.0f : 0.0f) + (box ? 6.0f / 64.0f : 0.0f);
        right = right && fabsf(s_field[i] - expected) <= 1e-6f;
    }
    Read(mvfx_gasTemperature, 768);
    float least = 0.0f;
    for (int32_t i = 0; i < 768; ++i)
    {
        least = fminf(least, s_field[i]);
    }
    CHECK(right && fabsf(least + 3.0f / 64.0f) <= 1e-6f, "sources pour into their cells");
    mvfxDestroyContext(s_scene.context);
}

static void TestSourceEdges(void)
{
    // Boxes of half size 0.25 stepped by 0.05 along X and Y across a 2D
    // gas of 32 by 24 cells of 0.1, so that their edges fall on cells'
    // centers again and again: each pours into exactly the cells its own
    // test holds, those on its edge too.
    bool right = true;
    for (int32_t g = 0; g < 5; ++g)
    {
        mvfxGasSource boxes[MVFX_MAX_GAS_SOURCES];
        for (int32_t s = 0; s < MVFX_MAX_GAS_SOURCES; ++s)
        {
            int32_t j = g * MVFX_MAX_GAS_SOURCES + s;
            boxes[s] = (mvfxGasSource){
                .shape = mvfx_gasBox,
                .center = {-1.4f + 0.05f * (float)j, -1.0f + 0.05f * (float)(j % 7), 0},
                .halfExtents = {0.25f, 0.25f, 1},
                .density = 64};
        }
        mvfxGasDef gas = mvfxDefaultGasDef();
        gas.size[0] = 32;
        gas.size[1] = 24;
        gas.pressureCycles = 0;
        gas.sources = boxes;
        gas.sourceCount = MVFX_MAX_GAS_SOURCES;
        Open(&gas);
        Steps(1);
        Read(mvfx_gasDensity, 768);
        for (int32_t i = 0; i < 768; ++i)
        {
            const int32_t at[2] = {i % 32, i / 32};
            float x = ((float)at[0] + 0.5f - 16.0f) * 0.1f;
            float y = ((float)at[1] + 0.5f - 12.0f) * 0.1f;
            float expected = 0.0f;
            for (int32_t s = 0; s < MVFX_MAX_GAS_SOURCES; ++s)
            {
                bool holds = fabsf(x - boxes[s].center[0]) <= 0.25f &&
                             fabsf(y - boxes[s].center[1]) <= 0.25f;
                expected += holds ? 1.0f : 0.0f;
            }
            right = right && fabsf(s_field[i] - expected) <= 1e-5f;
        }
        mvfxDestroyContext(s_scene.context);
    }
    CHECK(right, "sources' edges on cells' centers");
}

static void TestPrewarm(void)
{
    // A puff prewarmed, five steps of 1/64 s in one call, holds the
    // same bits as one stepped with its context five times: each step's
    // gas runs before the next step, and the last one's before the call
    // returns.
    static float prewarmed[128];
    const mvfxGasDef gas = Puff();
    Open(&gas);
    double remaining = 1.0;
    CHECK(mvfxInstance_Prewarm(s_scene.context, s_scene.instance, 5.0 / 64.0, 1.0 / 64.0, 10,
                               &remaining) == mvfx_success &&
              remaining == 0.0,
          "prewarmed");
    Read(mvfx_gasDensity, 128);
    memcpy(prewarmed, s_field, sizeof prewarmed);
    mvfxDestroyContext(s_scene.context);
    Open(&gas);
    Steps(5);
    Read(mvfx_gasDensity, 128);
    CHECK(memcmp(prewarmed, s_field, sizeof prewarmed) == 0,
          "a gas prewarmed as stepped with its context");
    mvfxDestroyContext(s_scene.context);
}

static void TestReadRefusals(void)
{
    mvfxGasDef gas = mvfxDefaultGasDef();
    Open(&gas);
    uint64_t misuse = mvfxGetContextMisuse(s_scene.context);
    mvfxGasInfo info;
    bool refused =
        mvfxInstance_ReadGas(s_scene.context, s_scene.instance, 1, mvfx_gasDensity, s_field,
                             4096) == mvfx_errorInvalid &&
        mvfxInstance_ReadGas(s_scene.context, s_scene.instance, 0, 6, s_field, 4096) ==
            mvfx_errorInvalid &&
        mvfxInstance_ReadGas(s_scene.context, s_scene.instance, 0, mvfx_gasDensity, s_field,
                             4095) == mvfx_errorInvalid &&
        mvfxInstance_ReadGas(s_scene.context, s_scene.instance, 0, mvfx_gasDensity, NULL, 4096) ==
            mvfx_errorInvalid &&
        mvfxInstance_GetGasInfo(s_scene.context, s_scene.instance, -1, &info) ==
            mvfx_errorInvalid &&
        mvfxInstance_GetGasInfo(s_scene.context, s_scene.instance, 0, NULL) == mvfx_errorInvalid;
    bool counted = mvfxGetContextMisuse(s_scene.context) == misuse + 6;
    bool stale = mvfxInstance_ReadGas(s_scene.context, (mvfxInstanceId){0}, 0, mvfx_gasDensity,
                                      s_field, 4096) == mvfx_errorStale &&
                 mvfxInstance_GetGasInfo(NULL, s_scene.instance, 0, &info) == mvfx_errorInvalid;
    CHECK(refused && counted && stale, "reading refused");
    mvfxDestroyContext(s_scene.context);
}

int main(void)
{
    TestDefs();
    TestFill();
    TestCarried();
    TestDecay();
    TestReadVelocity();
    TestSampling();
    TestTraceBack();
    TestPlaces();
    TestFinish();
    TestSourceCells();
    TestSourceEdges();
    TestPrewarm();
    TestReadRefusals();
    return s_failures;
}
