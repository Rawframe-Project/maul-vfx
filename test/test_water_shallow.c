// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shallow water: the refusals; the volume exact in a closed basin through
// pushes and steps, sources adding exactly theirs while the instance
// plays, an open wall draining; depths never below 0; a dam break on a
// dry bed against Ritter's solution; a lake at rest still over a sloped
// terrain, the effect's or the host's; a leaned terrain's ground; a fall
// off a cliff as its mirror's; reads and samples of a lake; friction;
// pushes at their edges; a stopped instance finishing once still.

#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// A shallow surface of nx by nz cells of cell, walled in, a flat floor,
// dry.
static mvfxWaterDef Shallow(int32_t nx, int32_t nz, float cell)
{
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.model = mvfx_waterShallow;
    water.size[0] = nx;
    water.size[1] = nz;
    water.cellSize = cell;
    return water;
}

// Opens a context and an effect of the surfaces, its instance at the
// origin; false when the effect is refused.
static bool Open(const mvfxWaterDef* waters, int32_t count)
{
    if (s_context == NULL)
    {
        mvfxContextDef contextDef = mvfxDefaultContextDef();
        CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    }
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.waters = waters;
    def.waterCount = count;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(s_context, &def, &effect) != mvfx_success)
    {
        return false;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    return true;
}

static void Close(void)
{
    mvfxDestroyContext(s_context);
    s_context = NULL;
}

// Steps the context count times by seconds.
static void Steps(int32_t count, double seconds)
{
    for (int32_t i = 0; i < count; ++i)
    {
        CHECK(mvfxContext_Step(s_context, seconds) == mvfx_success, "stepped");
    }
}

// Surface 0's state.
static mvfxWaterState* State(void)
{
    return &mvfxContextInstance(s_context, s_instance)->waters[0];
}

// Surface 0's volume (its depths' sum times a cell's area) and whether
// every depth is at least 0.
static double Volume(const mvfxWaterDef* water, bool* dry)
{
    const mvfxWaterState* state = State();
    const float* d = state->heights[state->current];
    double sum = 0.0;
    *dry = true;
    for (int64_t i = 0; i < (int64_t)water->size[0] * water->size[1]; ++i)
    {
        sum += (double)d[i];
        *dry = *dry && d[i] >= 0.0f;
    }
    return sum * (double)water->cellSize * (double)water->cellSize;
}

// Spoils a valid shallow def by case c (none for 14).
static void Spoil(int32_t c, mvfxWaterDef* water, mvfxWaterSource* source)
{
    switch (c)
    {
    case 0:
        water->model = mvfx_waterShallow + 1;
        break;
    case 1:
        water->gravity = 0.0f;
        break;
    case 2:
        water->friction = -1.0f;
        break;
    case 3:
        water->fillLevel = NAN;
        break;
    case 4:
        water->sourceCount = MVFX_MAX_WATER_SOURCES + 1;
        break;
    case 5:
        water->sources = NULL;
        break;
    case 6:
        source->shape = mvfx_gasBox + 1;
        break;
    case 7:
        source->rate = 0.0f;
        break;
    case 8:
        source->radius = 0.0f;
        break;
    case 9:
        source->shape = mvfx_gasBox;
        source->halfExtents[2] = 0.0f;
        break;
    case 10:
        water->hasTerrain = true;
        water->terrain.kind = mvfx_colliderSphere;
        break;
    case 11:
        water->hasTerrain = true;
        water->terrain.space = 2;
        break;
    case 12:
        source->center[1] = INFINITY;
        break;
    case 13:
        water->gravity = 2e6f;
        break;
    default:
        break;
    }
}

static void TestRefusals(void)
{
    bool right = true;
    for (int32_t c = 0; c <= 14; ++c)
    {
        mvfxWaterDef water = Shallow(8, 8, 0.1f);
        mvfxWaterSource source = {.shape = mvfx_gasSphere, .radius = 0.1f, .rate = 1.0f};
        water.sources = &source;
        water.sourceCount = 1;
        Spoil(c, &water, &source);
        bool opened = Open(&water, 1);
        right = right && opened == (c == 14);
        Close();
    }
    CHECK(right, "a spoiled shallow def refused, a valid one opened");
}

static void TestVolume(void)
{
    // A basin filled to 0.5, pushed in and out and stepped: its volume
    // held to rounding (within 1e-6 of it), every depth at least 0.
    mvfxWaterDef water = Shallow(48, 32, 0.1f);
    water.fillLevel = 0.5f;
    CHECK(Open(&water, 1), "opened");
    Steps(1, 0.0);
    bool dry = true;
    double start = Volume(&water, &dry);
    double worst = 0.0;
    bool positive = true;
    for (int32_t s = 0; s < 300; ++s)
    {
        if (s % 20 == 0)
        {
            const mvfxWaterPush push = {.point = {-1.0 + 0.01 * s, 0.0, 0.3},
                                        .radius = 0.4f,
                                        .depth = s % 40 == 0 ? 0.3f : -0.2f};
            CHECK(mvfxInstance_PushWater(s_context, s_instance, 0, &push, 1) == mvfx_success,
                  "pushed");
        }
        Steps(1, 1.0 / 60.0);
        worst = fmax(worst, fabs(Volume(&water, &dry) / start - 1.0));
        positive = positive && dry;
    }
    CHECK(start > 0.0 && worst <= 1e-6 && positive, "a closed basin keeps its volume");
    Close();
}

static void TestSources(void)
{
    // Two sources over a closed dry basin, a disk and a box: the volume
    // grows by each rate times the cells their footprints cover times a
    // cell's area, every step; stopped, it stays.
    mvfxWaterDef water = Shallow(40, 40, 0.1f);
    const mvfxWaterSource sources[2] = {
        {.shape = mvfx_gasSphere, .center = {-0.5f, 0.0f, 0.5f}, .radius = 0.32f, .rate = 0.2f},
        {.shape = mvfx_gasBox,
         .center = {0.75f, 0.0f, -0.55f},
         .halfExtents = {0.32f, 0.0f, 0.22f},
         .rate = 0.1f},
    };
    water.sources = sources;
    water.sourceCount = 2;
    CHECK(Open(&water, 1), "opened");
    // The disk of 0.32 about (-0.5, 0.5), between centers, covers 32 of
    // them; the box 7 by 5. Sixty steps of 1/60 s are 60 of 17476 ticks.
    double rate = (32.0 * 0.2 + 35.0 * 0.1) * 0.01 * (60.0 * 17476.0 / 1048576.0);
    bool dry = true;
    Steps(60, 1.0 / 60.0);
    double poured = Volume(&water, &dry);
    CHECK(fabs(poured / rate - 1.0) <= 1e-4, "sources add exactly theirs");
    CHECK(mvfxInstance_Stop(s_context, s_instance, mvfx_stopEmitting) == mvfx_success, "stopped");
    Steps(60, 1.0 / 60.0);
    CHECK(fabs(Volume(&water, &dry) / poured - 1.0) <= 1e-5, "stopped, they pour no more");
    Close();
}

static void TestDrain(void)
{
    // A flat basin filled to 0.2 with its +X wall open loses water every
    // step, and after 20 seconds holds below 10% of it (4.8%, measured: the
    // film at the edge thins, and drains ever slower); a walled one keeps
    // it.
    for (int32_t k = 0; k < 2; ++k)
    {
        mvfxWaterDef water = Shallow(32, 8, 0.1f);
        water.fillLevel = 0.2f;
        water.closedWalls = k == 0 ? (mvfxWaterWalls)(15 & ~mvfx_waterWallMaxX) : 15;
        CHECK(Open(&water, 1), "opened");
        Steps(1, 0.0);
        bool dry = true;
        double start = Volume(&water, &dry);
        double end = start;
        bool falls = true;
        for (int32_t s = 0; s < 1200; ++s)
        {
            Steps(1, 1.0 / 60.0);
            double now = Volume(&water, &dry);
            falls = falls && (k == 1 || now < end);
            end = now;
        }
        CHECK(k == 1 || (falls && end < 0.1 * start), "an open wall drains");
        CHECK(k == 0 || fabs(end / start - 1.0) <= 1e-6, "a walled basin keeps its water");
        Close();
    }
}

// Ritter's depth for a dam of depth h0 let go on a dry bed, at x past
// the dam after t: (2 c0 - x / t)^2 / (9 g) between -c0 t and 2 c0 t.
static double Ritter(double h0, double x, double t)
{
    double c0 = sqrt(9.81 * h0);
    double s = x / t;
    return s <= -c0 ? h0 : (s >= 2.0 * c0 ? 0.0 : (2.0 * c0 - s) * (2.0 * c0 - s) / (9.0 * 9.81));
}

static void TestDamBreak(void)
{
    // A strip of 512 by 2 cells of 0.01, water 0.5 deep on its -X half,
    // let go on a dry bed and stepped at 120 a second: after 0.4 s its
    // depth from 0.56 behind the dam to 0.8 past it is within 7% of
    // Ritter's (measured within 5.5%) and at the dam within 2% (0.8%), as
    // its velocity there, 2 c0 / 3 (1%); its front, the farthest cell
    // deeper than 5e-4, from 75% to 100% of his 2 c0 t (83%: a first
    // order scheme thins the tip).
    mvfxWaterDef water = Shallow(512, 2, 0.01f);
    CHECK(Open(&water, 1), "opened");
    Steps(1, 0.0);
    mvfxWaterState* state = State();
    float* d = state->heights[state->current];
    for (int32_t i = 0; i < 1024; ++i)
    {
        d[i] = i % 512 < 256 ? 0.5f : 0.0f;
    }
    Steps(48, 1.0 / 120.0);
    double t = 48.0 * 8738.0 / 1048576.0;
    d = state->heights[state->current];
    double worst = 0.0;
    for (int32_t x = 200; x <= 336; x += 8)
    {
        double at = (double)(x - 256) * 0.01 + 0.005;
        worst = fmax(worst, fabs((double)d[x] / Ritter(0.5, at, t) - 1.0));
    }
    int32_t last = 256;
    for (int32_t x = 256; x < 512; ++x)
    {
        last = d[x] > 5e-4f ? x : last;
    }
    double front = (double)(last + 1 - 256) * 0.01 / (2.0 * sqrt(9.81 * 0.5) * t);
    double dam = 0.5 * ((double)d[255] + (double)d[256]) / (0.5 * 4.0 / 9.0);
    double u = (double)state->faces[0][state->faceCurrent][256] / (2.0 / 3.0 * sqrt(9.81 * 0.5));
    CHECK(worst <= 0.07, "a dam break's depths as Ritter's");
    CHECK(fabs(dam - 1.0) <= 0.02 && fabs(u - 1.0) <= 0.02, "at the dam as Ritter's");
    CHECK(front >= 0.75 && front <= 1.0, "a dam break's front behind Ritter's");
    Close();
}

// A heightfield of the instance's frame of 33 by 33 points over 4 by 4
// about the origin, its height a x + b at x along its X, turned by
// rotation.
static mvfxCollider Field(float a, float b, const float rotation[4])
{
    static float heights[33 * 33];
    for (int32_t i = 0; i < 33 * 33; ++i)
    {
        heights[i] = b + a * (-2.0f + (float)(i % 33) / 8.0f);
    }
    mvfxGridDef grid = mvfxDefaultGridDef();
    grid.size[0] = 33;
    grid.size[1] = 1;
    grid.size[2] = 33;
    grid.values = heights;
    mvfxGridId id = {0};
    CHECK(mvfxCreateGrid(s_context, &grid, &id) == mvfx_success, "grid");
    mvfxCollider terrain = {.kind = mvfx_colliderHeightfield,
                            .space = 1,
                            .halfExtents = {2.0f, 1.0f, 2.0f},
                            .grid = id};
    memcpy(terrain.rotation, rotation, sizeof terrain.rotation);
    return terrain;
}

// A ground sloping from -0.5 at -X to 0.5 at +X (and, turned, along Z).
static mvfxCollider Slope(const float rotation[4])
{
    return Field(0.25f, 0.0f, rotation);
}

// A lake filled to 0 over the slope (its -X half wet, up to 0.5 deep),
// stepped five seconds: whether it is still (every face still, every
// depth as filled to 1e-6); its depths after.
static bool StillLake(bool host, const float rotation[4], float* depths)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxWaterDef water = Shallow(40, 40, 0.1f);
    mvfxCollider terrain = Slope(rotation);
    water.terrain = terrain;
    water.hasTerrain = !host;
    water.hostTerrain = host;
    CHECK(Open(&water, 1), "opened");
    if (host)
    {
        // The host's first heightfield, past a sphere.
        const mvfxCollider colliders[2] = {
            {.kind = mvfx_colliderSphere, .space = 1, .radius = 0.5f, .rotation = {0, 0, 0, 1}},
            terrain};
        CHECK(mvfxInstance_SetColliders(s_context, s_instance, colliders, 2) == mvfx_success,
              "host terrain");
    }
    Steps(1, 0.0);
    const mvfxWaterState* state = State();
    float filled[1600];
    memcpy(filled, state->heights[state->current], sizeof filled);
    Steps(300, 1.0 / 60.0);
    const float* d = state->heights[state->current];
    bool still = !state->holds;
    for (int32_t i = 0; i < 1600; ++i)
    {
        still = still && fabsf(d[i] - filled[i]) <= 1e-6f;
        depths[i] = d[i];
    }
    return still;
}

static void TestLake(void)
{
    // The effect's terrain and the host's alike, still; the wet cells the
    // -X half, each as deep as the ground is below 0; turned a quarter
    // about Y, the slope runs along Z instead.
    static const float upright[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    static const float turned[4] = {0.0f, 0.70710678f, 0.0f, 0.70710678f};
    static float own[1600];
    static float host[1600];
    static float quarter[1600];
    CHECK(StillLake(false, upright, own), "a lake at rest stays at rest");
    Close();
    CHECK(StillLake(true, upright, host), "over the host's terrain too");
    Close();
    int32_t differ = 0;
    for (int32_t i = 0; i < 1600; ++i)
    {
        differ += own[i] != host[i];
    }
    CHECK(differ == 0, "the host's terrain as the effect's");
    CHECK(StillLake(false, turned, quarter), "over a turned terrain");
    Close();
    int32_t wrong = 0;
    for (int32_t i = 0; i < 1600; ++i)
    {
        // Cell (x, z)'s center at x = (x + 0.5 - 20) 0.1: the ground there
        // is x / 4 (X for upright, -Z turned a quarter, by the rotation).
        int32_t column = i % 40;
        int32_t row = i / 40;
        float at = ((float)column + 0.5f - 20.0f) * 0.1f;
        float along = ((float)row + 0.5f - 20.0f) * 0.1f;
        wrong += fabsf(own[i] - fmaxf(-at / 4.0f, 0.0f)) > 1e-4f;
        wrong += fabsf(quarter[i] - fmaxf(along / 4.0f, 0.0f)) > 1e-4f;
    }
    CHECK(wrong == 0, "filled to the level over the ground");
}

// The ground under a surface of 20 by 20 cells of 0.1 over a field of
// height a x + b leaned 30 degrees about Z with its center 1 up: the
// cells whose ground is off the field's plane by over 1e-4.
static int32_t Leaned(float a, float b)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxWaterDef water = Shallow(20, 20, 0.1f);
    static const float leaned[4] = {0.0f, 0.0f, 0.25881904f, 0.96592583f};
    water.terrain = Field(a, b, leaned);
    water.terrain.center[1] = 1.0f;
    water.hasTerrain = true;
    CHECK(Open(&water, 1), "opened");
    Steps(1, 0.0);
    const float* ground = State()->ground;
    // Over the box's (X, Y) the field's frame is (c X + s Y, c Y - s X)
    // from its center, so its plane y = a x + b is met at Y (c - a s) =
    // X (s + a c) + b.
    double c = sqrt(3.0) / 2.0;
    double sn = 0.5;
    int32_t wrong = 0;
    for (int32_t i = 0; i < 400; ++i)
    {
        double x = ((double)(i % 20) + 0.5 - 10.0) * 0.1;
        double y = 1.0 + (x * (sn + (double)a * c) + (double)b) / (c - (double)a * sn);
        wrong += fabs((double)ground[i] - y) > 1e-4;
    }
    Close();
    return wrong;
}

static void TestLeaned(void)
{
    // A level field and a sloped one, leaned, each met where the vertical
    // meets its plane.
    CHECK(Leaned(0.0f, 0.2f) == 0, "a leaned level ground");
    CHECK(Leaned(0.25f, 0.0f) == 0, "a leaned sloped ground");
}

// A spring of 0.5 a second on a plateau 1 up over the +X half of a
// channel of 32 by 4 cells of 0.05 (turned a half about Y, the -X half),
// falling off its cliff for a second: the depths after.
static void Cliff(bool turned, float depths[128])
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    // 65 by 2 points over 1.6 by 0.2: those at the cells' centers from
    // the +X half's first on 1, the rest 0.
    static float heights[65 * 2];
    for (int32_t i = 0; i < 65 * 2; ++i)
    {
        heights[i] = i % 65 >= 33 ? 1.0f : 0.0f;
    }
    mvfxGridDef grid = mvfxDefaultGridDef();
    grid.size[0] = 65;
    grid.size[1] = 1;
    grid.size[2] = 2;
    grid.values = heights;
    mvfxWaterDef water = Shallow(32, 4, 0.05f);
    water.terrain =
        (mvfxCollider){.kind = mvfx_colliderHeightfield,
                       .space = 1,
                       .halfExtents = {0.8f, 1.0f, 0.1f},
                       .rotation = {0.0f, turned ? 1.0f : 0.0f, 0.0f, turned ? 0.0f : 1.0f}};
    CHECK(mvfxCreateGrid(s_context, &grid, &water.terrain.grid) == mvfx_success, "grid");
    water.hasTerrain = true;
    mvfxWaterSource source = {
        .shape = mvfx_gasSphere, .center = {turned ? -0.4f : 0.4f}, .radius = 0.1f, .rate = 0.5f};
    water.sources = &source;
    water.sourceCount = 1;
    CHECK(Open(&water, 1), "opened");
    Steps(60, 1.0 / 60.0);
    const mvfxWaterState* state = State();
    memcpy(depths, state->heights[state->current], 128 * sizeof(float));
    Close();
}

static void TestCliff(void)
{
    // Water falling off a cliff toward -X as its mirror falls toward +X:
    // each cell as its mirror's within a thousandth of the deepest.
    static float off[128];
    static float mirror[128];
    Cliff(false, off);
    Cliff(true, mirror);
    float deepest = 0.0f;
    float worst = 0.0f;
    for (int32_t i = 0; i < 128; ++i)
    {
        int32_t x = i % 32;
        int32_t z = i / 32;
        deepest = fmaxf(deepest, off[i]);
        worst = fmaxf(worst, fabsf(off[i] - mirror[(3 - z) * 32 + 31 - x]));
    }
    CHECK(deepest > 0.01f && worst <= 1e-3f * deepest, "a cliff's fall as its mirror's");
}

// Surface 0's field over its 40 by 40 cells, as binary32.
static void ReadField(mvfxWaterField field, float out[1600])
{
    const mvfxWaterRead read = {.field = field, .format = mvfx_formatFloat32, .size = {40, 40}};
    CHECK(mvfxInstance_ReadWater(s_context, s_instance, 0, &read, out, 1600 * sizeof(float)) ==
              mvfx_success,
          "read");
}

static void TestReads(void)
{
    // A lake over the slope: its height read is the level (0) where wet
    // and the ground where dry, its depth the level less the ground; a
    // sample over the water meets the level, over dry land the ground;
    // still, its flow 0.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxWaterDef water = Shallow(40, 40, 0.1f);
    static const float upright[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    water.terrain = Slope(upright);
    water.hasTerrain = true;
    CHECK(Open(&water, 1), "opened");
    CHECK(mvfxDestroyGrid(s_context, water.terrain.grid) == mvfx_errorBusy,
          "the effect holds its terrain's grid");
    Steps(2, 1.0 / 60.0);
    static float height[1600];
    static float depth[1600];
    static float flow[1600];
    ReadField(mvfx_waterHeight, height);
    ReadField(mvfx_waterDepth, depth);
    ReadField(mvfx_waterVelocityX, flow);
    int32_t wrong = 0;
    for (int32_t i = 0; i < 1600; ++i)
    {
        int32_t column = i % 40;
        float ground = ((float)column + 0.5f - 20.0f) * 0.1f / 4.0f;
        wrong += fabsf(height[i] - fmaxf(ground, 0.0f)) > 1e-4f;
        wrong += fabsf(depth[i] - fmaxf(-ground, 0.0f)) > 1e-4f;
        wrong += flow[i] != 0.0f;
    }
    CHECK(wrong == 0, "a lake's heights, depths and flow read");
    const double points[6] = {-1.23, 5.0, 0.4, 1.17, -3.0, -0.6};
    mvfxWaterSample samples[2];
    CHECK(mvfxInstance_SampleWater(s_context, s_instance, 0, points, 2, samples) == mvfx_success,
          "sampled");
    CHECK(samples[0].inside && fabs(samples[0].surface[1]) <= 1e-5 &&
              fabs(samples[1].surface[1] - 1.17 / 4.0) <= 1e-3,
          "a lake sampled over water and land");
    Close();
}

// A basin filled to 0.3, pushed once at its middle and stepped seconds
// under friction: its flow's largest speed after.
static float Slosh(float friction, double seconds)
{
    mvfxWaterDef water = Shallow(32, 32, 0.1f);
    water.fillLevel = 0.3f;
    water.friction = friction;
    CHECK(Open(&water, 1), "opened");
    const mvfxWaterPush push = {.point = {0.0, 0.0, 0.0}, .radius = 0.5f, .depth = 0.2f};
    CHECK(mvfxInstance_PushWater(s_context, s_instance, 0, &push, 1) == mvfx_success, "pushed");
    Steps((int32_t)(seconds * 60.0), 1.0 / 60.0);
    float most = 0.0f;
    const mvfxWaterState* state = State();
    for (int32_t i = 0; i < 33 * 32; ++i)
    {
        most = fmaxf(most, fabsf(state->faces[0][state->faceCurrent][i]));
    }
    Close();
    return most;
}

static void TestFriction(void)
{
    // After three seconds of a slosh, friction 2 has slowed the water to
    // below a tenth of the frictionless (0.062, measured: a wave's speed
    // dies as exp(-friction t / 2), gravity trading its energy between
    // flow and height, exp(-3) = 0.050).
    float free = Slosh(0.0f, 3.0);
    float slowed = Slosh(2.0f, 3.0);
    CHECK(free > 0.01f && slowed < 0.1f * free && slowed > 0.02f * free, "friction slows the flow");
}

// A push of 0.1 (a lift for sink false) and radius 0.3 on water 0.3 deep:
// whether its middle cell sinks by 0.1 (rises), a cell 1.5 radii out
// rises (sinks), one past twice the radius stays, and the volume stays.
static bool PushRing(bool sink)
{
    mvfxWaterDef water = Shallow(32, 32, 0.1f);
    water.fillLevel = 0.3f;
    CHECK(Open(&water, 1), "opened");
    Steps(1, 0.0);
    bool dry = true;
    double start = Volume(&water, &dry);
    const mvfxWaterPush push = {
        .point = {0.05, 0.0, 0.05}, .radius = 0.3f, .depth = sink ? 0.1f : -0.1f};
    CHECK(mvfxInstance_PushWater(s_context, s_instance, 0, &push, 1) == mvfx_success, "pushed");
    Steps(1, 0.0);
    const float* d = State()->heights[State()->current];
    float middle = d[16 * 32 + 16];
    float ring = d[16 * 32 + 16 + 4];
    float past = d[16 * 32 + 16 + 7];
    bool moved =
        sink ? (fabsf(middle - 0.2f) <= 1e-6f && ring > 0.3f) : (middle > 0.3f && ring < 0.3f);
    bool kept = fabs(Volume(&water, &dry) / start - 1.0) <= 1e-6;
    Close();
    return moved && past == 0.3f && kept;
}

static void TestPushRing(void)
{
    CHECK(PushRing(true) && PushRing(false), "a push moves water between its middle and ring");
}

// Pushes once a surface of n by n cells of 0.1 filled to level: the
// cells whose depth changed, whether the volume stayed (within 1e-6) and
// every depth is at least 0; its middle cell's depth after.
static int32_t PushOnce(int32_t n, float level, const mvfxWaterPush* push, bool* kept,
                        float* middle)
{
    mvfxWaterDef water = Shallow(n, n, 0.1f);
    water.fillLevel = level;
    CHECK(Open(&water, 1), "opened");
    Steps(1, 0.0);
    bool dry = true;
    double start = Volume(&water, &dry);
    CHECK(mvfxInstance_PushWater(s_context, s_instance, 0, push, 1) == mvfx_success, "pushed");
    Steps(1, 0.0);
    const float* d = State()->heights[State()->current];
    int32_t changed = 0;
    for (int32_t i = 0; i < n * n; ++i)
    {
        changed += d[i] != level;
    }
    *middle = d[(n / 2) * n + n / 2];
    *kept = fabs(Volume(&water, &dry) / start - 1.0) <= 1e-6 && dry;
    Close();
    return changed;
}

static void TestPushEdges(void)
{
    // A push deeper than the water takes what is there and no more; one
    // with no cell inside it (a lift between cells' centers) or no ring on
    // the grid (a sink over all of it) changes nothing.
    bool kept = false;
    float middle = 0.0f;
    const mvfxWaterPush deep = {.point = {0.05, 0.0, 0.05}, .radius = 0.3f, .depth = 0.3f};
    CHECK(PushOnce(32, 0.05f, &deep, &kept, &middle) > 0 && kept && middle == 0.0f,
          "a push deeper than the water empties its middle");
    const mvfxWaterPush between = {.point = {0.0, 0.0, 0.0}, .radius = 0.04f, .depth = -0.1f};
    CHECK(PushOnce(32, 0.3f, &between, &kept, &middle) == 0 && kept,
          "a lift between cells' centers changes nothing");
    const mvfxWaterPush over = {.point = {0.0, 0.0, 0.0}, .radius = 1.0f, .depth = 0.1f};
    CHECK(PushOnce(4, 0.3f, &over, &kept, &middle) == 0 && kept,
          "a sink with no ring on the grid changes nothing");
}

static void TestFinish(void)
{
    // A stopped instance's surface still moving keeps it from finishing
    // until friction has stilled it.
    mvfxWaterDef water = Shallow(32, 32, 0.1f);
    water.fillLevel = 0.3f;
    water.friction = 2.0f;
    CHECK(Open(&water, 1), "opened");
    CHECK(mvfxInstance_Stop(s_context, s_instance, mvfx_stopEmitting) == mvfx_success, "stopped");
    const mvfxWaterPush push = {.point = {0.0, 0.0, 0.0}, .radius = 0.5f, .depth = 0.2f};
    CHECK(mvfxInstance_PushWater(s_context, s_instance, 0, &push, 1) == mvfx_success, "pushed");
    Steps(1, 1.0 / 60.0);
    bool moving = !mvfxInstance_IsFinished(s_context, s_instance);
    Steps(1800, 1.0 / 60.0);
    CHECK(moving && mvfxInstance_IsFinished(s_context, s_instance), "finished once still");
    Close();
}

int main(void)
{
    TestRefusals();
    TestVolume();
    TestSources();
    TestDrain();
    TestDamBreak();
    TestLake();
    TestLeaned();
    TestCliff();
    TestReads();
    TestFriction();
    TestPushRing();
    TestPushEdges();
    TestFinish();
    return s_failures;
}
