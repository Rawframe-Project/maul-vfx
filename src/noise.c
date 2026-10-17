// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "noise.h"

#include "random.h"

#include <math.h>

// The twelve edges of a cube, four repeated to fill sixteen (Perlin 2002).
static const float s_gradients[16][3] = {
    {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
    {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {-1, 1, 0}, {0, -1, 1}, {0, -1, -1},
};

// 6 t^5 - 15 t^4 + 10 t^3 and its derivative.
static float Fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float FadeSlope(float t)
{
    float u = t * (t - 1.0f);
    return 30.0f * u * u;
}

// A lattice coordinate's cell, wrapped to 32 bits, and the fraction past
// it: exact for any finite coordinate; one that is not finite (a particle
// flown past the float range) reads as 0.
static uint32_t Cell(double p, float* fraction)
{
    if (!isfinite(p))
    {
        *fraction = 0.0f;
        return 0;
    }
    double cell = floor(p);
    *fraction = (float)(p - cell);
    double wrapped = cell - 4294967296.0 * floor(cell / 4294967296.0);
    return (uint32_t)(int64_t)wrapped;
}

void mvfxGradientNoise3(const double p[3], uint32_t seed, float value[3], float gradient[3][3])
{
    float f[3];
    uint32_t cell[3];
    float fade[3];
    float slope[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        cell[k] = Cell(p[k], &f[k]);
        fade[k] = Fade(f[k]);
        slope[k] = FadeSlope(f[k]);
    }
    for (int32_t c = 0; c < 3; ++c)
    {
        value[c] = 0.0f;
        gradient[c][0] = 0.0f;
        gradient[c][1] = 0.0f;
        gradient[c][2] = 0.0f;
    }
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        uint32_t i = corner & 1u;
        uint32_t j = (corner >> 1u) & 1u;
        uint32_t k = (corner >> 2u) & 1u;
        mvfxWords4 h = mvfxPcg4d((mvfxWords4){cell[0] + i, cell[1] + j, cell[2] + k, seed});
        const uint32_t words[3] = {h.x, h.y, h.z};
        float d[3] = {f[0] - (float)i, f[1] - (float)j, f[2] - (float)k};
        // The corner's weight, the product of each axis's fade toward it,
        // and the weight's slope along each axis.
        float wx = i != 0 ? fade[0] : 1.0f - fade[0];
        float wy = j != 0 ? fade[1] : 1.0f - fade[1];
        float wz = k != 0 ? fade[2] : 1.0f - fade[2];
        float sx = i != 0 ? slope[0] : -slope[0];
        float sy = j != 0 ? slope[1] : -slope[1];
        float sz = k != 0 ? slope[2] : -slope[2];
        float weight = wx * wy * wz;
        float dw[3] = {sx * wy * wz, wx * sy * wz, wx * wy * sz};
        for (int32_t c = 0; c < 3; ++c)
        {
            const float* g = s_gradients[words[c] >> 28u];
            float dot = g[0] * d[0] + g[1] * d[1] + g[2] * d[2];
            value[c] += weight * dot;
            for (int32_t a = 0; a < 3; ++a)
            {
                gradient[c][a] += dw[a] * dot + weight * g[a];
            }
        }
    }
}

// One octave's curl at p: the six entries of mvfxGradientNoise3's gradient
// it reads, each summed over the corners in the same operations and order
// (so the same bits), without the value and the diagonal it does not.
static void CurlOctave(const double p[3], uint32_t seed, float curl[3])
{
    float f[3];
    uint32_t cell[3];
    float fade[3];
    float slope[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        cell[k] = Cell(p[k], &f[k]);
        fade[k] = Fade(f[k]);
        slope[k] = FadeSlope(f[k]);
    }
    // g[c][a] of the full gradient, as named: g21, g12, g02, g20, g10, g01.
    float g21 = 0.0f;
    float g12 = 0.0f;
    float g02 = 0.0f;
    float g20 = 0.0f;
    float g10 = 0.0f;
    float g01 = 0.0f;
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        uint32_t i = corner & 1u;
        uint32_t j = (corner >> 1u) & 1u;
        uint32_t k = (corner >> 2u) & 1u;
        mvfxWords4 h = mvfxPcg4d((mvfxWords4){cell[0] + i, cell[1] + j, cell[2] + k, seed});
        float d[3] = {f[0] - (float)i, f[1] - (float)j, f[2] - (float)k};
        float wx = i != 0 ? fade[0] : 1.0f - fade[0];
        float wy = j != 0 ? fade[1] : 1.0f - fade[1];
        float wz = k != 0 ? fade[2] : 1.0f - fade[2];
        float sx = i != 0 ? slope[0] : -slope[0];
        float sy = j != 0 ? slope[1] : -slope[1];
        float sz = k != 0 ? slope[2] : -slope[2];
        float weight = wx * wy * wz;
        float dw[3] = {sx * wy * wz, wx * sy * wz, wx * wy * sz};
        const float* a = s_gradients[h.x >> 28u];
        const float* b = s_gradients[h.y >> 28u];
        const float* c = s_gradients[h.z >> 28u];
        float da = a[0] * d[0] + a[1] * d[1] + a[2] * d[2];
        float db = b[0] * d[0] + b[1] * d[1] + b[2] * d[2];
        float dc = c[0] * d[0] + c[1] * d[1] + c[2] * d[2];
        g01 += dw[1] * da + weight * a[1];
        g02 += dw[2] * da + weight * a[2];
        g10 += dw[0] * db + weight * b[0];
        g12 += dw[2] * db + weight * b[2];
        g20 += dw[0] * dc + weight * c[0];
        g21 += dw[1] * dc + weight * c[1];
    }
    curl[0] = g21 - g12;
    curl[1] = g02 - g20;
    curl[2] = g10 - g01;
}

void mvfxCurlNoise(const mvfxNoise* noise, uint32_t seed, const double place[3], double time,
                   float out[3])
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    double moved[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        moved[k] = place[k] + (double)noise->scroll[k] * time;
    }
    double frequency = (double)noise->frequency;
    float weight = 1.0f;
    for (int32_t o = 0; o < noise->octaves; ++o)
    {
        // Each octave its own seed; the potential's octave is divided by
        // its frequency, so its curl is the noise's gradient unscaled.
        double p[3] = {moved[0] * frequency, moved[1] * frequency, moved[2] * frequency};
        float curl[3];
        CurlOctave(p, seed + (uint32_t)o * 0x9E3779B9u, curl);
        out[0] += weight * curl[0];
        out[1] += weight * curl[1];
        out[2] += weight * curl[2];
        frequency *= (double)noise->lacunarity;
        weight *= noise->gain;
    }
}
