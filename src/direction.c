// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Unit vectors.

#include "direction.h"

#include "elementary.h"
#include "random.h"

#include <math.h>

#define TWO_PI 6.28318530717958647692f

bool mvfxNormalize(float v[3])
{
    double square =
        (double)v[0] * (double)v[0] + (double)v[1] * (double)v[1] + (double)v[2] * (double)v[2];
    if (!(square > 0.0 && square < (double)INFINITY))
    {
        return false;
    }
    double scale = 1.0 / sqrt(square);
    for (int32_t i = 0; i < 3; ++i)
    {
        v[i] = (float)((double)v[i] * scale);
    }
    return true;
}

void mvfxRandomDirection(uint32_t a, uint32_t b, float out[3])
{
    // Z uniform in [-1, 1] and the azimuth uniform: uniform on the sphere
    // (Archimedes' hat-box theorem).
    float z = 1.0f - 2.0f * mvfxUnitFloat(a);
    float ring = sqrtf(fmaxf(0.0f, 1.0f - z * z));
    float sine = 0.0f;
    float cosine = 0.0f;
    mvfxSinCos(TWO_PI * mvfxUnitFloat(b), &sine, &cosine);
    out[0] = ring * cosine;
    out[1] = ring * sine;
    out[2] = z;
}
