// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Liquid flows: a column in a box under gravity settling, its density
// within its bound, its compression rising with depth, its height held
// once settled and no particle out of the box; a dam break's front against
// Martin and Moyce's measurements. Apart from the other liquid tests so
// each test fits well inside the suite's timeout under sanitizers.

#include "liquid.h"
#include "liquid_scene.h"

#include <math.h>
#include <string.h>

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

int main(void)
{
    TestColumn();
    TestDamBreak();
    return s_failures;
}
