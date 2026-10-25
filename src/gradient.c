// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gradients: color and alpha keys, interpolated linearly in linear RGB,
// baked to tables.

#include "gradient.h"

#include <math.h>

// Linear interpolation between the keys around t, the end values held
// past the ends; of two keys at one time, the later one from it on.
static float Interpolate(const float* times, const float* values, int32_t count, double t)
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
    while (i < count - 1 && (double)times[i + 1] <= t)
    {
        ++i;
    }
    double u = (t - (double)times[i]) / ((double)times[i + 1] - (double)times[i]);
    return (float)((double)values[i] + ((double)values[i + 1] - (double)values[i]) * u);
}

void mvfxBakeGradient(const mvfxGradient* gradient, mvfxGradientTable* tableOut)
{
    float colorTimes[MVFX_CURVE_MAX_KEYS] = {0};
    float channels[3][MVFX_CURVE_MAX_KEYS] = {{0}};
    float alphaTimes[MVFX_CURVE_MAX_KEYS] = {0};
    float alphas[MVFX_CURVE_MAX_KEYS] = {0};
    for (int32_t k = 0; k < gradient->colorKeyCount; ++k)
    {
        colorTimes[k] = gradient->colorKeys[k].time;
        channels[0][k] = gradient->colorKeys[k].red;
        channels[1][k] = gradient->colorKeys[k].green;
        channels[2][k] = gradient->colorKeys[k].blue;
    }
    for (int32_t k = 0; k < gradient->alphaKeyCount; ++k)
    {
        alphaTimes[k] = gradient->alphaKeys[k].time;
        alphas[k] = gradient->alphaKeys[k].alpha;
    }
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        double t = (double)i / (double)(MVFX_CURVE_SAMPLES - 1);
        for (int32_t c = 0; c < 3; ++c)
        {
            tableOut->rgba[i][c] = Interpolate(colorTimes, channels[c], gradient->colorKeyCount, t);
        }
        tableOut->rgba[i][3] = Interpolate(alphaTimes, alphas, gradient->alphaKeyCount, t);
    }
}

// Whether a key's channel is within MVFX_MAX_COLOR in size; false for NaN.
static bool IsChannel(float x)
{
    return x >= -MVFX_MAX_COLOR && x <= MVFX_MAX_COLOR;
}

void mvfxGradientLargest(const mvfxGradient* gradient, double* color, double* alpha)
{
    *color = 0.0;
    *alpha = 0.0;
    for (int32_t i = 0; i < gradient->colorKeyCount; ++i)
    {
        const mvfxColorKey* key = &gradient->colorKeys[i];
        double sizes[3] = {fabs((double)key->red), fabs((double)key->green),
                           fabs((double)key->blue)};
        for (int32_t c = 0; c < 3; ++c)
        {
            *color = sizes[c] > *color ? sizes[c] : *color;
        }
    }
    for (int32_t i = 0; i < gradient->alphaKeyCount; ++i)
    {
        double size = fabs((double)gradient->alphaKeys[i].alpha);
        *alpha = size > *alpha ? size : *alpha;
    }
}

bool mvfxIsGradientValid(const mvfxGradient* gradient)
{
    if (gradient == nullptr || gradient->colorKeys == nullptr || gradient->alphaKeys == nullptr ||
        gradient->colorKeyCount < 1 || gradient->colorKeyCount > MVFX_CURVE_MAX_KEYS ||
        gradient->alphaKeyCount < 1 || gradient->alphaKeyCount > MVFX_CURVE_MAX_KEYS)
    {
        return false;
    }
    float previous = 0.0f;
    for (int32_t i = 0; i < gradient->colorKeyCount; ++i)
    {
        const mvfxColorKey* key = &gradient->colorKeys[i];
        if (!(key->time >= previous && key->time <= 1.0f) || !IsChannel(key->red) ||
            !IsChannel(key->green) || !IsChannel(key->blue))
        {
            return false;
        }
        previous = key->time;
    }
    previous = 0.0f;
    for (int32_t i = 0; i < gradient->alphaKeyCount; ++i)
    {
        const mvfxAlphaKey* key = &gradient->alphaKeys[i];
        if (!(key->time >= previous && key->time <= 1.0f) || !IsChannel(key->alpha))
        {
            return false;
        }
        previous = key->time;
    }
    return true;
}
