// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The integral of a baked curve, which is piecewise linear, so its
// integral is piecewise quadratic and both it and its inverse are exact
// up to rounding.

#include "curve_integral.h"

#include <math.h>

void mvfxIntegrateCurve(const mvfxCurveTable* table, double end, mvfxCurveIntegral* integralOut)
{
    double start = (double)table->start;
    double width = end - start;
    integralOut->start = start;
    integralOut->end = end;
    integralOut->cumulative[0] = 0.0;
    // The trapezoids' heights summed first, exact in binary64 for float
    // samples of a moderate range, then scaled once: a whole number of
    // particles over a domain stays whole (a constant 64 over [0, 1] sums to
    // 16,320 halves, and 16,320 / 255 is 64), where adding a rounded width
    // 255 times would fall short of it.
    double heights = 0.0;
    for (int32_t i = 1; i < MVFX_CURVE_SAMPLES; ++i)
    {
        heights += 0.5 * ((double)table->samples[i - 1] + (double)table->samples[i]);
        integralOut->cumulative[i] = heights / (double)(MVFX_CURVE_SAMPLES - 1) * width;
    }
}

// The width of one sample interval.
static double Step(const mvfxCurveIntegral* integral)
{
    return (integral->end - integral->start) / (double)(MVFX_CURVE_SAMPLES - 1);
}

double mvfxCurveIntegralAt(const mvfxCurveTable* table, const mvfxCurveIntegral* integral,
                           double time)
{
    const int32_t last = MVFX_CURVE_SAMPLES - 1;
    if (time <= integral->start)
    {
        return (time - integral->start) * (double)table->samples[0];
    }
    if (time >= integral->end)
    {
        return integral->cumulative[last] + (time - integral->end) * (double)table->samples[last];
    }
    double step = Step(integral);
    double position = (time - integral->start) / step;
    int32_t index = (int32_t)position;
    index = index > last - 1 ? last - 1 : index;
    double f = position - (double)index;
    double low = (double)table->samples[index];
    double rise = (double)table->samples[index + 1] - low;
    return integral->cumulative[index] + step * (low * f + rise * f * f * 0.5);
}

// The fraction f in [0, 1] of an interval of width step, starting at
// value low and rising by rise, at which the area reaches area >= 0:
// step (low f + rise f^2 / 2) = area, by the root free of cancellation.
static double SolveInterval(double low, double rise, double step, double area)
{
    double scaled = area / step;
    double discriminant = low * low + 2.0 * rise * scaled;
    discriminant = discriminant < 0.0 ? 0.0 : discriminant;
    double denominator = low + sqrt(discriminant);
    double f = denominator > 0.0 ? 2.0 * scaled / denominator : 0.0;
    return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}

double mvfxCurveIntegralTime(const mvfxCurveTable* table, const mvfxCurveIntegral* integral,
                             double target)
{
    const int32_t last = MVFX_CURVE_SAMPLES - 1;
    double first = (double)table->samples[0];
    if (target <= 0.0)
    {
        return first > 0.0 ? integral->start + target / first : integral->start;
    }
    double total = integral->cumulative[last];
    if (target > total)
    {
        double final = (double)table->samples[last];
        return final > 0.0 ? integral->end + (target - total) / final : (double)INFINITY;
    }
    // The first interval whose end reaches the target.
    int32_t low = 0;
    int32_t high = last;
    while (high - low > 1)
    {
        int32_t middle = low + (high - low) / 2;
        if (integral->cumulative[middle] >= target)
        {
            high = middle;
        }
        else
        {
            low = middle;
        }
    }
    double step = Step(integral);
    double value = (double)table->samples[low];
    double rise = (double)table->samples[high] - value;
    double f = SolveInterval(value, rise, step, target - integral->cumulative[low]);
    return integral->start + step * ((double)low + f);
}
