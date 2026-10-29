// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Unit vectors: normalizing one, and drawing one uniformly.

#ifndef MAUL_VFX_SRC_DIRECTION_H
#define MAUL_VFX_SRC_DIRECTION_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// A uniformly random unit vector from two words.
void mvfxRandomDirection(uint32_t a, uint32_t b, float out[3]);

// v normalized; false (and v left) when its length is 0 or not finite.
bool mvfxNormalize(float v[3]);

// v's length, as sqrtf of its squares' sum in x, y, z order; a vector
// whose largest component reaches 2^63, where that sum may pass binary32's
// largest, is measured scaled by 2^-64 and the length scaled back, so the
// length is finite wherever it fits and every shorter vector keeps its
// bits.
static inline float mvfxLengthOf(float x, float y, float z)
{
    float most = fmaxf(fabsf(x), fmaxf(fabsf(y), fabsf(z)));
    if (!(most >= 0x1p63f))
    {
        return sqrtf(x * x + y * y + z * z);
    }
    x *= 0x1p-64f;
    y *= 0x1p-64f;
    z *= 0x1p-64f;
    return sqrtf(x * x + y * y + z * z) * 0x1p64f;
}

// Turns v by a unit quaternion (x, y, z, w); inline, as forces, collision
// and output turn a vector a particle.
static inline void mvfxRotate(const float q[4], const float v[3], float out[3])
{
    // v + 2 w (u x v) + 2 u x (u x v), u the quaternion's vector part.
    float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
    float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
    float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

// The quaternion product a b: b's turn, then a's.
void mvfxMultiplyRotations(const float a[4], const float b[4], float out[4]);

// Turns a binary64 vector by a unit quaternion, in binary64; inline, as
// output turns every record's place by it.
static inline void mvfxRotateDouble(const float q[4], const double v[3], double out[3])
{
    double x = (double)q[0];
    double y = (double)q[1];
    double z = (double)q[2];
    double w = (double)q[3];
    double tx = 2.0 * (y * v[2] - z * v[1]);
    double ty = 2.0 * (z * v[0] - x * v[2]);
    double tz = 2.0 * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}

#endif
