// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gases: every refusal of a gas def, an effect of a gas alone; a source
// filling the cells it holds and nothing else; a puff carried one cell a
// step by a uniform velocity, its mass kept, in 2D and in 3D; decay at
// its rate, of the scalars and the velocity; a linear field sampled
// exactly from each array; tracing back by the midpoint rule; velocities read at the cells'
// centers; a local box following its instance and a world box staying; an instance with a gas
// finishing once stopped and empty, cleared at once by stopping and clearing, and emptied by
// restarting; an instance's bounds holding its gases' boxes; the reading's refusals.

#include "gas_step.h"
#include "test_harness.h"

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
    for (int32_t c = 0; c < 18; ++c)
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
        refused = refused && mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid;
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
    bool many = mvfxCreateEffect(context, &none, &effect) == mvfx_errorInvalid;
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

static void TestSampling(void)
{
    // A linear field over each array of a 5 by 4 by 3 lattice (the cells'
    // and the faces' across each axis), sampled anywhere among its
    // points: reproduced, as trilinear weights reproduce a linear field,
    // each array's points in their own place (a face on its cell's side).
    static float values[6 * 5 * 4];
    const mvfxGasLattice l = {.n = {5, 4, 3}, .cell = 0.1f, .h = 0.1f};
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
        for (int32_t s = 0; s < 40; ++s)
        {
            float c[3];
            float p[3];
            for (int32_t k = 0; k < 3; ++k)
            {
                c[k] = (float)(m[k] - 1) * (float)((s * (7 + 3 * k)) % 41) / 40.0f;
                p[k] = c[k] + (k == axis ? 0.0f : 0.5f);
            }
            float expected = 1.0f + 2.0f * c[0] + 3.0f * c[1] - 0.5f * c[2];
            right = right && fabsf(mvfxGasSample(values, &l, axis, p) - expected) <= 1e-4f;
        }
    }
    CHECK(right, "a linear field sampled");
}

static void TestTraceBack(void)
{
    // A 2D lattice of 16 by 16 cells of 0.1 turning at 2 radians a second
    // about its middle: a place 2 cells off it traced back over 0.1 s by
    // the midpoint rule, p - h v(p - h/2 v(p)), the velocity linear and so
    // sampled exactly; Euler's p - h v(p) is 0.04 cells off it.
    static float u[17 * 16];
    static float v[16 * 17];
    static float w[16 * 16 * 2];
    const mvfxGasLattice l = {.n = {16, 16, 1}, .cell = 0.1f, .h = 0.1f};
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
    float* velocity[3] = {u, v, w};
    const float p[3] = {10.0f, 8.0f, 0.5f};
    float back[3];
    mvfxGasTraceBack(velocity, &l, p, back);
    double o = (double)omega;
    double px = (double)p[0];
    double py = (double)p[1];
    // v(p) = omega (-(y - 8), x - 8); the midpoint p - h/2 v(p).
    double mx = px + 0.05 * o * (py - 8.0);
    double my = py - 0.05 * o * (px - 8.0);
    double ex = px + 0.1 * o * (my - 8.0);
    double ey = py - 0.1 * o * (mx - 8.0);
    CHECK(fabs((double)back[0] - ex) <= 1e-5 && fabs((double)back[1] - ey) <= 1e-5 &&
              back[2] == 0.5f,
          "traced back by the midpoint rule");
}

static void TestPlaces(void)
{
    // A local box centered 1 along X of its instance moves with it,
    // turned a quarter about Z; a world box stays where it began.
    for (int32_t world = 0; world < 2; ++world)
    {
        mvfxGasDef gas = mvfxDefaultGasDef();
        gas.center[0] = 1.0f;
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
        bool right = info.size[0] == 64 && info.size[1] == 64 && info.size[2] == 1;
        if (world != 0)
        {
            right = right && info.center[0] == 1.0 && info.center[1] == 0.0 &&
                    info.rotation[3] == 1.0f && info.cellSize == 0.1f;
        }
        else
        {
            right = right && fabs(info.center[0] - 10.0) <= 1e-5 &&
                    fabs(info.center[1] - 2.0) <= 1e-5 && info.rotation[2] == turn[2] &&
                    fabsf(info.cellSize - 0.2f) <= 1e-7f;
        }
        // Its bounds hold its box: unturned, 6.4 by 6.4 by 0.1 about its
        // center; scaled by 2 and turned a quarter, 12.8 by 12.8.
        mvfxBounds bounds;
        CHECK(mvfxInstance_GetBounds(s_scene.context, s_scene.instance, -1, &bounds) ==
                  mvfx_success,
              "bounds");
        double half = world != 0 ? 3.2 : 6.4;
        right = right && !bounds.empty && fabs(bounds.min[0] - (info.center[0] - half)) <= 1e-4 &&
                fabs(bounds.max[1] - (info.center[1] + half)) <= 1e-4;
        CHECK(right, "a gas's box placed");
        mvfxDestroyContext(s_scene.context);
    }
}

static void TestFinish(void)
{
    // A gas alone keeps its instance going until it stops; stopped, until
    // its density decays away (rate 50); stopped and cleared, it is done
    // at once; restarted, it is empty and runs again.
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
    CHECK(mvfxInstance_Restart(s_scene.context, s_scene.instance) == mvfx_success, "restart");
    Read(mvfx_gasDensity, 128);
    bool emptied = s_field[3 * 16 + 4] == 0.0f;
    Steps(1);
    bool again = !mvfxInstance_IsFinished(s_scene.context, s_scene.instance);
    Stop(mvfx_stopAndClear);
    Read(mvfx_gasDensity, 128);
    bool cleared =
        s_field[3 * 16 + 4] == 0.0f && mvfxInstance_IsFinished(s_scene.context, s_scene.instance);
    CHECK(going && fading && done && emptied && again && cleared, "a gas finishes when empty");
    mvfxDestroyContext(s_scene.context);
}

static void TestReadRefusals(void)
{
    mvfxGasDef gas = mvfxDefaultGasDef();
    Open(&gas);
    uint64_t misuse = mvfxContext_GetMisuse(s_scene.context);
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
    bool counted = mvfxContext_GetMisuse(s_scene.context) == misuse + 6;
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
    TestSampling();
    TestTraceBack();
    TestPlaces();
    TestFinish();
    TestReadRefusals();
    return s_failures;
}
