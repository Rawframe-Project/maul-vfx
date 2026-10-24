// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Births by a rate curve, read from their birth events: each comes where
// the integral of the rate, as baked (MVFX_CURVE_SAMPLES samples over a
// loop, linear between them), reaches a whole number, rounded up to a
// tick. Checked against the integral from the keys by curve.h's rules: a
// constant rate, a ramp (a quadratic integral), plateaus of 0 (one whose
// integral is whole, first reached where it begins), a rate that ends at
// 0, and loops whose integral is not whole; the same ticks however the
// time is split into steps.

#include "test_harness.h"

#include "maul-vfx/instance.h"

#include <math.h>

#define MOST_BIRTHS 4096

// The oracle. A rate curve's value from its keys in binary64 (linear,
// constant and given cubic segments; see test_curve.c for the tangent
// rules), baked as an effect bakes it: sample i at i / 255 of the loop,
// rounded once, times the rate, rounded again.

static double ValueAt(const mvfxCurveKey* k, int32_t count, double t)
{
    int32_t last = count - 1;
    if (t <= (double)k[0].time)
    {
        return (double)k[0].value;
    }
    if (t >= (double)k[last].time)
    {
        return (double)k[last].value;
    }
    int32_t i = 0;
    while ((double)k[i + 1].time <= t)
    {
        ++i;
    }
    double span = (double)k[i + 1].time - (double)k[i].time;
    double u = (t - (double)k[i].time) / span;
    double a = (double)k[i].value;
    double b = (double)k[i + 1].value;
    switch (k[i].interpolation)
    {
    case mvfx_interpolationConstant:
        return a;
    case mvfx_interpolationLinear:
        return a + (b - a) * u;
    default:
        return (2 * u * u * u - 3 * u * u + 1) * a +
               (u * u * u - 2 * u * u + u) * span * (double)k[i].outTangent +
               (-2 * u * u * u + 3 * u * u) * b +
               (u * u * u - u * u) * span * (double)k[i + 1].inTangent;
    }
}

typedef struct Rate
{
    double samples[MVFX_CURVE_SAMPLES];
    // The integral over a loop's normalized age, from 0 to each sample.
    double integral[MVFX_CURVE_SAMPLES];
} Rate;

static Rate BakeRate(const mvfxCurveKey* keys, int32_t count, float rate)
{
    Rate r = {0};
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        float sample = (float)ValueAt(keys, count, (double)i / (double)(MVFX_CURVE_SAMPLES - 1));
        sample *= rate;
        r.samples[i] = (double)sample;
    }
    // The trapezoids' heights summed, then divided once, so a whole
    // integral stays whole.
    double heights = 0.0;
    for (int32_t i = 1; i < MVFX_CURVE_SAMPLES; ++i)
    {
        heights += (r.samples[i - 1] + r.samples[i]) / 2.0;
        r.integral[i] = heights / (double)(MVFX_CURVE_SAMPLES - 1);
    }
    return r;
}

// The integral of the baked rate from 0 to normalized age u in [0, 1]:
// whole intervals, then the part of one under its line.
static double IntegralAt(const Rate* r, double u)
{
    double h = 1.0 / (double)(MVFX_CURVE_SAMPLES - 1);
    int32_t i = (int32_t)(u / h);
    i = i > MVFX_CURVE_SAMPLES - 2 ? MVFX_CURVE_SAMPLES - 2 : i;
    double x = u - (double)i * h;
    double slope = (r->samples[i + 1] - r->samples[i]) / h;
    return r->integral[i] + r->samples[i] * x + slope * x * x / 2.0;
}

// The first normalized age at which the integral reaches target (above
// 0, at most the loop's), by bisection: the integral never falls.
static double AgeAt(const Rate* r, double target)
{
    double low = 0.0;
    double high = 1.0;
    for (int32_t k = 0; k < 200 && high - low > 0x1p-60; ++k)
    {
        double middle = (low + high) / 2.0;
        if (IntegralAt(r, middle) >= target)
        {
            high = middle;
        }
        else
        {
            low = middle;
        }
    }
    return high;
}

// The ticks of the births of an emitter of this rate over loops of
// `seconds`, at most `count`, by the oracle: birth j where the integral
// over the loops reaches j + 1, which carries what one loop leaves past a
// whole number into the next; never before the loop's previous birth.
static int32_t Expected(const Rate* r, double seconds, int32_t loops, int64_t until,
                        int64_t* ticksOut, int32_t most)
{
    int64_t loopTicks = (int64_t)(seconds * (double)MVFX_TICKS_PER_SECOND);
    double perLoop = seconds * r->integral[MVFX_CURVE_SAMPLES - 1];
    int32_t count = 0;
    int64_t loop = 0;
    int64_t last = 0;
    for (int64_t j = 0; count < most && loop < loops;)
    {
        double target = ((double)j + 1.0 - (double)loop * perLoop) / seconds;
        int64_t tick = loopTicks + 1;
        if (target <= r->integral[MVFX_CURVE_SAMPLES - 1])
        {
            tick = (int64_t)ceil(AgeAt(r, target) * (double)loopTicks);
            tick = tick < last ? last : tick;
        }
        if (tick > loopTicks)
        {
            loop += 1;
            last = 0;
            continue;
        }
        if (loop * loopTicks + tick > until)
        {
            break;
        }
        ticksOut[count++] = loop * loopTicks + tick;
        last = tick;
        j += 1;
    }
    return count;
}

// The births of an emitter of this rate curve, read from its birth events
// over `until` ticks in steps of `step` ticks.
static int32_t Births(const mvfxCurveKey* keys, int32_t count, float rate, float seconds,
                      int32_t loops, int64_t until, int64_t step, int64_t* ticksOut)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = MOST_BIRTHS;
    emitter.rate = rate;
    emitter.rateCurve = (mvfxCurve){keys, count, mvfx_tangentsGiven};
    emitter.duration = seconds;
    emitter.loops = loops;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.events = mvfx_eventMaskBirth;
    emitter.eventCapacity = MOST_BIRTHS;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    int32_t births = 0;
    for (int64_t done = 0; done < until;)
    {
        int64_t ticks = until - done < step ? until - done : step;
        CHECK(mvfxContext_StepTicks(context, ticks) == mvfx_success, "stepped");
        done += ticks;
        const mvfxEvent* events = NULL;
        int32_t n = 0;
        CHECK(mvfxInstance_GetEvents(context, instance, 0, &events, &n) == mvfx_success, "events");
        for (int32_t e = 0; e < n && births < MOST_BIRTHS; ++e)
        {
            ticksOut[births++] = events[e].tick;
        }
    }
    mvfxDestroyContext(context);
    return births;
}

// Whether an emitter's births are the oracle's, to a tick (where the
// oracle's integral and the library's round apart at a whole tick), in
// steps of a sixty-fourth of a second and of an odd count of ticks.
static bool BirthsAre(const mvfxCurveKey* keys, int32_t count, float rate, float seconds,
                      int32_t loops, int64_t until, int32_t* countOut)
{
    static int64_t expected[MOST_BIRTHS];
    static int64_t got[MOST_BIRTHS];
    static int64_t split[MOST_BIRTHS];
    Rate r = BakeRate(keys, count, rate);
    int32_t n = Expected(&r, (double)seconds, loops, until, expected, MOST_BIRTHS);
    int32_t m = Births(keys, count, rate, seconds, loops, until, MVFX_TICKS_PER_SECOND / 64, got);
    int32_t s = Births(keys, count, rate, seconds, loops, until, 99991, split);
    bool same = n == m && m == s;
    for (int32_t i = 0; same && i < n; ++i)
    {
        same = got[i] - expected[i] <= 1 && expected[i] - got[i] <= 1 && got[i] == split[i];
    }
    *countOut = m;
    return same;
}

static void TestConstantRate(void)
{
    // 30 a second over a loop of 4 seconds: birth j at (j + 1) / 30 s.
    mvfxCurveKey keys[2] = {{0.0f, 1.0f, 0, 0, mvfx_interpolationLinear},
                            {1.0f, 1.0f, 0, 0, mvfx_interpolationLinear}};
    int32_t count = 0;
    CHECK(BirthsAre(keys, 2, 30.0f, 4.0f, 1, 5 * MVFX_TICKS_PER_SECOND, &count), "as integrated");
    CHECK(count == 120, "120 in the loop, none after");
    int64_t ticks[MOST_BIRTHS];
    Births(keys, 2, 30.0f, 4.0f, 1, 5 * MVFX_TICKS_PER_SECOND, 16384, ticks);
    bool exact = true;
    for (int32_t j = 0; j < 120; ++j)
    {
        exact = exact && ticks[j] == (int64_t)ceil((double)(j + 1) / 30.0 * MVFX_TICKS_PER_SECOND);
    }
    CHECK(exact, "a constant rate's births");
}

static void TestRampIsQuadratic(void)
{
    // The rate rises from 0 to 100 over a loop of two seconds: the integral
    // is 25 t^2, birth j at the root of (j + 1) / 25.
    mvfxCurveKey keys[2] = {{0.0f, 0.0f, 0, 0, mvfx_interpolationLinear},
                            {1.0f, 1.0f, 0, 0, mvfx_interpolationLinear}};
    int32_t count = 0;
    CHECK(BirthsAre(keys, 2, 100.0f, 2.0f, 1, 3 * MVFX_TICKS_PER_SECOND, &count), "as integrated");
    CHECK(count == 100, "100 in the loop");
    int64_t ticks[MOST_BIRTHS];
    Births(keys, 2, 100.0f, 2.0f, 1, 3 * MVFX_TICKS_PER_SECOND, 16384, ticks);
    bool quadratic = true;
    for (int32_t j = 0; j < 100; ++j)
    {
        double seconds = sqrt((double)(j + 1) / 25.0);
        quadratic = quadratic && fabs((double)ticks[j] / MVFX_TICKS_PER_SECOND - seconds) < 1e-5;
    }
    CHECK(quadratic, "a ramp's births");
}

static void TestCubicAndLoops(void)
{
    // A rate rising along a cubic and falling to 0, then 40, over a loop
    // of 3 seconds.
    mvfxCurveKey keys[4] = {{0.0f, 5.0f, 0, 0, mvfx_interpolationLinear},
                            {1.0f / 3.0f, 200.0f, 0, 0, mvfx_interpolationCubic},
                            {2.5f / 3.0f, 0.0f, 0, 0, mvfx_interpolationLinear},
                            {1.0f, 40.0f, 0, 0, mvfx_interpolationLinear}};
    int32_t count = 0;
    CHECK(BirthsAre(keys, 4, 1.0f, 3.0f, 1, 4 * MVFX_TICKS_PER_SECOND, &count) && count > 200,
          "a cubic's births as integrated");
    // A ramp of 2.5 births a loop, three loops: what one leaves past a
    // whole birth is carried into the next, 7 in all.
    mvfxCurveKey ramp[2] = {{0.0f, 0.0f, 0, 0, mvfx_interpolationLinear},
                            {1.0f, 1.0f, 0, 0, mvfx_interpolationLinear}};
    CHECK(BirthsAre(ramp, 2, 5.0f, 1.0f, 3, 4 * MVFX_TICKS_PER_SECOND, &count) && count == 7,
          "loops carry the integral");
}

static void TestPlateausAndEnds(void)
{
    // Rate 10, then 0 from a third of the loop to two thirds, then 10.
    mvfxCurveKey keys[4] = {{0.0f, 10.0f, 0, 0, mvfx_interpolationConstant},
                            {1.0f / 3.0f, 0.0f, 0, 0, mvfx_interpolationConstant},
                            {2.0f / 3.0f, 10.0f, 0, 0, mvfx_interpolationConstant},
                            {1.0f, 10.0f, 0, 0, mvfx_interpolationConstant}};
    int32_t count = 0;
    CHECK(BirthsAre(keys, 4, 1.0f, 3.0f, 1, 4 * MVFX_TICKS_PER_SECOND, &count), "as integrated");
    int64_t ticks[MOST_BIRTHS];
    count = Births(keys, 4, 1.0f, 3.0f, 1, 4 * MVFX_TICKS_PER_SECOND, 16384, ticks);
    bool none = true;
    for (int32_t j = 0; j < count; ++j)
    {
        none = none && !(ticks[j] > (int64_t)(1.02 * MVFX_TICKS_PER_SECOND) &&
                         ticks[j] < 2 * MVFX_TICKS_PER_SECOND);
    }
    CHECK(none && count >= 19, "none on the plateau");

    // A rate of 510 to just past sample 50 of 255, then 0 from sample 51
    // until 0.6: the integral at sample 51 is 101 exactly, and stays so
    // over the plateau. Birth 101 comes where it is first reached, at
    // sample 51, not where the plateau ends.
    mvfxCurveKey whole[3] = {{0.0f, 1.0f, 0, 0, mvfx_interpolationConstant},
                             {0.198f, 0.0f, 0, 0, mvfx_interpolationConstant},
                             {0.6f, 1.0f, 0, 0, mvfx_interpolationConstant}};
    CHECK(BirthsAre(whole, 3, 510.0f, 1.0f, 1, 2 * MVFX_TICKS_PER_SECOND, &count), "as integrated");
    count = Births(whole, 3, 510.0f, 1.0f, 1, 2 * MVFX_TICKS_PER_SECOND, 16384, ticks);
    int64_t first = (int64_t)ceil(51.0 / 255.0 * MVFX_TICKS_PER_SECOND);
    CHECK(count > 101 && ticks[100] - first <= 1 && first - ticks[100] <= 1 &&
              ticks[101] > (int64_t)(0.6 * MVFX_TICKS_PER_SECOND),
          "a plateau's integral first reached where it begins");

    // A rate falling from 10 to 0 over a second: 5 births, the last near
    // the loop's end, and never more.
    mvfxCurveKey ending[2] = {{0.0f, 10.0f, 0, 0, mvfx_interpolationLinear},
                              {1.0f, 0.0f, 0, 0, mvfx_interpolationLinear}};
    CHECK(BirthsAre(ending, 2, 1.0f, 1.0f, 1, 3 * MVFX_TICKS_PER_SECOND, &count) && count == 5,
          "a rate that ends at 0 reaches no more");
}

int main(void)
{
    TestConstantRate();
    TestRampIsQuadratic();
    TestCubicAndLoops();
    TestPlateausAndEnds();
    return s_failures == 0 ? 0 : 1;
}
