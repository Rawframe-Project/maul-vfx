// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Liquids: the kernels' rest density against a lattice summed in binary64,
// 3D and planar; a lattice at rest left in place away from its edges; a
// column in a box under gravity settling, its density within its bound,
// its compression rising with depth, its height held once settled and no
// particle out of the box; a dam break's front against Martin and Moyce's
// measurements; the refusals, the instance's scale, the neighbor cap's
// count, XSPH, confinement, a 3D liquid, the tensile correction's size and
// a planar liquid's Z.

#include "instance.h"
#include "instance_api.h"
#include "liquid.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

#define MOST 1024

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// A planar liquid's emitter: host spawns only, long lives, no gravity.
static mvfxEmitterDef Emitter(float spacing)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = MOST;
    emitter.hostSpawnCapacity = MOST;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.liquid.enabled = true;
    emitter.liquid.planar = true;
    emitter.liquid.spacing = spacing;
    return emitter;
}

// Opens an instance of the emitter and spawns particles at places, still.
static void Open(const mvfxEmitterDef* emitter, const double* places, int32_t count)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxSpawnRequest request = {.count = count, .positions = places};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
    // The spawns are born in the first step, and join the liquid from the
    // next.
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "born");
}

// The emitter's places, by axis.
static float* Places(int32_t k)
{
    const mvfxInstance* live = mvfxContextInstance(s_context, s_instance);
    return mvfxAttributeArray(live, 0, (mvfxAttribute)(mvfx_attributePositionX + k));
}

static int32_t Count(void)
{
    return mvfxContextInstance(s_context, s_instance)->emitters[0].count;
}

static void TestKernel(void)
{
    // The poly6 sum over a lattice, summed again in binary64 from the
    // kernels' formulas, is the rest density within 1e-5; the tensile
    // correction's reference kernel is its inverse's; the radius is the
    // spacing times its scale.
    bool right = true;
    for (int32_t planar = 0; planar < 2; ++planar)
    {
        mvfxEmitterDef emitter = Emitter(0.05f);
        emitter.liquid.planar = planar == 1;
        emitter.liquid.kernelScale = 2.5f;
        mvfxLiquidKernel kernel = mvfxLiquidKernelOf(&emitter.liquid, 2.0f);
        double h = 0.25;
        double pi = 3.14159265358979323846;
        double factor = planar == 1 ? 4.0 / (pi * pow(h, 8)) : 315.0 / (64.0 * pi * pow(h, 9));
        double rest = 0.0;
        for (int32_t c = planar == 1 ? 0 : -3; c <= (planar == 1 ? 0 : 3); ++c)
        {
            for (int32_t b = -3; b <= 3; ++b)
            {
                for (int32_t a = -3; a <= 3; ++a)
                {
                    double square = 0.01 * (double)(a * a + b * b + c * c);
                    rest += square < h * h ? factor * pow(h * h - square, 3) : 0.0;
                }
            }
        }
        float reference = 0.2f * kernel.radius;
        right = right && fabsf(kernel.radius - 0.25f) <= 1e-6f &&
                fabs((double)kernel.restInverse * rest - 1.0) <= 1e-5 &&
                fabsf(mvfxLiquidPoly6(&kernel, reference * reference) * kernel.tensileInverse -
                      1.0f) <= 1e-6f &&
                kernel.softening > 0.0f;
    }
    CHECK(right, "the kernels' rest density");
}

static void TestRest(void)
{
    // A square lattice of 24 by 24 at the rest spacing, one substep and one
    // iteration, no gravity: particles 6 spacings or more from its edges
    // move less than 1e-4 of the spacing in a step.
    static double places[24 * 24 * 3];
    for (int32_t i = 0; i < 24 * 24; ++i)
    {
        places[3 * i] = 0.05 * (double)(i % 24);
        places[3 * i + 1] = 0.05 * (double)(i / 24);
        places[3 * i + 2] = 0.0;
    }
    mvfxEmitterDef emitter = Emitter(0.05f);
    emitter.liquid.substeps = 1;
    emitter.liquid.iterations = 1;
    Open(&emitter, places, 24 * 24);
    CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success, "step");
    float most = 0.0f;
    int32_t inner = 0;
    for (int32_t i = 0; i < Count(); ++i)
    {
        int32_t x = i % 24;
        int32_t y = i / 24;
        bool deep = x >= 6 && x < 18 && y >= 6 && y < 18;
        float dx = Places(0)[i] - (float)places[3 * i];
        float dy = Places(1)[i] - (float)places[3 * i + 1];
        most = deep ? fmaxf(most, sqrtf(dx * dx + dy * dy)) : most;
        inner += deep ? 1 : 0;
    }
    CHECK(Count() == 24 * 24 && inner == 144 && most < 1e-4f * 0.05f, "a lattice at rest stays");
    mvfxDestroyContext(s_context);
}

// A particle's density over the rest density at the emitter's places, in
// binary64 over every particle.
static double DensityAt(const mvfxLiquidKernel* kernel, int32_t i)
{
    double sum = 0.0;
    for (int32_t j = 0; j < Count(); ++j)
    {
        double dx = (double)Places(0)[i] - (double)Places(0)[j];
        double dy = (double)Places(1)[i] - (double)Places(1)[j];
        sum += (double)mvfxLiquidPoly6(kernel, (float)(dx * dx + dy * dy));
    }
    return sum * (double)kernel->restInverse;
}

// Steps of 1/60 s.
static void Steps(int32_t n)
{
    for (int32_t s = 0; s < n; ++s)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success, "step");
    }
}

// The column's top, and whether every particle is in the box 0.6 wide.
static float Top(bool* inside)
{
    float top = 0.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        top = fmaxf(top, Places(1)[i]);
        *inside =
            *inside && Places(0)[i] >= -1e-4f && Places(0)[i] <= 0.6001f && Places(1)[i] >= -1e-4f;
    }
    return top;
}

// The mean speed of the emitter's particles.
static float MeanSpeed(void)
{
    const mvfxInstance* live = mvfxContextInstance(s_context, s_instance);
    const float* vx = mvfxAttributeArray(live, 0, mvfx_attributeVelocityX);
    const float* vy = mvfxAttributeArray(live, 0, mvfx_attributeVelocityY);
    double sum = 0.0;
    for (int32_t i = 0; i < Count(); ++i)
    {
        sum += sqrt((double)vx[i] * (double)vx[i] + (double)vy[i] * (double)vy[i]);
    }
    return (float)(sum / (double)Count());
}

static void TestColumn(void)
{
    // A column 12 wide and 24 tall at spacing 0.05 in a box 0.6 wide kept
    // inside, gravity 9.81 down, the defaults: no particle ever leaves the
    // box; settled after 2 s, its top moves by at most 3% over 2 s more and
    // its mean speed is below 0.3; the inner particles' density (those a
    // kernel radius from the surface and the walls) is within 3% of 1; the
    // bottom third's is above the top third's.
    static double places[12 * 24 * 3];
    for (int32_t i = 0; i < 12 * 24; ++i)
    {
        places[3 * i] = 0.025 + 0.05 * (double)(i % 12);
        places[3 * i + 1] = 0.025 + 0.05 * (double)(i / 12);
        places[3 * i + 2] = 0.0;
    }
    const mvfxCollider box = {.kind = mvfx_colliderBox,
                              .inside = true,
                              .center = {0.3f, 1.0f, 0.0f},
                              .halfExtents = {0.3f, 1.0f, 1.0f},
                              .rotation = {0, 0, 0, 1}};
    mvfxEmitterDef emitter = Emitter(0.05f);
    emitter.motion.gravity[1] = -9.81f;
    emitter.collision.enabled = true;
    emitter.colliders = &box;
    emitter.colliderCount = 1;
    Open(&emitter, places, 12 * 24);
    bool inside = true;
    Steps(120);
    float settled = Top(&inside);
    Steps(120);
    float top = Top(&inside);
    float speed = MeanSpeed();
    mvfxLiquidKernel kernel = mvfxLiquidKernelOf(&emitter.liquid, 1.0f);
    double worst = 0.0;
    double sums[2] = {0.0, 0.0};
    int32_t counts[2] = {0, 0};
    for (int32_t i = 0; i < Count(); ++i)
    {
        float x = Places(0)[i];
        float y = Places(1)[i];
        bool inner = y < top - kernel.radius && x > kernel.radius && x < 0.6f - kernel.radius &&
                     y > kernel.radius;
        double density = DensityAt(&kernel, i);
        worst = inner ? fmax(worst, fabs(density - 1.0)) : worst;
        int32_t third = y < top / 3.0f ? 0 : (y > 2.0f * top / 3.0f ? 1 : -1);
        if (inner && third >= 0)
        {
            sums[third] += density;
            counts[third] += 1;
        }
    }
    bool deeper =
        counts[0] > 0 && counts[1] > 0 && sums[0] / (double)counts[0] > sums[1] / (double)counts[1];
    CHECK(Count() == 12 * 24 && inside, "a column stays in its box");
    CHECK(worst <= 0.03, "a column's density within its bound");
    CHECK(deeper, "a column compressed by its depth");
    CHECK(fabsf(top - settled) <= 0.03f * settled && speed < 0.3f, "a column settles");
    mvfxDestroyContext(s_context);
}

// The surge front of Martin and Moyce's square column (1952, part IV,
// table 1, n^2 = 1, the mean of the runs with a = 2.25 in): Z = z / a
// against T = t (g / a)^(1/2), their times set so that Z is 1.44 at T 0.80.
static const double s_damZ[] = {1.67, 1.89, 2.11, 2.33, 2.56, 2.78, 3.00, 3.22,
                                3.44, 3.67, 3.89, 4.11, 4.33, 4.56, 4.78, 5.00};
static const double s_damT[] = {0.97, 1.14, 1.29, 1.45, 1.62, 1.76, 1.93, 2.07,
                                2.24, 2.40, 2.54, 2.71, 2.87, 3.04, 3.21, 3.32};

// The liquid's front over a: the farthest particle with at least two
// others within 1.5 spacings (a lone particle ahead is spray, not the
// front), plus half a spacing.
static double Front(double a, double spacing)
{
    float front = 0.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        int32_t near = 0;
        for (int32_t j = 0; j < Count(); ++j)
        {
            float dx = Places(0)[i] - Places(0)[j];
            float dy = Places(1)[i] - Places(1)[j];
            near += j != i && dx * dx + dy * dy < (float)(2.25 * spacing * spacing) ? 1 : 0;
        }
        front = near >= 2 ? fmaxf(front, Places(0)[i]) : front;
    }
    return ((double)front + 0.5 * spacing) / a;
}

// Z at time T on a stepped record, linear between steps; -1 outside it.
static double ZAt(const double* times, const double* zs, int32_t count, double time)
{
    for (int32_t k = 1; k < count; ++k)
    {
        if (times[k - 1] <= time && times[k] >= time)
        {
            double share = (time - times[k - 1]) / (times[k] - times[k - 1]);
            return zs[k - 1] + share * (zs[k] - zs[k - 1]);
        }
    }
    return -1.0;
}

static void TestDamBreak(void)
{
    // A square column of side a = 0.6, 24 particles a side, in the corner
    // of a box 4.8 long, the defaults, stepped at 120 a second: its front,
    // timed as Martin and Moyce's were, is within 10% of theirs from Z
    // 1.67 to 5 (it runs 1% to 8% ahead past Z 3, its floor without
    // friction).
    const double a = 0.6;
    const double spacing = a / 24.0;
    static double places[24 * 24 * 3];
    for (int32_t i = 0; i < 24 * 24; ++i)
    {
        places[3 * i] = spacing * (0.5 + (double)(i % 24));
        places[3 * i + 1] = spacing * (0.5 + (double)(i / 24));
        places[3 * i + 2] = 0.0;
    }
    const mvfxCollider box = {.kind = mvfx_colliderBox,
                              .inside = true,
                              .center = {2.4f, 1.0f, 0.0f},
                              .halfExtents = {2.4f, 1.0f, 1.0f},
                              .rotation = {0, 0, 0, 1}};
    mvfxEmitterDef emitter = Emitter((float)spacing);
    emitter.motion.gravity[1] = -9.81f;
    emitter.collision.enabled = true;
    emitter.colliders = &box;
    emitter.colliderCount = 1;
    Open(&emitter, places, 24 * 24);
    static double times[128];
    static double zs[128];
    double unit = sqrt(9.81 / a);
    for (int32_t s = 0; s < 128; ++s)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 120) == mvfx_success,
              "step");
        times[s] = (double)(s + 1) / 120.0 * unit;
        zs[s] = Front(a, spacing);
    }
    // Their clock: T at Z 1.44 is 0.80.
    double shift = 0.0;
    for (int32_t k = 1; k < 128; ++k)
    {
        if (zs[k - 1] < 1.44 && zs[k] >= 1.44)
        {
            double share = (1.44 - zs[k - 1]) / (zs[k] - zs[k - 1]);
            shift = 0.80 - (times[k - 1] + share * (times[k] - times[k - 1]));
            break;
        }
    }
    double worst = 0.0;
    for (int32_t m = 0; m < 16; ++m)
    {
        double z = ZAt(times, zs, 128, s_damT[m] - shift);
        worst = fmax(worst, fabs(z - s_damZ[m]) / s_damZ[m]);
    }
    CHECK(shift != 0.0 && worst <= 0.10, "a dam break's front as Martin and Moyce measured");
    mvfxDestroyContext(s_context);
}

// Opens a liquid's emitter in an instance of a scale, its particles
// spawned at places, still; false when the effect is refused.
static bool OpenScaled(const mvfxEmitterDef* emitter, const double* places, int32_t count,
                       float scale)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(s_context, &def, &effect) != mvfx_success)
    {
        return false;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.scale = scale;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxSpawnRequest request = {.count = count, .positions = places};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "born");
    return true;
}

static void TestRefusals(void)
{
    // Each parameter just out of its range, and a liquid of units, of
    // rigid bodies or with a beam, is refused; the defaults are taken.
    bool right = true;
    for (int32_t c = 0; c < 17; ++c)
    {
        mvfxEmitterDef e = Emitter(0.05f);
        mvfxLiquidDef* l = &e.liquid;
        l->spacing = c == 0 ? 0.0f : (c == 1 ? MVFX_MAX_NEIGHBOR_RADIUS / 2.0f * 1.01f : 0.05f);
        l->kernelScale = c == 2 ? 1.49f : (c == 3 ? 4.01f : 2.0f);
        l->substeps = c == 4 ? 0 : (c == 5 ? MVFX_MAX_SUBSTEPS + 1 : 4);
        l->iterations = c == 6 ? 0 : (c == 7 ? MVFX_MAX_LIQUID_ITERATIONS + 1 : 3);
        l->relaxation = c == 8 ? 0.0f : (c == 9 ? 1.01f : 0.01f);
        l->tensile = c == 10 ? 1.01f : 0.0f;
        l->tensileDistance = c == 11 ? 1.0f : 0.2f;
        l->tensilePower = c == 12 ? 0 : 4;
        l->viscosity = c == 13 ? -0.01f : 0.01f;
        l->vorticity = c == 14 ? -1.0f : 0.0f;
        l->maxNeighbors = c == 15 ? MVFX_MAX_NEIGHBORS + 1 : 64;
        e.rigid.enabled = c == 16;
        const double place[3] = {0, 0, 0};
        bool refused = !OpenScaled(&e, place, 1, 1.0f);
        CHECK(refused, "a liquid's refusal");
        right = right && refused;
        mvfxDestroyContext(s_context);
    }
    // A valid chain, taken alone and refused as a liquid.
    mvfxEmitterDef chain = Emitter(0.05f);
    chain.capacity = 16;
    chain.hostSpawnCapacity = 4;
    chain.unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                               .points = 4,
                               .spacing = 0.5f,
                               .direction = {0, -1, 0},
                               .substeps = 8};
    const double place[3] = {0, 0, 0};
    bool refused = !OpenScaled(&chain, place, 1, 1.0f);
    right = right && refused;
    mvfxDestroyContext(s_context);
    chain.liquid.enabled = false;
    bool taken = OpenScaled(&chain, place, 1, 1.0f);
    right = right && taken;
    mvfxDestroyContext(s_context);
    mvfxEmitterDef liquid = Emitter(0.05f);
    bool defaults = OpenScaled(&liquid, place, 1, 1.0f);
    right = right && defaults;
    mvfxDestroyContext(s_context);
    CHECK(right, "a liquid's refusals");
}

static void TestScale(void)
{
    // A lattice at spacing 0.05 in the world, in an instance of scale 2 (a
    // rest spacing of 0.1 there), is compressed and spreads; at scale 1 it
    // stays.
    static double places[8 * 8 * 3];
    for (int32_t i = 0; i < 64; ++i)
    {
        places[3 * i] = 0.05 * (double)(i % 8);
        places[3 * i + 1] = 0.05 * (double)(i / 8);
        places[3 * i + 2] = 0.0;
    }
    float spread[2];
    for (int32_t c = 0; c < 2; ++c)
    {
        mvfxEmitterDef emitter = Emitter(0.05f);
        CHECK(OpenScaled(&emitter, places, 64, c == 0 ? 1.0f : 2.0f), "opened");
        Steps(1);
        spread[c] = 0.0f;
        for (int32_t i = 0; i < Count(); ++i)
        {
            spread[c] = fmaxf(spread[c], fabsf(Places(0)[i] - (float)places[3 * i]));
        }
        mvfxDestroyContext(s_context);
    }
    CHECK(spread[0] < 1e-6f && spread[1] > 0.01f, "a liquid's lengths scale with its instance");
}

static void TestCapped(void)
{
    // Most neighbors 3 in a lattice at rest: the particles with more
    // within the kernel radius, by the library's own float test at their
    // places, are counted once a step, the same each step.
    static double places[6 * 6 * 3];
    for (int32_t i = 0; i < 36; ++i)
    {
        places[3 * i] = 0.05 * (double)(i % 6);
        places[3 * i + 1] = 0.05 * (double)(i / 6);
        places[3 * i + 2] = 0.0;
    }
    mvfxEmitterDef emitter = Emitter(0.05f);
    emitter.liquid.maxNeighbors = 3;
    CHECK(OpenScaled(&emitter, places, 36, 1.0f), "opened");
    mvfxEmitterStats stats[2];
    for (int32_t s = 0; s < 2; ++s)
    {
        Steps(1);
        CHECK(mvfxInstance_GetEmitterStats(s_context, s_instance, 0, &stats[s]) == mvfx_success,
              "stats");
    }
    mvfxLiquidKernel kernel = mvfxLiquidKernelOf(&emitter.liquid, 1.0f);
    uint64_t expected = 0;
    for (int32_t i = 0; i < Count(); ++i)
    {
        int32_t near = 0;
        for (int32_t j = 0; j < Count(); ++j)
        {
            float square = 0.0f;
            for (int32_t k = 0; k < 3; ++k)
            {
                float d = Places(k)[i] - Places(k)[j];
                square += d * d;
            }
            near += j != i && square < kernel.square ? 1 : 0;
        }
        expected += near > 3 ? 1u : 0u;
    }
    CHECK(expected > 0 && stats[0].neighborsCapped == expected &&
              stats[1].neighborsCapped == 2 * expected,
          "a liquid's neighbors capped and counted");
    mvfxDestroyContext(s_context);
}

static void TestViscosity(void)
{
    // Two particles 0.06 apart along X (below rest density, so no
    // correction), moving 1 and -1 along Y, one substep, viscosity 0.5:
    // each velocity is its own plus 0.5 of the difference times the poly6
    // kernel over the rest density.
    const double places[6] = {0, 0, 0, 0.06, 0, 0};
    const float velocities[6] = {0, 1, 0, 0, -1, 0};
    mvfxEmitterDef emitter = Emitter(0.05f);
    emitter.liquid.substeps = 1;
    emitter.liquid.viscosity = 0.5f;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxSpawnRequest request = {.count = 2, .positions = places, .velocities = velocities};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "born");
    const mvfxInstance* live = mvfxContextInstance(s_context, s_instance);
    const float* vy = mvfxAttributeArray(live, 0, mvfx_attributeVelocityY);
    float before[2] = {vy[0], vy[1]};
    float dx = Places(0)[0] - Places(0)[1];
    float dy = Places(1)[0] - Places(1)[1];
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "step");
    mvfxLiquidKernel kernel = mvfxLiquidKernelOf(&emitter.liquid, 1.0f);
    // The tick moves them by a hair; the kernel is read at their places
    // then.
    dx = Places(0)[0] - Places(0)[1];
    dy = Places(1)[0] - Places(1)[1];
    float w = mvfxLiquidPoly6(&kernel, dx * dx + dy * dy) * kernel.restInverse;
    float expected0 = before[0] + 0.5f * ((before[1] - before[0]) * w);
    float expected1 = before[1] + 0.5f * ((before[0] - before[1]) * w);
    CHECK(fabsf(vy[0] - expected0) <= 1e-5f && fabsf(vy[1] - expected1) <= 1e-5f && w > 0.0f &&
              fabsf(vy[0] + vy[1]) <= 1e-6f,
          "XSPH viscosity blends velocities");
    mvfxDestroyContext(s_context);
}

// The kinetic energy (per unit mass) of the emitter's particles.
static double Energy(void)
{
    const mvfxInstance* live = mvfxContextInstance(s_context, s_instance);
    const float* vx = mvfxAttributeArray(live, 0, mvfx_attributeVelocityX);
    const float* vy = mvfxAttributeArray(live, 0, mvfx_attributeVelocityY);
    double sum = 0.0;
    for (int32_t i = 0; i < Count(); ++i)
    {
        sum += 0.5 * ((double)vx[i] * (double)vx[i] + (double)vy[i] * (double)vy[i]);
    }
    return sum;
}

static void TestVorticity(void)
{
    // A disk of radius 0.3 in a square lattice turning at 2 a second, no
    // gravity, no viscosity: after 30 steps it keeps more of its energy
    // with confinement (5 a second) than without.
    static double places[13 * 13 * 3];
    static float velocities[13 * 13 * 3];
    int32_t count = 0;
    for (int32_t i = 0; i < 13 * 13; ++i)
    {
        double x = 0.05 * (double)(i % 13 - 6);
        double y = 0.05 * (double)(i / 13 - 6);
        if (x * x + y * y <= 0.09 + 1e-9)
        {
            places[3 * count] = x;
            places[3 * count + 1] = y;
            places[3 * count + 2] = 0.0;
            velocities[3 * count] = (float)(-2.0 * y);
            velocities[3 * count + 1] = (float)(2.0 * x);
            velocities[3 * count + 2] = 0.0f;
            count += 1;
        }
    }
    double energy[2];
    for (int32_t c = 0; c < 2; ++c)
    {
        mvfxEmitterDef emitter = Emitter(0.05f);
        emitter.liquid.viscosity = 0.0f;
        emitter.liquid.vorticity = c == 0 ? 0.0f : 5.0f;
        mvfxContextDef contextDef = mvfxDefaultContextDef();
        CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.emitters = &emitter;
        def.emitterCount = 1;
        mvfxEffectId effect = {0};
        CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = effect;
        CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
        mvfxSpawnRequest request = {.count = count, .positions = places, .velocities = velocities};
        CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
        Steps(30);
        energy[c] = Energy();
        mvfxDestroyContext(s_context);
    }
    CHECK(count > 100 && energy[1] > energy[0], "vorticity confinement keeps a swirl");
}

static void TestDeep(void)
{
    // A 3D cube of 8 particles a side at spacing 0.05 in a box 0.4 wide and
    // deep, gravity 9.81 down, 1.5 s: it stays in the box; its inner
    // particles' density (summed in binary64 over all, 3D kernels) is at
    // most 3% above 1 (the constraint bounds compression only) and at
    // least 0.9 on average.
    static double places[512 * 3];
    for (int32_t i = 0; i < 512; ++i)
    {
        places[3 * i] = 0.025 + 0.05 * (double)(i % 8);
        places[3 * i + 1] = 0.025 + 0.05 * (double)(i / 64);
        places[3 * i + 2] = 0.025 + 0.05 * (double)(i / 8 % 8);
    }
    const mvfxCollider box = {.kind = mvfx_colliderBox,
                              .inside = true,
                              .center = {0.2f, 1.0f, 0.2f},
                              .halfExtents = {0.2f, 1.0f, 0.2f},
                              .rotation = {0, 0, 0, 1}};
    mvfxEmitterDef emitter = Emitter(0.05f);
    emitter.liquid.planar = false;
    emitter.motion.gravity[1] = -9.81f;
    emitter.collision.enabled = true;
    emitter.colliders = &box;
    emitter.colliderCount = 1;
    Open(&emitter, places, 512);
    Steps(90);
    mvfxLiquidKernel kernel = mvfxLiquidKernelOf(&emitter.liquid, 1.0f);
    bool inside = true;
    float top = 0.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        top = fmaxf(top, Places(1)[i]);
        for (int32_t k = 0; k < 3; ++k)
        {
            float low = k == 1 ? -1e-4f : -1e-4f;
            float high = k == 1 ? 2.0001f : 0.4001f;
            inside = inside && Places(k)[i] >= low && Places(k)[i] <= high;
        }
    }
    double worst = 0.0;
    double mean = 0.0;
    int32_t inner = 0;
    for (int32_t i = 0; i < Count(); ++i)
    {
        bool deep = Places(1)[i] < top - kernel.radius && Places(1)[i] > kernel.radius;
        for (int32_t k = 0; k < 3; k += 2)
        {
            deep = deep && Places(k)[i] > kernel.radius && Places(k)[i] < 0.4f - kernel.radius;
        }
        if (!deep)
        {
            continue;
        }
        double sum = 0.0;
        for (int32_t j = 0; j < Count(); ++j)
        {
            double square = 0.0;
            for (int32_t k = 0; k < 3; ++k)
            {
                double d = (double)Places(k)[i] - (double)Places(k)[j];
                square += d * d;
            }
            sum += (double)mvfxLiquidPoly6(&kernel, (float)square);
        }
        worst = fmax(worst, sum * (double)kernel.restInverse - 1.0);
        mean += sum * (double)kernel.restInverse;
        inner += 1;
    }
    CHECK(Count() == 512 && inside && inner > 0 && worst <= 0.03 && mean / (double)inner >= 0.9,
          "a 3D liquid settles");
    mvfxDestroyContext(s_context);
}

static void TestTensile(void)
{
    // Two particles 0.01 apart (below rest density, so their multipliers
    // are 0), tensile 0.1 of power 4 at 0.2 of the radius, one substep of
    // one iteration: each moves away from the other by the tensile
    // correction times the gradient over the rest density.
    const double places[6] = {0, 0, 0, 0.01, 0, 0};
    mvfxEmitterDef emitter = Emitter(0.05f);
    emitter.liquid.substeps = 1;
    emitter.liquid.iterations = 1;
    emitter.liquid.viscosity = 0.0f;
    emitter.liquid.tensile = 0.1f;
    CHECK(OpenScaled(&emitter, places, 2, 1.0f), "opened");
    const float before[2] = {Places(0)[0], Places(0)[1]};
    Steps(1);
    mvfxLiquidKernel kernel = mvfxLiquidKernelOf(&emitter.liquid, 1.0f);
    const float offset[3] = {before[0] - before[1], 0.0f, 0.0f};
    float length = fabsf(offset[0]);
    float g[3];
    mvfxLiquidSpiky(&kernel, offset, length, g);
    float ratio = mvfxLiquidPoly6(&kernel, length * length) * kernel.tensileInverse;
    float tensile = -kernel.tensileScale * ratio * ratio * ratio * ratio;
    float expected = before[0] + tensile * g[0] * kernel.restInverse;
    float moved = Places(0)[0];
    CHECK(fabsf(moved - expected) <= 1e-7f && moved < before[0] &&
              fabsf((Places(0)[1] - before[1]) + (moved - before[0])) <= 1e-7f,
          "the tensile correction parts a close pair");
    mvfxDestroyContext(s_context);
}

static void TestPlanar(void)
{
    // A planar lattice at 0.04, closer than its rest spacing of 0.05, its
    // particles at Z 0 and 0.01 by turns: a step parts them along X and Y
    // and leaves every Z as it was.
    static double places[8 * 8 * 3];
    for (int32_t i = 0; i < 64; ++i)
    {
        places[3 * i] = 0.04 * (double)(i % 8);
        places[3 * i + 1] = 0.04 * (double)(i / 8);
        places[3 * i + 2] = (i + i / 8) % 2 == 0 ? 0.0 : 0.01;
    }
    mvfxEmitterDef emitter = Emitter(0.05f);
    CHECK(OpenScaled(&emitter, places, 64, 1.0f), "opened");
    Steps(1);
    bool kept = true;
    float spread = 0.0f;
    for (int32_t i = 0; i < Count(); ++i)
    {
        kept = kept && Places(2)[i] == (float)places[3 * i + 2];
        spread = fmaxf(spread, fabsf(Places(0)[i] - (float)places[3 * i]));
    }
    CHECK(kept && spread > 1e-3f, "a planar liquid keeps its Z");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestKernel();
    TestRefusals();
    TestScale();
    TestCapped();
    TestViscosity();
    TestVorticity();
    TestDeep();
    TestTensile();
    TestPlanar();
    TestRest();
    TestColumn();
    TestDamBreak();
    return s_failures;
}
