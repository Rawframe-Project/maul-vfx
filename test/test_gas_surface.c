// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Surface gases (a distance grid from particles): the refusals; a lone
// particle's sphere, and circle, exact; the largest eigenvalue against a
// binary64 reference; a jittered block's flat face where it belongs; two
// drops kept apart by the correction and joined without it; the distance
// read out and sampled.

#include "gas.h"
#include "gas_surface.h"
#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// A surface gas of n by n by depth cells of cell centered on the origin,
// wrapping emitter 0's particles of radius.
static mvfxGasDef Surface(int32_t n, int32_t depth, float cell, float radius)
{
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.kind = mvfx_gasSurface;
    gas.size[0] = n;
    gas.size[1] = n;
    gas.size[2] = depth;
    gas.cellSize = cell;
    gas.surface.radius = radius;
    return gas;
}

// Plain particles from the host, long lived, still.
static mvfxEmitterDef Emitter(int32_t capacity)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = capacity;
    emitter.hostSpawnCapacity = capacity;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    return emitter;
}

// Opens an effect of the emitters and gases; false when refused.
static bool Open(const mvfxEmitterDef* emitters, int32_t emitterCount, const mvfxGasDef* gases,
                 int32_t gasCount)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = emitterCount;
    def.gases = gases;
    def.gasCount = gasCount;
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

// Spawns particles at places and steps one tick to bear them, the surface
// made anew from them.
static void Spawn(const double* places, int32_t count)
{
    mvfxSpawnRequest request = {.count = count, .positions = places};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "born");
}

// The surface's distance, all its cells.
static float* Distance(int64_t cells)
{
    float* out = malloc(sizeof(float) * (size_t)cells);
    CHECK(out != NULL, "memory");
    CHECK(mvfxInstance_ReadGas(s_context, s_instance, 0, mvfx_gasDistance, out, cells) ==
              mvfx_success,
          "read");
    return out;
}

static const mvfxGasSource s_source = {.shape = mvfx_gasSphere, .radius = 0.1f, .density = 1};
static const mvfxForce s_push = {.kind = mvfx_forceConstant, .strength = 1, .vector = {1, 0, 0}};
static const mvfxForce s_follow = {.kind = mvfx_forceGas, .strength = 1, .gas = 0};
static const mvfxCollider s_ball = {.kind = mvfx_colliderSphere, .radius = 0.1f};

// Spoils a valid surface and its emitter by case c (none for 13).
static void Spoil(int32_t c, mvfxGasDef* gas, mvfxEmitterDef* emitter)
{
    switch (c)
    {
    case 0:
        gas->sources = &s_source;
        gas->sourceCount = 1;
        break;
    case 1:
        gas->forces = &s_push;
        gas->forceCount = 1;
        break;
    case 2:
        gas->obstacles = &s_ball;
        gas->obstacleCount = 1;
        break;
    case 3:
        gas->hostObstacles = true;
        break;
    case 4:
        gas->substeps = 2;
        break;
    case 5:
        gas->surface.radius = 0.0f;
        break;
    case 6:
        gas->surface.reach = 0.5f;
        break;
    case 7:
        gas->surface.reach = 9.0f;
        break;
    case 8:
        gas->surface.smoothing = 9;
        break;
    case 9:
        gas->surface.low = -0.1f;
        break;
    case 10:
        gas->surface.emitter = 1;
        break;
    case 11:
        emitter->gasSplat = (mvfxGasSplat){.enabled = true, .gas = 0, .density = 1};
        break;
    case 12:
        emitter->forces = &s_follow;
        emitter->forceCount = 1;
        break;
    default:
        break;
    }
}

static void TestRefusals(void)
{
    // A surface with a source, a force, an obstacle, the host's obstacles,
    // two substeps, radius 0, reach 0.5 or 9, smoothing 9, a negative
    // threshold or an emitter out of range; an emitter splatting into a
    // surface, or a force following one. Then the valid pair is taken.
    bool right = true;
    for (int32_t c = 0; c < 14; ++c)
    {
        mvfxGasDef gas = Surface(8, 8, 0.1f, 0.1f);
        mvfxEmitterDef emitter = Emitter(8);
        Spoil(c, &gas, &emitter);
        bool opened = Open(&emitter, 1, &gas, 1);
        if (opened)
        {
            mvfxDestroyContext(s_context);
        }
        right = right && opened == (c == 13);
    }
    CHECK(right, "surfaces' refusals");
}

static void TestReads(void)
{
    // A surface reads only its distance, a smoke gas never the distance;
    // sampled beyond the box, a surface's distance is its reach.
    mvfxGasDef gases[2] = {Surface(8, 8, 0.1f, 0.1f), mvfxDefaultGasDef()};
    mvfxEmitterDef emitter = Emitter(8);
    CHECK(Open(&emitter, 1, gases, 2), "opened");
    static float out[64 * 64];
    bool right = mvfxInstance_ReadGas(s_context, s_instance, 0, mvfx_gasDensity, out, 512) ==
                     mvfx_errorInvalid &&
                 mvfxInstance_ReadGas(s_context, s_instance, 1, mvfx_gasDistance, out, 4096) ==
                     mvfx_errorInvalid &&
                 mvfxInstance_ReadGas(s_context, s_instance, 0, mvfx_gasDistance, out, 512) ==
                     mvfx_success &&
                 out[0] == 0.4f;
    const double far[3] = {5.0, 0.0, 0.0};
    float sampled = 0.0f;
    right = right &&
            mvfxInstance_SampleGas(s_context, s_instance, 0, mvfx_gasDistance, far, 1, &sampled) ==
                mvfx_success &&
            sampled == 0.4f;
    mvfxDestroyContext(s_context);
    CHECK(right, "a surface's distance read and sampled");
}

// Whether a lone particle at p gives, in a gas of n by n by depth cells
// (depth n or 1) of cell and particles of radius and reach, |x - p| -
// radius within 2e-6 where x is within the reach of p and the reach
// beyond.
static bool LoneParticle(int32_t n, int32_t depth, const double p[3], float cell, float radius,
                         float reach)
{
    mvfxGasDef gas = Surface(n, depth, cell, radius);
    gas.surface.reach = reach;
    gas.surface.smoothing = 0;
    mvfxEmitterDef emitter = Emitter(4);
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    Spawn(p, 1);
    int64_t cells = (int64_t)n * n * depth;
    float* phi = Distance(cells);
    double r = (double)reach * (double)radius;
    float worst = 0.0f;
    for (int64_t i = 0; i < cells; ++i)
    {
        int32_t at[3] = {(int32_t)(i % n), (int32_t)(i / n % n), (int32_t)(i / n / n)};
        double d2 = 0.0;
        for (int32_t k = 0; k < (depth > 1 ? 3 : 2); ++k)
        {
            double x = ((double)at[k] + 0.5 - 0.5 * (double)n) * (double)cell;
            d2 += (x - p[k]) * (x - p[k]);
        }
        double d = sqrt(d2);
        double expected = d < r ? d - (double)radius : r;
        worst = fmaxf(worst, (float)fabs((double)phi[i] - expected));
    }
    free(phi);
    mvfxDestroyContext(s_context);
    return worst <= 2e-6f;
}

static void TestLoneParticle(void)
{
    // A sphere and a circle; a reach of 3.3 radii (buckets of whole cells
    // past it), and of 3.9 (7.8 cells) in a box of 32 with the particle
    // near a bucket's low end, a cell 7.6 cells off two buckets of 7 away;
    // a particle beyond the box's far X face, within reach of its cells;
    // one exactly at the buckets' far Z edge, which counts for none (cells
    // of 1/16, the reach 8 cells).
    const double inside[3] = {0.013, -0.021, 0.007};
    const double flat[3] = {0.013, -0.021, 0.0};
    const double low[3] = {-0.095, 0.013, 0.007};
    const double beyond[3] = {0.45, -0.021, 0.007};
    const double edge[3] = {0.0, 0.0, 1.0};
    CHECK(LoneParticle(16, 16, inside, 0.05f, 0.1f, 4.0f) &&
              LoneParticle(16, 1, flat, 0.05f, 0.1f, 4.0f) &&
              LoneParticle(16, 16, inside, 0.05f, 0.1f, 3.3f) &&
              LoneParticle(32, 32, low, 0.05f, 0.1f, 3.9f) &&
              LoneParticle(16, 16, beyond, 0.05f, 0.1f, 4.0f) &&
              LoneParticle(16, 16, edge, 0.0625f, 0.125f, 4.0f),
          "a lone particle's sphere and circle");
}

// The field of a lone particle in a gas of 16 by 16 by depth cells of
// 0.05 after smoothing passes, read.
static float* Smoothed(int32_t depth, int32_t passes)
{
    mvfxGasDef gas = Surface(16, depth, 0.05f, 0.1f);
    gas.surface.smoothing = passes;
    mvfxEmitterDef emitter = Emitter(4);
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    const double p[3] = {0.013, -0.021, depth > 1 ? 0.007 : 0.0};
    Spawn(p, 1);
    float* phi = Distance(16 * 16 * depth);
    mvfxDestroyContext(s_context);
    return phi;
}

// One smoothing pass as documented, in place: the mean of each cell and
// its neighbors along the axes (an edge's cell for the one beyond), then
// that mean again only where it is lower.
static void SmoothOnce(float* phi, int32_t depth)
{
    int32_t axes = depth > 1 ? 3 : 2;
    int64_t cells = 16 * 16 * (int64_t)depth;
    float* other = malloc(sizeof(float) * (size_t)cells);
    CHECK(other != NULL, "memory");
    for (int32_t half = 0; half < 2; ++half)
    {
        const float* from = half == 0 ? phi : other;
        float* to = half == 0 ? other : phi;
        for (int64_t i = 0; i < cells; ++i)
        {
            const int32_t at[3] = {(int32_t)(i % 16), (int32_t)(i / 16 % 16), (int32_t)(i / 256)};
            const int64_t step[3] = {1, 16, 256};
            float sum = from[i];
            for (int32_t k = 0; k < axes; ++k)
            {
                sum += from[at[k] > 0 ? i - step[k] : i];
                sum += from[at[k] + 1 < 16 ? i + step[k] : i];
            }
            float mean = sum / (float)(1 + 2 * axes);
            to[i] = half == 0 ? mean : fminf(mean, from[i]);
        }
    }
    free(other);
}

static void TestSmoothing(void)
{
    // Two passes over a lone particle's field, in 3D and 2D, are the
    // documented passes over the unsmoothed field, bit for bit.
    bool same = true;
    for (int32_t c = 0; c < 2; ++c)
    {
        int32_t depth = c == 0 ? 16 : 1;
        float* expected = Smoothed(depth, 0);
        SmoothOnce(expected, depth);
        SmoothOnce(expected, depth);
        float* phi = Smoothed(depth, 2);
        same = same && memcmp(expected, phi, sizeof(float) * 256u * (size_t)depth) == 0;
        free(expected);
        free(phi);
    }
    CHECK(same, "smoothing passes as documented");
}

static void TestTinyReach(void)
{
    // A radius so small its reach over a cell of a million rounds to 0: the
    // surface still takes buckets of a cell, and holds its reach.
    mvfxGasDef gas = Surface(8, 8, 1e6f, 1e-40f);
    mvfxEmitterDef emitter = Emitter(4);
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    const double p[3] = {0.0, 0.0, 0.0};
    Spawn(p, 1);
    float* phi = Distance(512);
    bool right = true;
    for (int32_t i = 0; i < 512; ++i)
    {
        right = right && phi[i] == 4.0f * 1e-40f;
    }
    free(phi);
    mvfxDestroyContext(s_context);
    CHECK(right, "a reach below a cell");
}

// The largest eigenvalue of a symmetric 3 by 3 matrix by Jacobi's
// rotations in binary64.
static double Reference(const float m[6])
{
    double e[6];
    for (int32_t k = 0; k < 6; ++k)
    {
        e[k] = (double)m[k];
    }
    double a[3][3] = {{e[0], e[3], e[4]}, {e[3], e[1], e[5]}, {e[4], e[5], e[2]}};
    for (int32_t sweep = 0; sweep < 30; ++sweep)
    {
        for (int32_t p = 0; p < 2; ++p)
        {
            for (int32_t q = p + 1; q < 3; ++q)
            {
                if (fabs(a[p][q]) < 1e-300)
                {
                    continue;
                }
                double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                double t = (theta >= 0.0 ? 1.0 : -1.0) / (fabs(theta) + sqrt(theta * theta + 1.0));
                double c = 1.0 / sqrt(t * t + 1.0);
                double s = t * c;
                for (int32_t k = 0; k < 3; ++k)
                {
                    double x = a[k][p];
                    double y = a[k][q];
                    a[k][p] = c * x - s * y;
                    a[k][q] = s * x + c * y;
                }
                for (int32_t k = 0; k < 3; ++k)
                {
                    double x = a[p][k];
                    double y = a[q][k];
                    a[p][k] = c * x - s * y;
                    a[q][k] = s * x + c * y;
                }
            }
        }
    }
    return fmax(fmax(a[0][0], a[1][1]), a[2][2]);
}

static void TestEigenvalue(void)
{
    // Random symmetric matrices of entries within 3, some with repeated
    // eigenvalues: at or above 0.4 within 1e-4 of the reference (relative
    // to 1 or its size); below, below 0.4 too. In 2D, exact to rounding.
    uint64_t state = 0x9E3779B97F4A7C15u;
    float worst = 0.0f;
    bool below = true;
    for (int32_t c = 0; c < 20000; ++c)
    {
        float m[6];
        for (int32_t e = 0; e < 6; ++e)
        {
            state = state * 6364136223846793005u + 1442695040888963407u;
            m[e] = (float)((double)(state >> 11) / 9007199254740992.0 * 6.0 - 3.0);
        }
        if (c % 4 == 1)
        {
            // A double eigenvalue: diag(u, u, w) turned about Z.
            m[1] = m[0];
            m[3] = 0.0f;
            m[4] = 0.0f;
            m[5] = 0.0f;
        }
        double reference = Reference(m);
        float found = mvfxSurfaceEigenvalue(m, 3, 0.4f);
        if (reference >= 0.4)
        {
            float scale = fmaxf(1.0f, fabsf((float)reference));
            worst = fmaxf(worst, fabsf(found - (float)reference) / scale);
        }
        else
        {
            below = below && found < 0.4f;
        }
    }
    const float flat[6] = {1.5f, -0.5f, 0.0f, 2.0f, 0.0f, 0.0f};
    double exact = 0.5 + sqrt(1.0 + 4.0);
    bool plane = fabs((double)mvfxSurfaceEigenvalue(flat, 2, 0.4f) - exact) <= 1e-6;
    CHECK(worst <= 1e-4f && below && plane, "the largest eigenvalue");
}

// A deterministic jitter in (-1, 1).
static double Jitter(uint64_t* state)
{
    *state = *state * 6364136223846793005u + 1442695040888963407u;
    return (double)(*state >> 11) / 4503599627370496.0 - 1.0;
}

// A block of particles 0.05 apart, 20 by 20 by 10, each jittered up to
// 0.15 spacings, its top face at Z 0; its count.
static int32_t JitteredBlock(double* places)
{
    int32_t count = 0;
    uint64_t state = 12345u;
    for (int32_t i = 0; i < 4000; ++i)
    {
        int32_t x = i % 20 - 10;
        int32_t y = i / 20 % 20 - 10;
        int32_t z = i / 400;
        const double at[3] = {(double)x + 0.5, (double)y + 0.5, -(double)z - 0.5};
        for (int32_t k = 0; k < 3; ++k)
        {
            places[3 * count + k] = (at[k] + 0.15 * Jitter(&state)) * 0.05;
        }
        ++count;
    }
    return count;
}

// Where the distance first rises through 0 along Z in column (x, y) of a
// box of 24 cells of 0.025 centered on the origin, in spacings of 0.05 (by
// linear interpolation); NaN for none.
static double FaceAt(const float* phi, int32_t x, int32_t y)
{
    for (int32_t z = 0; z + 1 < 24; ++z)
    {
        float a = phi[(z * 24 + y) * 24 + x];
        float b = phi[((z + 1) * 24 + y) * 24 + x];
        if (a <= 0.0f && b > 0.0f)
        {
            return ((double)z + 0.5 + (double)a / (double)(a - b) - 12.0) * 0.5;
        }
    }
    return (double)NAN;
}

static void TestFlatFace(void)
{
    // The jittered block filling a 3D box of 24 cells of 0.025 to its
    // sides and below Z 0: with radius 0.05 (the spacing) and the defaults,
    // without smoothing and with a pass, the zero of the distance along Z
    // in each of the 16 by 16 inner columns is within a tenth of a spacing
    // of 0 on average (0.058 and 0.067 inside, measured), and none strays
    // more than 0.15.
    mvfxGasDef gas = Surface(24, 24, 0.025f, 0.05f);
    static double places[3 * 4000];
    int32_t count = JitteredBlock(places);
    bool flat = true;
    for (int32_t pass = 0; pass < 2; ++pass)
    {
        gas.surface.smoothing = pass;
        mvfxEmitterDef emitter = Emitter(4000);
        CHECK(Open(&emitter, 1, &gas, 1), "opened");
        Spawn(places, count);
        float* phi = Distance(24 * 24 * 24);
        double mean = 0.0;
        double most = 0.0;
        for (int32_t c = 0; c < 256; ++c)
        {
            double at = FaceAt(phi, 4 + c % 16, 4 + c / 16);
            mean += at / 256.0;
            most = fmax(most, fabs(at));
            flat = flat && !isnan(at);
        }
        flat = flat && fabs(mean) <= 0.1 && most <= 0.15;
        free(phi);
        mvfxDestroyContext(s_context);
    }
    CHECK(flat, "a jittered block's flat face");
}

// The distance midway between two balls (disks for depth 1) of particles
// 0.05 apart and of radius 3 spacings, their lattices' edges gap spacings
// apart, in a box of 64 by 32 by depth cells of 0.025; correct sets the
// thresholds' order.
static float Between(double gap, int32_t depth, bool correct)
{
    mvfxGasDef gas = Surface(64, depth, 0.025f, 0.05f);
    gas.size[1] = 32;
    gas.surface.low = correct ? 0.4f : 2.5f;
    gas.surface.high = correct ? 2.5f : 0.4f;
    static double places[3 * 2048];
    int32_t count = 0;
    double c = 3.0 + 0.5 * gap;
    int32_t across = depth > 1 ? 4 : 0;
    for (int32_t i = 0; i < 25 * 9 * (2 * across + 1); ++i)
    {
        int32_t x = i % 25 - 12;
        int32_t y = i / 25 % 9 - 4;
        int32_t z = i / 225 - across;
        const double at[3] = {(double)x, (double)y, (double)z};
        double side = at[1] * at[1] + at[2] * at[2];
        if ((at[0] + c) * (at[0] + c) + side <= 9.0 || (at[0] - c) * (at[0] - c) + side <= 9.0)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                places[3 * count + k] = 0.05 * at[k];
            }
            ++count;
        }
    }
    mvfxEmitterDef emitter = Emitter(2048);
    CHECK(Open(&emitter, 1, &gas, 1), "opened");
    Spawn(places, count);
    const double middle[3] = {0.0, 0.0, 0.0};
    float out = 0.0f;
    CHECK(mvfxInstance_SampleGas(s_context, s_instance, 0, mvfx_gasDistance, middle, 1, &out) ==
              mvfx_success,
          "sampled");
    mvfxDestroyContext(s_context);
    return out;
}

static void TestDrops(void)
{
    // Two drops whose lattices' edges are 2 and 3 spacings apart (a
    // spacing and two between their cubes): with the defaults the middle
    // is outside both balls (above 0.005), and outside two disks 3 apart;
    // without the correction the balls 2 apart are bridged.
    float balls[2] = {Between(2.0, 32, true), Between(3.0, 32, true)};
    float disks = Between(3.0, 1, true);
    float joined = Between(2.0, 32, false);
    CHECK(balls[0] > 0.005f && balls[1] > 0.005f && disks > 0.005f && joined < 0.0f,
          "drops kept apart");
}

int main(void)
{
    TestRefusals();
    TestReads();
    TestLoneParticle();
    TestTinyReach();
    TestSmoothing();
    TestEigenvalue();
    TestFlatFace();
    TestDrops();
    return s_failures;
}
