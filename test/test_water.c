// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Water surfaces of waves: the refusals; a pulse's halves at the wave
// speed; energy and volume held without damping and lost at its rate
// with it; a closed wall reflecting and an open one letting waves out;
// substeps stable at the bound and through a hitch; pushes, and heights
// held at MVFX_MAX_WATER_DEPTH; reads, samples, info and bounds in a
// moved, turned and scaled frame; a stopped instance finishing once its
// water is still.

#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// A water surface of nx by nz cells of cell, its waves at speed, undamped.
static mvfxWaterDef Water(int32_t nx, int32_t nz, float cell, float speed)
{
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = nx;
    water.size[1] = nz;
    water.cellSize = cell;
    water.waveSpeed = speed;
    water.damping = 0.0f;
    return water;
}

// Opens an effect of the water surfaces alone; false when refused.
static bool Open(const mvfxWaterDef* waters, int32_t count)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.waters = waters;
    def.waterCount = count;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(s_context, &def, &effect) != mvfx_success)
    {
        mvfxDestroyContext(s_context);
        return false;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    return true;
}

// Steps the context count times by seconds.
static void Steps(int32_t count, double seconds)
{
    for (int32_t i = 0; i < count; ++i)
    {
        CHECK(mvfxContext_Step(s_context, seconds) == mvfx_success, "stepped");
    }
}

// Pushes surface 0 at a point in the world.
static void Push(double x, double y, double z, float radius, float depth)
{
    const mvfxWaterPush push = {.point = {x, y, z}, .radius = radius, .depth = depth};
    CHECK(mvfxInstance_PushWater(s_context, s_instance, 0, &push, 1) == mvfx_success, "pushed");
}

// A field of surface 0 over all its cells, as float32.
static float* Read(const mvfxWaterDef* water, mvfxWaterField field)
{
    int64_t cells = (int64_t)water->size[0] * water->size[1];
    float* out = malloc(sizeof(float) * (size_t)cells);
    CHECK(out != NULL, "memory");
    const mvfxWaterRead read = {
        .field = field, .format = mvfx_formatFloat32, .size = {water->size[0], water->size[1]}};
    CHECK(mvfxInstance_ReadWater(s_context, s_instance, 0, &read, out,
                                 cells * (int64_t)sizeof(float)) == mvfx_success,
          "read");
    return out;
}

// The surface's energy: its rates' squares and c^2 times its slopes'
// across every pair of neighbors; and its volume, its heights' sum.
static double Energy(const mvfxWaterDef* water, double* volume)
{
    float* u = Read(water, mvfx_waterHeight);
    float* v = Read(water, mvfx_waterVelocityY);
    int32_t nx = water->size[0];
    double stiffness = (double)water->waveSpeed * (double)water->waveSpeed /
                       ((double)water->cellSize * (double)water->cellSize);
    double energy = 0.0;
    *volume = 0.0;
    for (int32_t z = 0; z < water->size[1]; ++z)
    {
        for (int32_t x = 0; x < nx; ++x)
        {
            int64_t i = (int64_t)z * nx + x;
            double across = x + 1 < nx ? (double)u[i + 1] - (double)u[i] : 0.0;
            double along = z + 1 < water->size[1] ? (double)u[i + nx] - (double)u[i] : 0.0;
            energy += (double)v[i] * (double)v[i] + stiffness * (across * across + along * along);
            *volume += (double)u[i];
        }
    }
    free(u);
    free(v);
    return energy;
}

// Spoils a valid water def by case c (none for 15).
static void Spoil(int32_t c, mvfxWaterDef* water)
{
    switch (c)
    {
    case 0:
        water->cookie ^= 1u;
        break;
    case 1:
        water->size[0] = 1;
        break;
    case 2:
        water->size[1] = 1;
        break;
    case 3:
        water->size[0] = MVFX_MAX_WATER_SIZE + 1;
        break;
    case 4:
        water->size[1] = MVFX_MAX_WATER_SIZE + 1;
        break;
    case 5:
        water->cellSize = 0.99f * MVFX_MIN_WATER_CELL;
        break;
    case 6:
        water->cellSize = 2.0f * MVFX_MAX_EXTENT;
        break;
    case 7:
        water->center[1] = NAN;
        break;
    case 8:
        water->center[2] = -2.0f * MVFX_MAX_EXTENT;
        break;
    case 9:
        water->space = (mvfxGasSpace)(mvfx_gasWorld + 1);
        break;
    case 10:
        water->closedWalls = 16;
        break;
    case 11:
        water->waveSpeed = 0.0f;
        break;
    case 12:
        water->waveSpeed = 2.0f * MVFX_MAX_EXTENT;
        break;
    case 13:
        water->damping = -1.0f;
        break;
    case 14:
        water->damping = 2.0f * MVFX_MAX_DRAG;
        break;
    default:
        break;
    }
}

static void TestRefusals(void)
{
    for (int32_t c = 0; c <= 15; ++c)
    {
        mvfxWaterDef water = Water(8, 8, 0.1f, 1.0f);
        Spoil(c, &water);
        bool opened = Open(&water, 1);
        CHECK(opened == (c == 15), "a spoiled water def refused, a valid one opened");
        if (opened)
        {
            mvfxDestroyContext(s_context);
        }
    }
    mvfxWaterDef waters[MVFX_MAX_WATERS + 1];
    for (int32_t w = 0; w <= MVFX_MAX_WATERS; ++w)
    {
        waters[w] = Water(MVFX_MAX_WATER_SIZE, 2, 0.1f, 1.0f);
    }
    CHECK(!Open(waters, MVFX_MAX_WATERS + 1), "too many surfaces refused");
    CHECK(!Open(nullptr, 1), "no surfaces with a count refused");
    CHECK(!Open(waters, -1), "a negative count refused");
    CHECK(Open(waters, MVFX_MAX_WATERS), "the most surfaces, the largest, opened");
    mvfxDestroyContext(s_context);
}

static void TestPulse(void)
{
    // A strip one wave wide (two rows the push meets alike, its Z walls
    // closed): the bump splits into halves of half its depth, each at the
    // wave speed (measured 0.998 of a second's travel, 0.987 of the
    // height: the scheme's dispersion).
    mvfxWaterDef water = Water(512, 2, 0.05f, 1.0f);
    CHECK(Open(&water, 1), "opened");
    Push(0.0, 0.0, 0.0, 0.4f, 0.01f);
    Steps(60, 1.0 / 60.0);
    float* u = Read(&water, mvfx_waterHeight);
    int32_t lowest = 256;
    for (int32_t x = 256; x < 511; ++x)
    {
        lowest = u[x] < u[lowest] ? x : lowest;
    }
    // The trough between cells, by the parabola through its three.
    double a = (double)u[lowest - 1];
    double b = (double)u[lowest];
    double c = (double)u[lowest + 1];
    double at = (double)lowest + 0.5 * (a - c) / (a - 2.0 * b + c) + 0.5 - 256.0;
    CHECK(fabs(at * 0.05 - 1.0) < 0.01, "the front at the wave speed");
    // The level the push's displaced water raised, far from the pulse.
    CHECK(fabs(b - (double)u[0] + 0.005) < 0.005 * 0.03, "each half of half the depth");
    CHECK(u[lowest] == u[512 + lowest], "the rows alike");
    free(u);
    mvfxDestroyContext(s_context);
}

// Ten seconds of a walled-in surface of two pushes, damped or not: its
// energy at the start, its least and most, and at the end; its volume at
// the start and the end.
typedef struct Run
{
    double start;
    double low;
    double high;
    double end;
    double volumes[2];
} Run;

static Run RunEnergy(float damping)
{
    mvfxWaterDef water = Water(64, 64, 0.1f, 1.0f);
    water.damping = damping;
    CHECK(Open(&water, 1), "opened");
    Push(0.0, 0.0, 0.0, 0.5f, 0.02f);
    Push(1.1, 0.0, -0.7, 0.3f, -0.01f);
    Steps(1, 1.0 / 60.0);
    Run run = {0};
    run.start = Energy(&water, &run.volumes[0]);
    run.low = run.start;
    run.high = run.start;
    for (int32_t s = 0; s < 600; ++s)
    {
        Steps(1, 1.0 / 60.0);
        double now = Energy(&water, &run.volumes[1]);
        run.low = fmin(run.low, now);
        run.high = fmax(run.high, now);
    }
    run.end = Energy(&water, &run.volumes[1]);
    mvfxDestroyContext(s_context);
    return run;
}

static void TestEnergy(void)
{
    // Undamped: the energy within 5% of its start for ten seconds
    // (measured from 0.971 to 1.040, the scheme's staggering) and the
    // volume held; damped, it falls as exp(-damping t) (measured within
    // 1%).
    Run held = RunEnergy(0.0f);
    CHECK(held.low > 0.95 * held.start && held.high < 1.05 * held.start, "energy held");
    CHECK(fabs(held.volumes[0]) < 1e-5 && fabs(held.volumes[1]) < 1e-5, "volume held");
    Run damped = RunEnergy(0.5f);
    CHECK(fabs(damped.end / damped.start / exp(-5.0) - 1.0) < 0.05, "energy damped");
}

// A strip's pulse after seconds: its energy over its energy at the start.
static double RunStrip(int32_t nx, int32_t nz, mvfxWaterWalls closed, double seconds)
{
    mvfxWaterDef water = Water(nx, nz, 0.05f, 1.0f);
    water.closedWalls = closed;
    CHECK(Open(&water, 1), "opened");
    Push(0.0, 0.0, 0.0, 0.4f, 0.01f);
    Steps(1, 1.0 / 60.0);
    double volume = 0.0;
    double start = Energy(&water, &volume);
    Steps((int32_t)(seconds * 60.0), 1.0 / 60.0);
    double end = Energy(&water, &volume);
    mvfxDestroyContext(s_context);
    return end / start;
}

static void TestWalls(void)
{
    // A strip's pulse meets its X walls in a few seconds: closed, its
    // energy stays (measured 1.02 after 8 seconds); open, it leaves
    // (measured 0.0048, Mur's edge exact only head on at a Courant
    // number of 1).
    const mvfxWaterWalls alongZ = mvfx_waterWallMinZ | mvfx_waterWallMaxZ;
    CHECK(RunStrip(256, 2, 15, 8.0) > 0.97, "a closed wall reflects");
    CHECK(RunStrip(256, 2, alongZ, 8.0) < 0.01, "an open wall lets waves out");
    // One open wall at a time: half the pulse leaves at once, the other
    // after its reflection (9.6 seconds).
    const mvfxWaterWalls walls[4] = {mvfx_waterWallMinX, mvfx_waterWallMaxX, mvfx_waterWallMinZ,
                                     mvfx_waterWallMaxZ};
    for (int32_t k = 0; k < 4; ++k)
    {
        bool alongX = k < 2;
        double left =
            RunStrip(alongX ? 128 : 2, alongX ? 2 : 128, (mvfxWaterWalls)(15 & ~walls[k]), 12.0);
        CHECK(left < 0.01, "one open wall lets waves out");
    }
}

// A surface of open walls, nx by 240 cells of 0.1 centered at x, its waves
// at 1 undamped, pushed at (13, 0, 0); in its own context.
static mvfxInstanceId OpenWide(mvfxContext** context, int32_t nx, float x)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, context) == mvfx_success, "context");
    mvfxWaterDef water = Water(nx, 240, 0.1f, 1.0f);
    water.center[0] = x;
    water.closedWalls = 0;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.waters = &water;
    def.waterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(*context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId id = {0};
    CHECK(mvfxCreateInstance(*context, &instanceDef, &id) == mvfx_success, "instance");
    const mvfxWaterPush push = {.point = {13.0, 0.0, 0.0}, .radius = 0.8f, .depth = 0.01f};
    CHECK(mvfxInstance_PushWater(*context, id, 0, &push, 1) == mvfx_success, "pushed");
    return id;
}

// The heights of a surface at three points.
static void Heights(mvfxContext* context, mvfxInstanceId id, const double points[9], double out[3])
{
    mvfxWaterSample samples[3];
    CHECK(mvfxInstance_SampleWater(context, id, 0, points, 3, samples) == mvfx_success, "sampled");
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = samples[k].surface[1];
    }
}

// The most of a peak and a value at step s of 1/30, within the first
// arrival after a path from the push (from 1 before to 1.5 after it).
static void Peak(double* peak, double value, int32_t s, double path)
{
    bool first = s > (int32_t)(30.0 * (path - 1.0)) && s < (int32_t)(30.0 * (path + 1.5));
    *peak = first ? fmax(*peak, value) : *peak;
}

static void TestOpenAngles(void)
{
    // A pulse 3 from an open wall, against the same pulse with the wall
    // far: their difference is the reflection. At a probe whose
    // reflection met the wall at 0, 30 and 60 degrees, its peak over the
    // incident wave's after the same path, 6 / cos: Mur's first-order
    // edge reflects (1 - cos) / (1 + cos), 0, 0.072 and 0.333 (measured
    // at cells of 0.05: 0.018, 0.066, 0.374; at these cells of 0.1,
    // 0.024, 0.061, 0.360).
    mvfxContext* contexts[2];
    mvfxInstanceId open = OpenWide(&contexts[0], 320, 0.0f);
    mvfxInstanceId far = OpenWide(&contexts[1], 480, 8.0f);
    const double tangents[3] = {0.0, 0.57735027, 1.7320508};
    double probes[9];
    double incident[9];
    double paths[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        paths[k] = 6.0 * sqrt(1.0 + tangents[k] * tangents[k]);
        const double probe[3] = {13.0, 0.0, 6.0 * tangents[k]};
        const double back[3] = {13.0 - paths[k], 0.0, 0.0};
        memcpy(&probes[3 * k], probe, sizeof probe);
        memcpy(&incident[3 * k], back, sizeof back);
    }
    double reflected[3] = {0.0, 0.0, 0.0};
    double arrived[3] = {0.0, 0.0, 0.0};
    for (int32_t s = 1; s <= 30 * 14; ++s)
    {
        CHECK(mvfxContext_Step(contexts[0], 1.0 / 30.0) == mvfx_success, "stepped");
        CHECK(mvfxContext_Step(contexts[1], 1.0 / 30.0) == mvfx_success, "stepped");
        double a[3];
        double b[3];
        double c[3];
        Heights(contexts[0], open, probes, a);
        Heights(contexts[1], far, probes, b);
        Heights(contexts[1], far, incident, c);
        for (int32_t k = 0; k < 3; ++k)
        {
            Peak(&reflected[k], fabs(a[k] - b[k]), s, paths[k]);
            Peak(&arrived[k], fabs(c[k]), s, paths[k]);
        }
    }
    const double low[3] = {0.0, 0.03, 0.28};
    const double high[3] = {0.04, 0.1, 0.45};
    int32_t wrong = 0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double ratio = reflected[k] / arrived[k];
        wrong += ratio < low[k] || ratio > high[k];
    }
    CHECK(wrong == 0, "an open wall reflects as Mur's edge does at each angle");
    mvfxDestroyContext(contexts[0]);
    mvfxDestroyContext(contexts[1]);
}

// A rough surface: single cells pushed up and down alike at random.
static void Roughen(const mvfxWaterDef* water, uint32_t seed)
{
    mvfxWaterPush pushes[MVFX_MAX_WATER_PUSHES];
    for (int32_t p = 0; p < MVFX_MAX_WATER_PUSHES; ++p)
    {
        seed = seed * 1664525u + 1013904223u;
        int32_t x = (int32_t)((seed >> 8) % (uint32_t)water->size[0]);
        int32_t z = (int32_t)((seed >> 20) % (uint32_t)water->size[1]);
        pushes[p] = (mvfxWaterPush){
            .point = {((double)x + 0.5 - 0.5 * water->size[0]) * (double)water->cellSize, 0.0,
                      ((double)z + 0.5 - 0.5 * water->size[1]) * (double)water->cellSize},
            .radius = 0.4f * water->cellSize,
            .depth = (seed & 1u) != 0 ? 0.01f : -0.01f};
    }
    CHECK(mvfxInstance_PushWater(s_context, s_instance, 0, pushes, MVFX_MAX_WATER_PUSHES) ==
              mvfx_success,
          "roughened");
}

static void TestStability(void)
{
    // At the bound: a second's steps take the most substeps, each the
    // longest; the roughest waves (a checkerboard's) grow without end
    // beyond it. The energy taken at one instant swings with them (to
    // 1.13 of its start, measured).
    mvfxWaterDef water = Water(32, 32, 0.1f, 10.0f);
    CHECK(Open(&water, 1), "opened");
    Roughen(&water, 7u);
    Steps(1, 1.0 / 60.0);
    double volume = 0.0;
    double start = Energy(&water, &volume);
    double high = start;
    for (int32_t s = 0; s < 40; ++s)
    {
        Steps(1, 1.0);
        high = fmax(high, Energy(&water, &volume));
    }
    CHECK(high < 1.5 * start, "stable at the bound");
    // Each second's step outran the most substeps; the first did not, nor
    // one due exactly the most (63.5 of the longest, 0.9 h / (c sqrt 2)).
    Steps(1, 63.5 * 0.9 * 0.1 / (10.0 * sqrt(2.0)));
    mvfxWaterInfo info;
    CHECK(mvfxInstance_GetWaterInfo(s_context, s_instance, 0, &info) == mvfx_success &&
              info.stepsSlowed == 40 && info.facesHeld == 0,
          "each long step slowed");
    mvfxDestroyContext(s_context);
}

// The frame the frame tests put the instance in: moved, a quarter turn
// about Y, doubled; and the surface's center in it.
static const double s_origin[3] = {10.0, 2.0, -3.0};
static const float s_turn = 0.70710678f;
static const double s_center[3] = {0.3, 0.5, -0.2};

// A vector of the instance's frame turned into the world's directions.
static void Turn(const double v[3], double out[3])
{
    const double q[4] = {0.0, (double)s_turn, 0.0, (double)s_turn};
    const double t[3] = {2.0 * (q[1] * v[2] - q[2] * v[1]), 2.0 * (q[2] * v[0] - q[0] * v[2]),
                         2.0 * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

// A point of the instance's frame in the world.
static void ToWorld(const double p[3], double out[3])
{
    const double scaled[3] = {2.0 * p[0], 2.0 * p[1], 2.0 * p[2]};
    Turn(scaled, out);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] += s_origin[k];
    }
}

// The center of cell (x, z) of the frame tests' 16 by 8 surface of 0.1, at
// height y of the instance's frame, in the world.
static void CellInWorld(double x, double z, double y, double out[3])
{
    const double p[3] = {s_center[0] + (x + 0.5 - 8.0) * 0.1, y,
                         s_center[2] + (z + 0.5 - 4.0) * 0.1};
    ToWorld(p, out);
}

// The frame tests' surface opened in the frame: small dents at cells
// (3, 5), (15, 7) and (0, 0) and a wide lift at (10, 2), pushed in the world's lengths and
// taken without a step's time.
static mvfxWaterDef OpenInFrame(void)
{
    mvfxWaterDef water = Water(16, 8, 0.1f, 1.0f);
    for (int32_t k = 0; k < 3; ++k)
    {
        water.center[k] = (float)s_center[k];
    }
    CHECK(Open(&water, 1), "opened");
    const float rotation[4] = {0.0f, s_turn, 0.0f, s_turn};
    CHECK(mvfxInstance_SetTransform(s_context, s_instance, s_origin, rotation, 2.0f, true) ==
              mvfx_success,
          "moved");
    double at[3];
    CellInWorld(3.0, 5.0, 0.0, at);
    Push(at[0], at[1], at[2], 0.08f, 0.04f);
    CellInWorld(10.0, 2.0, 7.0, at);
    Push(at[0], at[1], at[2], 0.6f, -0.02f);
    CellInWorld(15.0, 7.0, 0.0, at);
    Push(at[0], at[1], at[2], 0.08f, 0.04f);
    CellInWorld(0.0, 0.0, 0.0, at);
    Push(at[0], at[1], at[2], 0.08f, 0.04f);
    Steps(1, 0.0);
    return water;
}

// The frame tests' heights after the pushes, at cell (x, z), before what
// they sank raises every cell.
static float Expected(int32_t x, int32_t z)
{
    float dx = (float)(x - 10) / 3.0f;
    float dz = (float)(z - 2) / 3.0f;
    float s = 1.0f - (dx * dx + dz * dz);
    float lift = s > 0.0f ? 0.01f * s * s : 0.0f;
    bool dent = (x == 3 && z == 5) || (x == 15 && z == 7) || (x == 0 && z == 0);
    return 0.5f + lift - (dent ? 0.02f : 0.0f);
}

// The frame tests' instance's pushes: the most a step holds, a batch past
// it refused whole; far ones missing the grid.
static void PushCapacity(const mvfxWaterDef* water)
{
    mvfxInstanceId id = s_instance;
    mvfxWaterPush pushes[MVFX_MAX_WATER_PUSHES];
    for (int32_t p = 0; p < MVFX_MAX_WATER_PUSHES; ++p)
    {
        pushes[p] =
            (mvfxWaterPush){.point = {s_origin[0], s_origin[1], s_origin[2]}, .radius = 0.1f};
    }
    CHECK(mvfxInstance_PushWater(s_context, id, 0, pushes, MVFX_MAX_WATER_PUSHES - 1) ==
              mvfx_success,
          "pushes held");
    CHECK(mvfxInstance_PushWater(s_context, id, 0, pushes, 2) == mvfx_errorCapacity,
          "a batch past the most refused");
    CHECK(mvfxInstance_PushWater(s_context, id, 0, pushes, 1) == mvfx_success, "the last held");
    CHECK(mvfxInstance_PushWater(s_context, id, 0, pushes, 1) == mvfx_errorCapacity, "then full");
    CHECK(mvfxInstance_PushWater(s_context, id, 0, nullptr, 0) == mvfx_success, "none");
    Steps(1, 0.0);
    // Far pushes, each side, miss the grid.
    float* before = Read(water, mvfx_waterHeight);
    const double far = 0x1p52;
    Push(far, 0.0, -far, 1.0f, 0.01f);
    Push(-far, 0.0, far, 1.0f, 0.01f);
    Steps(1, 0.0);
    float* after = Read(water, mvfx_waterHeight);
    int32_t moved = 0;
    for (int32_t i = 0; i < 16 * 8; ++i)
    {
        moved += before[i] != after[i];
    }
    CHECK(moved == 0, "far pushes miss");
    free(before);
    free(after);
    CHECK(mvfxInstance_PushWater(s_context, id, 0, pushes, 1) == mvfx_success, "taken by a step");
}

// The frame tests' instance's pushes refused, each counted as misuse;
// then stale, the instance destroyed.
static void PushRefusals(void)
{
    mvfxInstanceId id = s_instance;
    const mvfxWaterPush pushes[1] = {{.radius = 0.1f}};
    const mvfxWaterPush bad[6] = {
        {.point = {0.0, 0.0, 0.0}, .radius = 0.0f},
        {.point = {0.0, 0.0, 0.0}, .radius = 2.0f * MVFX_MAX_EXTENT},
        {.point = {0.0, 0.0, 0.0}, .radius = 1.0f, .depth = NAN},
        {.point = {0.0, 0.0, 0.0}, .radius = 1.0f, .depth = -2.0f * MVFX_MAX_EXTENT},
        {.point = {0.0, 0x1p53, 0.0}, .radius = 1.0f},
        {.point = {(double)NAN, 0.0, 0.0}, .radius = 1.0f},
    };
    uint64_t misuse = mvfxGetContextMisuse(s_context);
    int32_t wrong = 0;
    for (int32_t b = 0; b < 6; ++b)
    {
        // A radius or depth past MVFX_MAX_EXTENT is past a named bound.
        mvfxResult want = b == 1 || b == 3 ? mvfx_errorLimit : mvfx_errorInvalid;
        wrong += mvfxInstance_PushWater(s_context, id, 0, &bad[b], 1) != want;
    }
    wrong += mvfxInstance_PushWater(s_context, id, 0, nullptr, 1) != mvfx_errorInvalid;
    wrong += mvfxInstance_PushWater(s_context, id, 0, pushes, -1) != mvfx_errorInvalid;
    wrong += mvfxInstance_PushWater(s_context, id, 1, pushes, 1) != mvfx_errorInvalid;
    wrong += mvfxInstance_PushWater(s_context, id, -1, pushes, 1) != mvfx_errorInvalid;
    CHECK(wrong == 0, "pushes out of range, NULL, negative or of no surface refused");
    CHECK(mvfxGetContextMisuse(s_context) == misuse + 10, "misuse counted");
    CHECK(mvfxInstance_PushWater(nullptr, id, 0, pushes, 1) == mvfx_errorInvalid, "no context");
    CHECK(mvfxDestroyInstance(s_context, id) == mvfx_success, "destroyed");
    CHECK(mvfxInstance_PushWater(s_context, id, 0, pushes, 1) == mvfx_errorStale, "stale");
    mvfxDestroyContext(s_context);
}

// Whether every byte of a buffer is value.
static bool Filled(const void* buffer, size_t size, unsigned char value)
{
    const unsigned char* bytes = buffer;
    for (size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != value)
        {
            return false;
        }
    }
    return true;
}

static void TestPushes(void)
{
    mvfxWaterDef water = OpenInFrame();
    float* u = Read(&water, mvfx_waterHeight);
    double sank = 0.0;
    double held = 0.0;
    for (int32_t i = 0; i < 16 * 8; ++i)
    {
        sank += 0.5 - (double)Expected(i % 16, i / 16);
        held += (double)u[i] - 0.5;
    }
    bool exact = true;
    for (int32_t i = 0; i < 16 * 8; ++i)
    {
        exact =
            exact && fabs((double)u[i] - (double)Expected(i % 16, i / 16) - sank / 128.0) < 1e-6;
    }
    CHECK(exact, "pushes by their falloff, in the frame's lengths, the level raised");
    CHECK(fabs(sank) > 0.01 && fabs(held) < 1e-5, "the volume kept");
    CHECK(u[2 * 16 + 13] == u[16 + 1] && u[4 * 16 + 12] > u[16 + 1], "a push's reach");
    free(u);
    PushCapacity(&water);
    PushRefusals();
}

static void TestHeld(void)
{
    // Waves of 1e-6 a second over cells of 1e-6, the instance at the
    // least scale: 64 pushes at the most depth, two cells wide, sink its
    // center by 6.4e13 of the surface's lengths in one step. The step
    // holds every height within MVFX_MAX_WATER_DEPTH of the rest level,
    // each field finite.
    mvfxWaterDef water = Water(16, 16, 1e-6f, 1e-6f);
    CHECK(Open(&water, 1), "opened");
    static const double origin[3] = {0.0, 0.0, 0.0};
    static const float upright[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, upright, MVFX_MIN_SCALE, true) ==
              mvfx_success,
          "scaled");
    mvfxWaterPush pushes[MVFX_MAX_WATER_PUSHES];
    for (int32_t p = 0; p < MVFX_MAX_WATER_PUSHES; ++p)
    {
        pushes[p] =
            (mvfxWaterPush){.radius = 2.0f * 1e-6f * MVFX_MIN_SCALE, .depth = MVFX_MAX_EXTENT};
    }
    CHECK(mvfxInstance_PushWater(s_context, s_instance, 0, pushes, MVFX_MAX_WATER_PUSHES) ==
              mvfx_success,
          "pushed");
    Steps(1, 1.0 / 60.0);
    int32_t wrong = 0;
    for (mvfxWaterField f = mvfx_waterHeight; f <= mvfx_waterDepth; ++f)
    {
        float* v = Read(&water, f);
        for (int32_t i = 0; i < 256; ++i)
        {
            wrong += !isfinite(v[i]) ||
                     (f == mvfx_waterHeight && !(fabsf(v[i]) <= MVFX_MAX_WATER_DEPTH));
        }
        free(v);
    }
    CHECK(wrong == 0, "the heights held within MVFX_MAX_WATER_DEPTH, every field finite");
    mvfxDestroyContext(s_context);
}

// The unit normal at cell (x, z) of heights u, n by m of cell h, from
// central differences, one-sided at the edges.
static void NormalOf(const float* u, int32_t n, int32_t m, float h, int32_t x, int32_t z,
                     float out[3])
{
    int32_t x0 = x > 0 ? x - 1 : x;
    int32_t x1 = x + 1 < n ? x + 1 : x;
    int32_t z0 = z > 0 ? z - 1 : z;
    int32_t z1 = z + 1 < m ? z + 1 : z;
    float dx = (u[z * n + x1] - u[z * n + x0]) / ((float)(x1 - x0) * h);
    float dz = (u[z1 * n + x] - u[z0 * n + x]) / ((float)(z1 - z0) * h);
    float length = sqrtf(dx * dx + 1.0f + dz * dz);
    out[0] = -dx / length;
    out[1] = 1.0f / length;
    out[2] = -dz / length;
}

// Whether a box of surface 0's field read in unorm8 at a pitch is the
// float32 read mapped, the padding untouched.
static bool IsUnormBox(const float* all, mvfxWaterField field, float low, float high)
{
    unsigned char out[5 * 7];
    memset(out, 0xAB, sizeof out);
    const mvfxWaterRead read = {.field = field,
                                .format = mvfx_formatUnorm8,
                                .first = {2, 1},
                                .size = {4, 5},
                                .low = low,
                                .high = high,
                                .rowBytes = 7};
    bool same =
        mvfxInstance_ReadWater(s_context, s_instance, 0, &read, out, 4 * 7 + 4) == mvfx_success;
    for (int32_t z = 0; z < 5; ++z)
    {
        for (int32_t x = 0; x < 7; ++x)
        {
            float v = (all[(z + 1) * 16 + x + 2] - low) / (high - low);
            float clamped = v >= 0.0f ? fminf(v, 1.0f) : 0.0f;
            unsigned expected = x < 4 ? (unsigned)floorf(clamped * 255.0f + 0.5f) : 0xABu;
            same = same && out[z * 7 + x] == expected;
        }
    }
    return same;
}

// The frame tests' instance's reads refused, each counted as misuse.
static void ReadRefusals(void)
{
    const mvfxWaterRead good = {.field = mvfx_waterHeight,
                                .format = mvfx_formatUnorm8,
                                .first = {1, 1},
                                .size = {15, 7},
                                .low = 0.0f,
                                .high = 1.0f};
    mvfxWaterRead bad[10];
    for (int32_t b = 0; b < 10; ++b)
    {
        bad[b] = good;
    }
    bad[0].field = mvfx_waterDepth + 1;
    bad[1].format = 99;
    bad[2].first[0] = -1;
    bad[3].first[1] = 2;
    bad[4].size[0] = 0;
    bad[5].size[1] = 8;
    bad[6].rowBytes = 14;
    bad[7].high = 0.0f;
    bad[8].low = NAN;
    bad[9].high = INFINITY;
    unsigned char out[16 * 8];
    uint64_t misuse = mvfxGetContextMisuse(s_context);
    for (int32_t b = 0; b < 10; ++b)
    {
        CHECK(mvfxInstance_ReadWater(s_context, s_instance, 0, &bad[b], out, sizeof out) ==
                  mvfx_errorInvalid,
              "a read out of range refused");
    }
    CHECK(mvfxInstance_ReadWater(s_context, s_instance, 0, &good, out, 15 * 7 - 1) ==
              mvfx_errorInvalid,
          "too few bytes refused");
    CHECK(mvfxInstance_ReadWater(s_context, s_instance, 0, &good, out, 15 * 7) == mvfx_success,
          "enough bytes");
    CHECK(mvfxInstance_ReadWater(s_context, s_instance, 0, nullptr, out, 99) == mvfx_errorInvalid,
          "no read");
    CHECK(mvfxInstance_ReadWater(s_context, s_instance, 0, &good, nullptr, 99) == mvfx_errorInvalid,
          "no buffer");
    CHECK(mvfxInstance_ReadWater(s_context, s_instance, 1, &good, out, 99) == mvfx_errorInvalid,
          "index");
    CHECK(mvfxGetContextMisuse(s_context) == misuse + 14, "misuse counted");
    // What a refusal leaves: the span the caller gives zeroed.
    memset(out, 0xEE, sizeof out);
    CHECK(mvfxInstance_ReadWater(s_context, s_instance, 0, &bad[0], out, sizeof out) ==
                  mvfx_errorInvalid &&
              Filled(out, sizeof out, 0),
          "a read refused: its bytes zeroed");
    const mvfxInstanceId stale = {0};
    static const double points[6] = {0, 0, 0, 1, 0, 1};
    mvfxWaterSample samples[3];
    memset(samples, 0xEE, sizeof samples);
    CHECK(mvfxInstance_SampleWater(s_context, stale, 0, points, 2, samples) == mvfx_errorStale &&
              Filled(samples, 2 * sizeof samples[0], 0) &&
              Filled(&samples[2], sizeof samples[2], 0xEE),
          "samples of a stale instance: its two zeroed");
    mvfxDestroyContext(s_context);
}

static void TestReads(void)
{
    mvfxWaterDef water = OpenInFrame();
    float* u = Read(&water, mvfx_waterHeight);
    CHECK(IsUnormBox(u, mvfx_waterHeight, 0.48f, 0.512f), "a box, mapped, at a pitch");
    bool normals = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        float* read = Read(&water, (mvfxWaterField)(mvfx_waterNormalX + k));
        for (int32_t i = 0; i < 16 * 8; ++i)
        {
            float normal[3];
            NormalOf(u, 16, 8, 0.1f, i % 16, i / 16, normal);
            normals = normals && fabsf(read[i] - normal[k]) < 1e-6f;
        }
        free(read);
    }
    CHECK(normals, "normals from central differences, one-sided at the edges");
    Steps(1, 1.0 / 60.0);
    float* x = Read(&water, mvfx_waterVelocityX);
    float* y = Read(&water, mvfx_waterVelocityY);
    float* z = Read(&water, mvfx_waterVelocityZ);
    float most = 0.0f;
    bool across = true;
    for (int32_t i = 0; i < 16 * 8; ++i)
    {
        across = across && x[i] == 0.0f && z[i] == 0.0f;
        most = fmaxf(most, fabsf(y[i]));
    }
    CHECK(across && most > 0.01f, "waves move along Y");
    free(x);
    free(y);
    free(z);
    free(u);
    ReadRefusals();
}

// Whether two points of the world are within tolerance.
static bool Near(const double a[3], const double b[3], double tolerance)
{
    return fabs(a[0] - b[0]) < tolerance && fabs(a[1] - b[1]) < tolerance &&
           fabs(a[2] - b[2]) < tolerance;
}

// Samples surface 0 at one point.
static mvfxWaterSample Sample(const double point[3])
{
    mvfxWaterSample out = {0};
    CHECK(mvfxInstance_SampleWater(s_context, s_instance, 0, point, 1, &out) == mvfx_success,
          "sampled");
    return out;
}

// A sample's normal or velocity as binary64.
static void Widen(const float v[3], double out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (double)v[k];
    }
}

// The frame tests' instance sampled moving: the rate along the frame's Y,
// in the world's lengths.
static void SampleMotion(const mvfxWaterDef* water)
{
    Steps(1, 1.0 / 60.0);
    float* v = Read(water, mvfx_waterVelocityY);
    double at[3];
    CellInWorld(3.0, 5.0, 0.0, at);
    mvfxWaterSample sample = Sample(at);
    const double rate[3] = {0.0, 2.0 * (double)v[5 * 16 + 3], 0.0};
    double turned[3];
    Turn(rate, turned);
    double velocity[3];
    Widen(sample.velocity, velocity);
    CHECK(fabs(rate[1]) > 0.01 && Near(velocity, turned, 1e-5 * fabs(rate[1])), "velocity");
    free(v);
}

// The frame tests' instance's samples refused, each counted as misuse.
static void SampleRefusals(void)
{
    uint64_t misuse = mvfxGetContextMisuse(s_context);
    const double nan[3] = {0.0, (double)NAN, 0.0};
    const double at[3] = {0.0, 0.0, 0.0};
    mvfxWaterSample sample;
    mvfxInstanceId id = s_instance;
    int32_t wrong = 0;
    wrong += mvfxInstance_SampleWater(s_context, id, 0, nan, 1, &sample) != mvfx_errorInvalid;
    wrong += mvfxInstance_SampleWater(s_context, id, 0, at, -1, &sample) != mvfx_errorInvalid;
    wrong += mvfxInstance_SampleWater(s_context, id, 0, at, INT64_MAX / 3 + 1, &sample) !=
             mvfx_errorInvalid;
    wrong += mvfxInstance_SampleWater(s_context, id, 0, nullptr, 1, &sample) != mvfx_errorInvalid;
    wrong += mvfxInstance_SampleWater(s_context, id, 0, at, 1, nullptr) != mvfx_errorInvalid;
    wrong += mvfxInstance_SampleWater(s_context, id, 4, at, 1, &sample) != mvfx_errorInvalid;
    CHECK(wrong == 0, "samples not finite, negative, NULL or of no surface refused");
    CHECK(mvfxGetContextMisuse(s_context) == misuse + 6, "misuse counted");
    CHECK(mvfxInstance_SampleWater(s_context, id, 0, nullptr, 0, nullptr) == mvfx_success, "none");
}

static void TestSamples(void)
{
    mvfxWaterDef water = OpenInFrame();
    float* u = Read(&water, mvfx_waterHeight);
    // At a cell's center, its height and normal, in the world.
    double at[3];
    double expected[3];
    CellInWorld(3.0, 5.0, -4.0, at);
    mvfxWaterSample sample = Sample(at);
    CellInWorld(3.0, 5.0, (double)u[5 * 16 + 3], expected);
    float normal[3];
    NormalOf(u, 16, 8, 0.1f, 3, 5, normal);
    double local[3];
    Widen(normal, local);
    double turned[3];
    Turn(local, turned);
    double sampled[3];
    Widen(sample.normal, sampled);
    CHECK(sample.inside && Near(sample.surface, expected, 1e-5), "a cell's height");
    CHECK(Near(sampled, turned, 1e-5), "a cell's normal, turned");
    // Between two cells, bilinear; within the outer cells' half, held.
    CellInWorld(10.5, 2.0, 0.0, at);
    CellInWorld(10.5, 2.0, 0.5 * ((double)u[2 * 16 + 10] + (double)u[2 * 16 + 11]), expected);
    CHECK(Near(Sample(at).surface, expected, 1e-5), "between cells");
    CellInWorld(15.4, 7.4, 0.0, at);
    CellInWorld(15.4, 7.4, (double)u[7 * 16 + 15], expected);
    CHECK(Sample(at).inside && Near(Sample(at).surface, expected, 1e-5), "held at the edge");
    CellInWorld(-0.3, -0.4, 0.0, at);
    CellInWorld(-0.3, -0.4, (double)u[0], expected);
    CHECK(Sample(at).inside && Near(Sample(at).surface, expected, 1e-5), "and the other edge");
    // Off the grid: the rest plane, its normal the frame's Y, still.
    CellInWorld(-0.6, 3.0, 9.0, at);
    sample = Sample(at);
    CellInWorld(-0.6, 3.0, 0.5, expected);
    const double up[3] = {0.0, 1.0, 0.0};
    Turn(up, turned);
    Widen(sample.normal, sampled);
    CHECK(!sample.inside && Near(sample.surface, expected, 1e-5) && Near(sampled, turned, 1e-6),
          "off the grid, the rest plane");
    CellInWorld(3.0, 8.6, 0.0, at);
    CHECK(!Sample(at).inside, "off the grid along Z");
    free(u);
    SampleMotion(&water);
    SampleRefusals();
    mvfxDestroyContext(s_context);
}

// The info of a surface of a space, its instance moved after it began.
static void CheckInfo(mvfxGasSpace space)
{
    mvfxWaterDef water = Water(16, 8, 0.1f, 1.0f);
    water.space = space;
    for (int32_t c = 0; c < 3; ++c)
    {
        water.center[c] = (float)s_center[c];
    }
    CHECK(Open(&water, 1), "opened");
    const float rotation[4] = {0.0f, s_turn, 0.0f, s_turn};
    CHECK(mvfxInstance_SetTransform(s_context, s_instance, s_origin, rotation, 2.0f, true) ==
              mvfx_success,
          "moved");
    mvfxWaterInfo info;
    CHECK(mvfxInstance_GetWaterInfo(s_context, s_instance, 0, &info) == mvfx_success, "info");
    bool local = space == mvfx_gasLocal;
    double expected[3] = {s_center[0], s_center[1], s_center[2]};
    if (local)
    {
        ToWorld(s_center, expected);
    }
    int32_t wrong = !Near(info.center, expected, 1e-6);
    wrong += info.size[0] != 16;
    wrong += info.size[1] != 8;
    wrong += info.cellSize != (local ? 0.2f : 0.1f);
    wrong += info.scale != (local ? 2.0f : 1.0f);
    wrong += info.rotation[1] != (local ? s_turn : 0.0f);
    wrong += info.rotation[3] != (local ? s_turn : 1.0f);
    CHECK(wrong == 0, "a local surface moves with its instance, a world one stays");
    CHECK(mvfxInstance_GetWaterInfo(s_context, s_instance, 1, &info) == mvfx_errorInvalid &&
              info.size[0] == 0,
          "an index out of range zeroes it");
    CHECK(mvfxInstance_GetWaterInfo(s_context, s_instance, 0, nullptr) == mvfx_errorInvalid,
          "no out");
    mvfxDestroyContext(s_context);
}

static void TestInfo(void)
{
    CheckInfo(mvfx_gasLocal);
    CheckInfo(mvfx_gasWorld);
}

static void TestBounds(void)
{
    // A quarter turn and doubled: the grid's X along the world's -Z, its
    // reach along Y the largest displacement.
    mvfxWaterDef water = OpenInFrame();
    float* u = Read(&water, mvfx_waterHeight);
    double reach = 0.0;
    for (int32_t i = 0; i < 16 * 8; ++i)
    {
        reach = fmax(reach, fabs((double)u[i] - 0.5));
    }
    free(u);
    mvfxBounds bounds;
    CHECK(mvfxInstance_GetBounds(s_context, s_instance, -1, &bounds) == mvfx_success, "bounds");
    const double center[3] = {s_center[0], s_center[1], s_center[2]};
    double middle[3];
    ToWorld(center, middle);
    const double half[3] = {2.0 * 0.4, 2.0 * reach, 2.0 * 0.8};
    bool fits = !bounds.empty;
    for (int32_t k = 0; k < 3; ++k)
    {
        fits = fits && fabs(bounds.min[k] - (middle[k] - half[k])) < 1e-5 &&
               fabs(bounds.max[k] - (middle[k] + half[k])) < 1e-5;
    }
    CHECK(fits && reach > 0.02, "the grid's box in the world");
    mvfxDestroyContext(s_context);
}

// Steps until the instance is finished, at most limit steps: how many.
static int32_t StepsToFinish(int32_t limit)
{
    int32_t steps = 0;
    while (!mvfxInstance_IsFinished(s_context, s_instance) && steps < limit)
    {
        Steps(1, 1.0 / 60.0);
        ++steps;
    }
    return steps;
}

// A finished instance's surface pushed again: taken in a step of no time
// it has no rate yet, but a slope that holds the instance; cleared, its
// pushes not yet taken are dropped.
static void FinishAgain(void)
{
    Push(0.0, 0.0, 0.0, 0.3f, 0.01f);
    Steps(1, 0.0);
    CHECK(!mvfxInstance_IsFinished(s_context, s_instance), "a slope holds it");
    Push(0.0, 0.0, 0.0, 0.3f, 0.01f);
    CHECK(mvfxInstance_Stop(s_context, s_instance, mvfx_stopAndClear) == mvfx_success, "cleared");
    CHECK(mvfxInstance_IsFinished(s_context, s_instance), "its pushes dropped");
}

static void TestHighLevel(void)
{
    // A surface at a rest level of 1000 (its heights its displacement
    // from it, not the level): still at last, read and sampled there.
    mvfxWaterDef water = Water(16, 16, 0.1f, 1.0f);
    water.center[1] = 1000.0f;
    water.damping = 5.0f;
    CHECK(Open(&water, 1), "opened");
    Push(0.0, 1000.0, 0.0, 0.3f, 0.01f);
    Steps(1, 1.0 / 60.0);
    CHECK(mvfxInstance_Stop(s_context, s_instance, mvfx_stopEmitting) == mvfx_success, "stopped");
    int32_t steps = StepsToFinish(600);
    CHECK(steps > 60 && steps < 600, "still at a high level");
    float* u = Read(&water, mvfx_waterHeight);
    const double at[3] = {0.05, 0.0, 0.05};
    mvfxWaterSample sample = Sample(at);
    CHECK(u[8 * 16 + 8] == 1000.0f && fabs(sample.surface[1] - 1000.0) < 1e-6, "at its level");
    free(u);
    mvfxDestroyContext(s_context);
}

static void TestFinish(void)
{
    mvfxWaterDef water = Water(16, 16, 0.1f, 1.0f);
    water.damping = 5.0f;
    CHECK(Open(&water, 1), "opened");
    Push(0.0, 0.0, 0.0, 0.3f, 0.01f);
    Steps(1, 1.0 / 60.0);
    CHECK(mvfxInstance_Stop(s_context, s_instance, mvfx_stopEmitting) == mvfx_success, "stopped");
    Steps(1, 1.0 / 60.0);
    CHECK(!mvfxInstance_IsFinished(s_context, s_instance), "waves hold a stopped instance");
    int32_t steps = StepsToFinish(600);
    CHECK(steps > 60 && steps < 600, "still at last");
    Push(0.0, 0.0, 0.0, 0.3f, 0.01f);
    CHECK(!mvfxInstance_IsFinished(s_context, s_instance), "a push to take holds it");
    Steps(1, 1.0 / 60.0);
    CHECK(!mvfxInstance_IsFinished(s_context, s_instance), "its waves hold it");
    CHECK(mvfxInstance_Stop(s_context, s_instance, mvfx_stopAndClear) == mvfx_success, "cleared");
    CHECK(mvfxInstance_IsFinished(s_context, s_instance), "cleared, still");
    FinishAgain();
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestRefusals();
    TestPulse();
    TestEnergy();
    TestWalls();
    TestOpenAngles();
    TestStability();
    TestPushes();
    TestHeld();
    TestReads();
    TestSamples();
    TestInfo();
    TestBounds();
    TestFinish();
    TestHighLevel();
    return s_failures;
}
