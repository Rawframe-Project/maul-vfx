// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Curves and gradients as an effect bakes them, read from its emitter's
// view: the refusals of curves, gradients and rate curves; every sample of
// a baked curve against the curve's value from its keys by curve.h's
// rules (constant, linear and Hermite segments, steps, tangents given,
// smooth and monotone), the monotone rule never overshooting and smooth
// at keys, the smooth rule keeping a line straight; the domain a curve is
// baked over, with a duration and without; every sample of a baked
// gradient against linear blends of its keys; and a curve in seconds
// read past both ends of its keys, in particles' sizes.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <stdlib.h>

// The oracle: a curve's value from its keys in binary64, written from
// curve.h. A segment runs from the last key at or before the time (of two
// keys at one time, the later); before the first key and after the last
// the curve holds their values.

// The slope from key a to key b, 0 across a step.
static double Slope(const mvfxCurveKey* a, const mvfxCurveKey* b)
{
    double span = (double)b->time - (double)a->time;
    return span > 0.0 ? ((double)b->value - (double)a->value) / span : 0.0;
}

// Every key's tangent by a rule: the given ones (outTangent and the next
// key's inTangent, kept in two arrays); smooth, the slope between the
// neighbors (one-sided at the ends); or Fritsch and Carlson's ("Monotone
// Piecewise Cubic Interpolation", SIAM J. Numer. Anal. 17, 1980): the
// mean of the slopes either side, 0 where they differ in sign, then per
// segment both 0 on a flat one, else both scaled into the circle of
// radius 3.
static void Tangents(const mvfxCurve* curve, double* outs, double* ins)
{
    const mvfxCurveKey* k = curve->keys;
    int32_t last = curve->keyCount - 1;
    for (int32_t i = 0; i <= last; ++i)
    {
        outs[i] = (double)k[i].outTangent;
        ins[i] = (double)k[i].inTangent;
    }
    if (curve->tangents == mvfx_tangentsSmooth)
    {
        for (int32_t i = 0; i <= last; ++i)
        {
            outs[i] = Slope(&k[i > 0 ? i - 1 : i], &k[i < last ? i + 1 : i]);
            ins[i] = outs[i];
        }
    }
    else if (curve->tangents == mvfx_tangentsMonotone)
    {
        double slopes[MVFX_CURVE_MAX_KEYS] = {0};
        for (int32_t i = 0; i < last; ++i)
        {
            slopes[i] = Slope(&k[i], &k[i + 1]);
        }
        double m[MVFX_CURVE_MAX_KEYS] = {0};
        m[0] = slopes[0];
        m[last] = last > 0 ? slopes[last - 1] : 0.0;
        for (int32_t i = 1; i < last; ++i)
        {
            m[i] = slopes[i - 1] * slopes[i] <= 0.0 ? 0.0 : (slopes[i - 1] + slopes[i]) / 2.0;
        }
        for (int32_t i = 0; i < last; ++i)
        {
            if (slopes[i] == 0.0)
            {
                m[i] = 0.0;
                m[i + 1] = 0.0;
                continue;
            }
            double a = m[i] / slopes[i];
            double b = m[i + 1] / slopes[i];
            if (a * a + b * b > 9.0)
            {
                double tau = 3.0 / sqrt(a * a + b * b);
                m[i] = tau * a * slopes[i];
                m[i + 1] = tau * b * slopes[i];
            }
        }
        for (int32_t i = 0; i <= last; ++i)
        {
            outs[i] = m[i];
            ins[i] = m[i];
        }
    }
}

static double ValueAt(const mvfxCurve* curve, double t)
{
    const mvfxCurveKey* k = curve->keys;
    int32_t last = curve->keyCount - 1;
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
    if (k[i].interpolation == mvfx_interpolationConstant)
    {
        return a;
    }
    if (k[i].interpolation == mvfx_interpolationLinear)
    {
        return a + (b - a) * u;
    }
    double outs[MVFX_CURVE_MAX_KEYS];
    double ins[MVFX_CURVE_MAX_KEYS];
    Tangents(curve, outs, ins);
    double u2 = u * u;
    double u3 = u2 * u;
    return (2.0 * u3 - 3.0 * u2 + 1.0) * a + (u3 - 2.0 * u2 + u) * span * outs[i] +
           (-2.0 * u3 + 3.0 * u2) * b + (u3 - u2) * span * ins[i + 1];
}

// A baked curve as a view gives it: MVFX_CURVE_SAMPLES samples, then the
// domain's start and the factor to a sample's place.
typedef struct Baked
{
    float samples[MVFX_CURVE_SAMPLES];
    float start;
    float scale;
} Baked;

// Bakes a curve as an emitter's size, scaling every component by 1, with
// a duration of a second (over [0, 1]) or without (over its keys' span),
// and reads it back; the effect's result.
static mvfxResult Bake(const mvfxCurve* curve, bool duration, Baked* bakedOut)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.duration = duration ? 1.0f : 0.0f;
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceCurve, .min = {1, 1, 1, 0}, .max = {1, 1, 1, 0}, .curve = *curve};
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxResult result = mvfxCreateEffect(context, &effectDef, &effect);
    if (result == mvfx_success && bakedOut != NULL)
    {
        mvfxEmitterView view;
        CHECK(mvfxEffect_GetEmitterView(context, effect, 0, &view) == mvfx_success &&
                  view.size.curve != NULL,
              "the size's baked curve");
        for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
        {
            bakedOut->samples[i] = view.size.curve[i];
        }
        bakedOut->start = view.size.curve[MVFX_CURVE_SAMPLES];
        bakedOut->scale = view.size.curve[MVFX_CURVE_SAMPLES + 1];
    }
    mvfxDestroyContext(context);
    return result;
}

// Where sample i lies in a domain.
static double SampleTime(double start, double end, int32_t i)
{
    return start + (end - start) * (double)i / (double)(MVFX_CURVE_SAMPLES - 1);
}

// Whether every sample is the curve's value at its place, rounded once.
static bool SamplesAre(const mvfxCurve* curve, const Baked* baked, double start, double end)
{
    bool same = true;
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        same = same && baked->samples[i] == (float)ValueAt(curve, SampleTime(start, end, i));
    }
    return same;
}

static mvfxCurve CurveOf(const mvfxCurveKey* keys, int32_t count, mvfxTangents tangents)
{
    return (mvfxCurve){keys, count, tangents};
}

static void TestRefusals(void)
{
    mvfxCurveKey keys[3] = {{0.0f, 1.0f, 0, 0, mvfx_interpolationLinear},
                            {0.5f, 2.0f, 0, 0, mvfx_interpolationCubic},
                            {1.0f, 0.0f, 0, 0, mvfx_interpolationConstant}};
    mvfxCurve curve = CurveOf(keys, 3, mvfx_tangentsGiven);
    CHECK(Bake(&curve, true, NULL) == mvfx_success, "a valid curve");
    mvfxCurve empty = CurveOf(keys, 0, mvfx_tangentsGiven);
    CHECK(Bake(&empty, false, NULL) == mvfx_errorInvalid, "no keys");
    mvfxCurve noKeys = CurveOf(NULL, 3, mvfx_tangentsGiven);
    CHECK(Bake(&noKeys, false, NULL) == mvfx_errorInvalid, "a null key array");
    mvfxCurve badRule = CurveOf(keys, 3, (mvfxTangents)3);
    CHECK(Bake(&badRule, false, NULL) == mvfx_errorInvalid, "an unknown tangent rule");

    // The most keys, and one more.
    mvfxCurveKey many[MVFX_CURVE_MAX_KEYS + 1];
    for (int32_t i = 0; i <= MVFX_CURVE_MAX_KEYS; ++i)
    {
        many[i] = (mvfxCurveKey){(float)i / 128.0f, 1.0f, 0, 0, mvfx_interpolationLinear};
    }
    mvfxCurve most = CurveOf(many, MVFX_CURVE_MAX_KEYS, mvfx_tangentsGiven);
    CHECK(Bake(&most, true, NULL) == mvfx_success, "the most keys");
    mvfxCurve tooMany = CurveOf(many, MVFX_CURVE_MAX_KEYS + 1, mvfx_tangentsGiven);
    CHECK(Bake(&tooMany, true, NULL) == mvfx_errorInvalid, "too many keys");

    // Each field of a key out of bounds; then each at its bound, which is
    // taken. A field's bound is 1e18 in size.
    for (int32_t field = 0; field < 7; ++field)
    {
        mvfxCurveKey bad[3] = {keys[0], keys[1], keys[2]};
        switch (field)
        {
        case 0:
            bad[1].time = NAN;
            break;
        case 1:
            bad[1].value = INFINITY;
            break;
        case 2:
            bad[1].inTangent = -INFINITY;
            break;
        case 3:
            bad[1].outTangent = 2e18f;
            break;
        case 4:
            bad[1].inTangent = -2e18f;
            break;
        case 5:
            bad[1].interpolation = 3;
            break;
        default:
            bad[2].time = 0.25f;
            break;
        }
        mvfxCurve broken = CurveOf(bad, 3, mvfx_tangentsGiven);
        CHECK(Bake(&broken, false, NULL) == mvfx_errorInvalid, "a broken key is refused");
    }
    mvfxCurveKey edge[2] = {{-1e18f, 1e18f, -1e18f, 1e18f, mvfx_interpolationCubic},
                            {1e18f, -1e18f, 1e18f, -1e18f, mvfx_interpolationConstant}};
    mvfxCurve atBounds = CurveOf(edge, 2, mvfx_tangentsGiven);
    CHECK(Bake(&atBounds, false, NULL) == mvfx_success, "every field at its bound");
    edge[1].interpolation = mvfx_interpolationCubic;
    CHECK(Bake(&atBounds, false, NULL) == mvfx_success, "the last interpolation known");
    mvfxCurveKey same[2] = {{0.5f, 1.0f, 0, 0, 0}, {0.5f, 2.0f, 0, 0, 0}};
    mvfxCurve step = CurveOf(same, 2, mvfx_tangentsGiven);
    CHECK(Bake(&step, true, NULL) == mvfx_success, "two keys at one time");

    // Times outside [0, 1]: any domain without a duration, none with.
    mvfxCurveKey outside[2] = {{-0.5f, 0, 0, 0, 0}, {2.0f, 0, 0, 0, 0}};
    mvfxCurve wide = CurveOf(outside, 2, mvfx_tangentsGiven);
    CHECK(Bake(&wide, false, NULL) == mvfx_success, "any domain without a duration");
    CHECK(Bake(&wide, true, NULL) == mvfx_errorInvalid, "outside [0, 1] with one");
    mvfxCurveKey ends[2] = {{0.0f, 0, 0, 0, 0}, {1.0f, 0, 0, 0, 0}};
    mvfxCurve unit = CurveOf(ends, 2, mvfx_tangentsGiven);
    CHECK(Bake(&unit, true, NULL) == mvfx_success, "0 and 1 with one");
    ends[0].time = -0x1p-24f;
    CHECK(Bake(&unit, true, NULL) == mvfx_errorInvalid, "just below 0 with one");
    ends[0].time = 0.0f;
    ends[1].time = 0x1.000002p0f;
    CHECK(Bake(&unit, true, NULL) == mvfx_errorInvalid, "just past 1 with one");
}

static void TestInterpolations(void)
{
    // Keys at 0, 1, 2 and 3 over [0, 3]: samples 0, 85, 170 and 255.
    mvfxCurveKey keys[4] = {{0.0f, 1.0f, 0, 0, mvfx_interpolationConstant},
                            {1.0f, 3.0f, 0, 0, mvfx_interpolationLinear},
                            {2.0f, 5.0f, 4.0f, 4.0f, mvfx_interpolationCubic},
                            {3.0f, 0.0f, -1.0f, -1.0f, mvfx_interpolationLinear}};
    mvfxCurve curve = CurveOf(keys, 4, mvfx_tangentsGiven);
    Baked baked;
    CHECK(Bake(&curve, false, &baked) == mvfx_success, "baked");
    CHECK(SamplesAre(&curve, &baked, 0.0, 3.0), "every sample the curve's value");
    CHECK(baked.samples[0] == 1.0f && baked.samples[84] == 1.0f, "a constant segment");
    CHECK(baked.samples[85] == 3.0f && baked.samples[170] == 5.0f && baked.samples[255] == 0.0f,
          "at the keys");
    CHECK(baked.samples[127] == (float)(3.0 + 2.0 * (127.0 / 85.0 - 1.0)), "a linear segment");
    // Hermite from 5 (slope 4) to 0 (slope -1) over one unit.
    for (int32_t i = 171; i < 255; ++i)
    {
        double u = (double)i / 85.0 - 2.0;
        double h = (2 * u * u * u - 3 * u * u + 1) * 5.0 + (u * u * u - 2 * u * u + u) * 4.0 +
                   (u * u * u - u * u) * -1.0;
        CHECK(fabs((double)baked.samples[i] - h) <= 1e-6, "a cubic segment");
    }

    // A step at 1: the earlier key's value before it, the later one's at it.
    mvfxCurveKey step[4] = {{0.0f, 1.0f, 0, 0, mvfx_interpolationLinear},
                            {1.0f, 1.0f, 0, 0, mvfx_interpolationLinear},
                            {1.0f, 7.0f, 0, 0, mvfx_interpolationLinear},
                            {3.0f, 7.0f, 0, 0, mvfx_interpolationLinear}};
    mvfxCurve stepped = CurveOf(step, 4, mvfx_tangentsGiven);
    CHECK(Bake(&stepped, false, &baked) == mvfx_success, "baked");
    CHECK(baked.samples[84] == 1.0f && baked.samples[85] == 7.0f, "at a step, the later key");
    CHECK(SamplesAre(&stepped, &baked, 0.0, 3.0), "every sample the curve's value");

    // Given tangents that differ either side of a key: a segment leaves a
    // key along its out tangent and arrives along the next key's in.
    mvfxCurveKey turns[3] = {{0.0f, 0.0f, -7.0f, 2.0f, mvfx_interpolationCubic},
                             {1.0f, 1.0f, -3.0f, 5.0f, mvfx_interpolationCubic},
                             {3.0f, 0.0f, 1.0f, 9.0f, mvfx_interpolationCubic}};
    mvfxCurve turning = CurveOf(turns, 3, mvfx_tangentsGiven);
    CHECK(Bake(&turning, false, &baked) == mvfx_success, "baked");
    CHECK(SamplesAre(&turning, &baked, 0.0, 3.0), "out tangents leave, in tangents arrive");

    // Held before the first key and after the last, over [0, 1].
    mvfxCurveKey inner[2] = {{0.25f, 2.0f, 0, 0, mvfx_interpolationLinear},
                             {0.75f, 4.0f, 0, 0, mvfx_interpolationLinear}};
    mvfxCurve held = CurveOf(inner, 2, mvfx_tangentsGiven);
    CHECK(Bake(&held, true, &baked) == mvfx_success, "baked");
    CHECK(baked.samples[0] == 2.0f && baked.samples[63] == 2.0f && baked.samples[192] == 4.0f &&
              baked.samples[255] == 4.0f,
          "held past the ends");
    CHECK(SamplesAre(&held, &baked, 0.0, 1.0), "every sample the curve's value");
}

static void TestDomains(void)
{
    // With a duration, [0, 1]; without, the keys' span in seconds.
    mvfxCurveKey keys[2] = {{0.25f, 2.0f, 0, 0, mvfx_interpolationLinear},
                            {0.75f, 4.0f, 0, 0, mvfx_interpolationLinear}};
    mvfxCurve curve = CurveOf(keys, 2, mvfx_tangentsGiven);
    Baked baked;
    CHECK(Bake(&curve, true, &baked) == mvfx_success && baked.start == 0.0f &&
              baked.scale == 255.0f,
          "over [0, 1] with a duration");
    CHECK(Bake(&curve, false, &baked) == mvfx_success && baked.start == 0.25f &&
              baked.scale == 510.0f && SamplesAre(&curve, &baked, 0.25, 0.75),
          "over the keys' span without");
    // One key: a domain of one point, every sample its value.
    mvfxCurveKey one = {0.3f, 4.0f, 0, 0, mvfx_interpolationLinear};
    mvfxCurve single = CurveOf(&one, 1, mvfx_tangentsGiven);
    CHECK(Bake(&single, false, &baked) == mvfx_success && baked.start == 0.3f &&
              baked.scale == 0.0f,
          "a domain of one point");
    bool four = true;
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        four = four && baked.samples[i] == 4.0f;
    }
    CHECK(four, "every sample its value");
}

static void TestSmoothKeepsALineStraight(void)
{
    mvfxCurveKey keys[5];
    for (int32_t i = 0; i < 5; ++i)
    {
        keys[i] = (mvfxCurveKey){(float)i * 0.25f, (float)i * 2.0f - 1.0f, 99.0f, -99.0f,
                                 mvfx_interpolationCubic};
    }
    mvfxCurve curve = CurveOf(keys, 5, mvfx_tangentsSmooth);
    Baked baked;
    CHECK(Bake(&curve, true, &baked) == mvfx_success, "baked");
    bool straight = true;
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        double t = SampleTime(0.0, 1.0, i);
        straight = straight && fabs((double)baked.samples[i] - (8.0 * t - 1.0)) < 1e-6;
    }
    CHECK(straight, "the smooth rule ignores given tangents and keeps a line");
    CHECK(SamplesAre(&curve, &baked, 0.0, 1.0), "every sample the curve's value");

    // Not a line: each key's tangent the slope between its neighbors.
    mvfxCurveKey bent[4] = {{0.0f, 0.0f, 9.0f, 9.0f, mvfx_interpolationCubic},
                            {0.25f, 1.0f, 9.0f, 9.0f, mvfx_interpolationCubic},
                            {0.5f, 0.5f, 9.0f, 9.0f, mvfx_interpolationCubic},
                            {1.0f, 2.0f, 9.0f, 9.0f, mvfx_interpolationCubic}};
    mvfxCurve smooth = CurveOf(bent, 4, mvfx_tangentsSmooth);
    CHECK(Bake(&smooth, true, &baked) == mvfx_success, "baked");
    CHECK(SamplesAre(&smooth, &baked, 0.0, 1.0), "every sample the curve's value");
}

static void TestMonotoneNeverOvershoots(void)
{
    srand(7);
    for (int32_t trial = 0; trial < 200; ++trial)
    {
        // Increasing with flats and jumps, where tangents from neighbors
        // would overshoot, over a domain without a duration.
        mvfxCurveKey keys[8];
        float time = 0.0f;
        float value = 0.0f;
        for (int32_t i = 0; i < 8; ++i)
        {
            keys[i] = (mvfxCurveKey){time, value, 50.0f, 50.0f, mvfx_interpolationCubic};
            time += 0.01f + (float)(rand() % 100) / 100.0f;
            int32_t kind = rand() % 3;
            value += kind == 0 ? 0.0f : (kind == 1 ? 0.1f : 10.0f);
        }
        mvfxCurve curve = CurveOf(keys, 8, mvfx_tangentsMonotone);
        Baked baked;
        CHECK(Bake(&curve, false, &baked) == mvfx_success, "baked");
        bool ok = true;
        for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
        {
            float v = baked.samples[i];
            ok = ok && v >= 0.0f && v <= keys[7].value && (i == 0 || v >= baked.samples[i - 1]);
        }
        CHECK(ok, "monotone keys give a monotone curve within their range");
        CHECK(SamplesAre(&curve, &baked, 0.0, (double)keys[7].time),
              "every sample the curve's value");
    }
    // A step among monotone keys: the slope across it counts as 0.
    mvfxCurveKey stepKeys[4] = {{0.0f, 0.0f, 0, 0, mvfx_interpolationCubic},
                                {1.0f, 1.0f, 0, 0, mvfx_interpolationCubic},
                                {1.0f, 2.0f, 0, 0, mvfx_interpolationCubic},
                                {3.0f, 3.0f, 0, 0, mvfx_interpolationCubic}};
    for (mvfxTangents rule = mvfx_tangentsSmooth; rule <= mvfx_tangentsMonotone; ++rule)
    {
        mvfxCurve stepped = CurveOf(stepKeys, 4, rule);
        Baked baked;
        CHECK(Bake(&stepped, false, &baked) == mvfx_success, "baked");
        CHECK(SamplesAre(&stepped, &baked, 0.0, 3.0), "a step among tangents from neighbors");
    }

    // The given tangents do overshoot, so the check above can fail.
    mvfxCurveKey keys[3] = {{0.0f, 0.0f, 0, 50.0f, mvfx_interpolationCubic},
                            {1.0f, 1.0f, 50.0f, 50.0f, mvfx_interpolationCubic},
                            {2.0f, 1.0f, 0, 0, mvfx_interpolationCubic}};
    Baked given;
    Baked monotone;
    mvfxCurve givenCurve = CurveOf(keys, 3, mvfx_tangentsGiven);
    mvfxCurve monotoneCurve = CurveOf(keys, 3, mvfx_tangentsMonotone);
    CHECK(Bake(&givenCurve, false, &given) == mvfx_success &&
              Bake(&monotoneCurve, false, &monotone) == mvfx_success,
          "baked");
    CHECK(given.samples[191] > 1.0f, "given tangents overshoot");
    bool within = true;
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        within = within && monotone.samples[i] <= 1.0f;
    }
    CHECK(within, "the monotone rule does not");
}

static void TestMonotoneIsSmoothAtKeys(void)
{
    // Keys on samples (a domain of 255 seconds), a peak at 60 and a flat
    // from 72 to 120: one tangent per key serves both segments around it,
    // so the slope between samples does not jump at an interior key, and
    // a peak's is flat, so the curve does not pass it.
    mvfxCurveKey keys[6] = {{0.0f, 0.0f, 0, 0, mvfx_interpolationCubic},
                            {60.0f, 1.0f, 0, 0, mvfx_interpolationCubic},
                            {72.0f, 0.1f, 0, 0, mvfx_interpolationCubic},
                            {120.0f, 0.1f, 0, 0, mvfx_interpolationCubic},
                            {150.0f, 3.0f, 0, 0, mvfx_interpolationCubic},
                            {255.0f, 3.5f, 0, 0, mvfx_interpolationCubic}};
    mvfxCurve curve = CurveOf(keys, 6, mvfx_tangentsMonotone);
    Baked baked;
    CHECK(Bake(&curve, false, &baked) == mvfx_success, "baked");
    CHECK(SamplesAre(&curve, &baked, 0.0, 255.0), "every sample the curve's value");
    // Away from a key, the slope between samples changes by at most the
    // most it changes within a segment; across a key no more.
    double within = 0.0;
    for (int32_t i = 1; i < 255; ++i)
    {
        bool atKey = i == 60 || i == 72 || i == 120 || i == 150;
        double change = fabs((double)baked.samples[i + 1] - 2.0 * (double)baked.samples[i] +
                             (double)baked.samples[i - 1]);
        within = !atKey && change > within ? change : within;
    }
    for (int32_t k = 1; k < 5; ++k)
    {
        int32_t i = (int32_t)keys[k].time;
        double change = fabs((double)baked.samples[i + 1] - 2.0 * (double)baked.samples[i] +
                             (double)baked.samples[i - 1]);
        CHECK(change <= within, "the slope is continuous at a key");
    }
    CHECK(baked.samples[59] < 1.0f && baked.samples[60] == 1.0f && baked.samples[61] < 1.0f,
          "a peak has a flat tangent: no sample past it");
}

// Whether every live particle of an emitter whose size is a curve in
// seconds has the size `size`.
static bool SizesAre(mvfxContext* context, mvfxInstanceId instance, float size)
{
    static TestParticle records[256];
    int32_t count = ReadParticles(context, instance, 0, records, 256);
    bool all = count > 10;
    for (int32_t i = 0; i < count; ++i)
    {
        all = all && records[i].size[0] == size && records[i].size[1] == size &&
              records[i].size[2] == size;
    }
    return all;
}

static void TestReadPastTheEnds(void)
{
    // A size by a curve in seconds over [0.5, 1], read at each particle's
    // birth: held at its first value before 0.5 s and at its last after 1 s.
    mvfxCurveKey keys[2] = {{0.5f, 1.0f, 0, 0, mvfx_interpolationLinear},
                            {1.0f, 3.0f, 0, 0, mvfx_interpolationLinear}};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 200.0f;
    emitter.lifetime = mvfxRangeSource(0.25f, 0.25f);
    emitter.size = (mvfxVectorSource){.kind = mvfx_sourceCurve,
                                      .min = {1, 1, 1, 0},
                                      .max = {1, 1, 1, 0},
                                      .curve = CurveOf(keys, 2, mvfx_tangentsGiven)};
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND * 2 / 5) == mvfx_success, "stepped");
    CHECK(SizesAre(context, instance, 1.0f), "the first value before the curve");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND) == mvfx_success &&
              mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND * 3 / 5) == mvfx_success,
          "stepped");
    CHECK(SizesAre(context, instance, 3.0f), "the last value after it");
    mvfxDestroyContext(context);
}

// A rate curve over a loop of a second: whether an effect takes it.
static mvfxResult Rate(const mvfxCurveKey* keys, int32_t count, float duration)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.duration = duration;
    emitter.rateCurve = CurveOf(keys, count, mvfx_tangentsGiven);
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxResult result = mvfxCreateEffect(context, &effectDef, &effect);
    mvfxDestroyContext(context);
    return result;
}

static void TestRateCurves(void)
{
    // Keys never below 0 whose cubic dips below between them, refused;
    // the same curve as lines, and one at 0 throughout, taken.
    mvfxCurveKey dip[2] = {{0.0f, 1.0f, 0, -10.0f, mvfx_interpolationCubic},
                           {1.0f, 1.0f, 10.0f, 0, mvfx_interpolationCubic}};
    CHECK(Rate(dip, 2, 1.0f) == mvfx_errorInvalid, "a rate baked below 0");
    dip[0].interpolation = mvfx_interpolationLinear;
    CHECK(Rate(dip, 2, 1.0f) == mvfx_success, "a rate never below 0");
    mvfxCurveKey zero[2] = {{0.0f, 0.0f, 0, 0, mvfx_interpolationLinear},
                            {1.0f, 0.0f, 0, 0, mvfx_interpolationLinear}};
    CHECK(Rate(zero, 2, 1.0f) == mvfx_success, "a rate of 0");
    CHECK(Rate(zero, 2, 0.0f) == mvfx_errorInvalid, "a rate curve needs a duration");
    mvfxCurveKey late[2] = {{0.0f, 1.0f, 0, 0, mvfx_interpolationLinear},
                            {1.5f, 1.0f, 0, 0, mvfx_interpolationLinear}};
    CHECK(Rate(late, 2, 1.0f) == mvfx_errorInvalid, "a rate's keys within [0, 1]");
}

// A gradient as an emitter's colour drawn along it, and its baked samples.
static mvfxResult BakeGradient(const mvfxGradient* gradient, float (*rgbaOut)[4])
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.color = (mvfxVectorSource){.kind = mvfx_sourceGradient,
                                       .min = {1, 1, 1, 1},
                                       .max = {1, 1, 1, 1},
                                       .gradient = *gradient};
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxResult result = mvfxCreateEffect(context, &effectDef, &effect);
    if (result == mvfx_success && rgbaOut != NULL)
    {
        mvfxEmitterView view;
        CHECK(mvfxEffect_GetEmitterView(context, effect, 0, &view) == mvfx_success &&
                  view.color.gradient != NULL,
              "the colour's baked gradient");
        for (int32_t i = 0; i < 4 * MVFX_CURVE_SAMPLES; ++i)
        {
            rgbaOut[i / 4][i % 4] = view.color.gradient[i];
        }
    }
    mvfxDestroyContext(context);
    return result;
}

static void TestGradientRefusals(void)
{
    mvfxColorKey colors[2] = {{0.0f, 1, 0, 0}, {1.0f, 0, 0, 1}};
    mvfxAlphaKey alphas[1] = {{0.5f, 0.25f}};
    mvfxGradient gradient = {colors, 2, alphas, 1};
    CHECK(BakeGradient(&gradient, NULL) == mvfx_success, "a valid gradient");
    mvfxGradient noAlpha = {colors, 2, alphas, 0};
    CHECK(BakeGradient(&noAlpha, NULL) == mvfx_errorInvalid, "no alpha keys");
    mvfxGradient noColor = {colors, 0, alphas, 1};
    CHECK(BakeGradient(&noColor, NULL) == mvfx_errorInvalid, "no colour keys");
    mvfxGradient nullColors = {NULL, 2, alphas, 1};
    CHECK(BakeGradient(&nullColors, NULL) == mvfx_errorInvalid, "a null colour array");
    mvfxGradient nullAlphas = {colors, 2, NULL, 1};
    CHECK(BakeGradient(&nullAlphas, NULL) == mvfx_errorInvalid, "a null alpha array");
    mvfxColorKey many[MVFX_CURVE_MAX_KEYS + 1];
    mvfxAlphaKey manyAlphas[MVFX_CURVE_MAX_KEYS + 1];
    for (int32_t i = 0; i <= MVFX_CURVE_MAX_KEYS; ++i)
    {
        many[i] = (mvfxColorKey){(float)i / 128.0f, 1, 1, 1};
        manyAlphas[i] = (mvfxAlphaKey){(float)i / 128.0f, 1};
    }
    mvfxGradient most = {many, MVFX_CURVE_MAX_KEYS, manyAlphas, MVFX_CURVE_MAX_KEYS};
    CHECK(BakeGradient(&most, NULL) == mvfx_success, "the most keys");
    mvfxGradient tooManyColors = {many, MVFX_CURVE_MAX_KEYS + 1, alphas, 1};
    CHECK(BakeGradient(&tooManyColors, NULL) == mvfx_errorInvalid, "too many colour keys");
    mvfxGradient tooManyAlphas = {colors, 2, manyAlphas, MVFX_CURVE_MAX_KEYS + 1};
    CHECK(BakeGradient(&tooManyAlphas, NULL) == mvfx_errorInvalid, "too many alpha keys");
    mvfxColorKey backward[2] = {{0.6f, 1, 0, 0}, {0.5f, 0, 0, 1}};
    mvfxGradient reversed = {backward, 2, alphas, 1};
    CHECK(BakeGradient(&reversed, NULL) == mvfx_errorInvalid, "decreasing colour times");
    mvfxAlphaKey backAlpha[2] = {{0.6f, 1}, {0.5f, 1}};
    mvfxGradient reversedAlpha = {colors, 2, backAlpha, 2};
    CHECK(BakeGradient(&reversedAlpha, NULL) == mvfx_errorInvalid, "decreasing alpha times");
    mvfxColorKey past[1] = {{1.5f, 1, 0, 0}};
    mvfxGradient beyond = {past, 1, alphas, 1};
    CHECK(BakeGradient(&beyond, NULL) == mvfx_errorInvalid, "a colour time past 1");
    mvfxAlphaKey pastAlpha[1] = {{0x1.000002p0f, 1}};
    mvfxGradient beyondAlpha = {colors, 2, pastAlpha, 1};
    CHECK(BakeGradient(&beyondAlpha, NULL) == mvfx_errorInvalid, "an alpha time past 1");
    mvfxColorKey before[1] = {{-0x1p-24f, 1, 0, 0}};
    mvfxGradient early = {before, 1, alphas, 1};
    CHECK(BakeGradient(&early, NULL) == mvfx_errorInvalid, "a colour time below 0");
    mvfxAlphaKey badAlpha[1] = {{-0.1f, 1.0f}};
    mvfxGradient negative = {colors, 2, badAlpha, 1};
    CHECK(BakeGradient(&negative, NULL) == mvfx_errorInvalid, "an alpha time below 0");
    for (int32_t channel = 0; channel < 4; ++channel)
    {
        mvfxColorKey nan[1] = {{0.5f, 0, 0, 0}};
        mvfxAlphaKey nanAlpha[1] = {{0.5f, 1}};
        float* fields[4] = {&nan[0].red, &nan[0].green, &nan[0].blue, &nanAlpha[0].alpha};
        *fields[channel] = NAN;
        mvfxGradient notANumber = {nan, 1, nanAlpha, 1};
        CHECK(BakeGradient(&notANumber, NULL) == mvfx_errorInvalid, "a NaN channel");
        *fields[channel] = INFINITY;
        CHECK(BakeGradient(&notANumber, NULL) == mvfx_errorInvalid, "an infinite channel");
    }
    mvfxColorKey ends[2] = {{0.0f, 1, 0, 0}, {1.0f, 0, 0, 1}};
    mvfxAlphaKey alphaEnds[2] = {{0.0f, 0}, {1.0f, 1}};
    mvfxGradient whole = {ends, 2, alphaEnds, 2};
    CHECK(BakeGradient(&whole, NULL) == mvfx_success, "keys at 0 and 1");
}

// A gradient's channel from its keys: linear between the keys around t,
// held past the ends, of two keys at one time the later from it on.
static float Blend(const float* times, const float* values, int32_t count, double t)
{
    if (t <= (double)times[0])
    {
        return values[0];
    }
    if (t >= (double)times[count - 1])
    {
        return values[count - 1];
    }
    int32_t i = 0;
    while ((double)times[i + 1] <= t)
    {
        ++i;
    }
    double u = (t - (double)times[i]) / ((double)times[i + 1] - (double)times[i]);
    return (float)((double)values[i] + ((double)values[i + 1] - (double)values[i]) * u);
}

static void TestBlends(void)
{
    // Three colours from a quarter to three quarters, a step at a half
    // from green to a darker green; alpha apart, from 0 to 1.
    mvfxColorKey colors[4] = {
        {0.25f, 1, 0, 0}, {0.5f, 0, 1, 0}, {0.5f, 0, 0.5f, 0}, {0.75f, 0, 0, 1}};
    mvfxAlphaKey alphas[2] = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    mvfxGradient gradient = {colors, 4, alphas, 2};
    static float rgba[MVFX_CURVE_SAMPLES][4];
    CHECK(BakeGradient(&gradient, rgba) == mvfx_success, "baked");
    float times[4];
    float channels[3][4];
    for (int32_t k = 0; k < 4; ++k)
    {
        times[k] = colors[k].time;
        channels[0][k] = colors[k].red;
        channels[1][k] = colors[k].green;
        channels[2][k] = colors[k].blue;
    }
    float alphaTimes[2] = {0.0f, 1.0f};
    float alphaValues[2] = {0.0f, 1.0f};
    bool same = true;
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        double t = SampleTime(0.0, 1.0, i);
        for (int32_t c = 0; c < 3; ++c)
        {
            same = same && rgba[i][c] == Blend(times, channels[c], 4, t);
        }
        same = same && rgba[i][3] == Blend(alphaTimes, alphaValues, 2, t);
    }
    CHECK(same, "every sample the keys' blend");
    CHECK(rgba[0][0] == 1.0f && rgba[63][0] == 1.0f && rgba[0][3] == 0.0f,
          "held before the first key");
    CHECK(rgba[192][2] == 1.0f && rgba[255][2] == 1.0f && rgba[255][3] == 1.0f,
          "held after the last key");
    CHECK(rgba[127][1] > 0.99f && rgba[128][1] < 0.51f, "a step at a half, the later key");
    CHECK(fabs((double)rgba[100][0] - (0.5 - 100.0 / 255.0) / 0.25) < 1e-6 &&
              fabs((double)rgba[100][3] - 100.0 / 255.0) < 1e-6,
          "red blends linearly, alpha apart from colour");
}

int main(void)
{
    TestRefusals();
    TestInterpolations();
    TestDomains();
    TestSmoothKeepsALineStraight();
    TestMonotoneNeverOvershoots();
    TestMonotoneIsSmoothAtKeys();
    TestReadPastTheEnds();
    TestRateCurves();
    TestGradientRefusals();
    TestBlends();
    return s_failures == 0 ? 0 : 1;
}
