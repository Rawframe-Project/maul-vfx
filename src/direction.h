// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Unit vectors: normalizing one, and drawing one uniformly.

#ifndef MAUL_VFX_SRC_DIRECTION_H
#define MAUL_VFX_SRC_DIRECTION_H

#include <stdbool.h>
#include <stdint.h>

// A uniformly random unit vector from two words.
void mvfxRandomDirection(uint32_t a, uint32_t b, float out[3]);

// v normalized; false (and v left) when its length is 0 or not finite.
bool mvfxNormalize(float v[3]);

// Turns v by a unit quaternion (x, y, z, w).
void mvfxRotate(const float q[4], const float v[3], float out[3]);

// The quaternion product a b: b's turn, then a's.
void mvfxMultiplyRotations(const float a[4], const float b[4], float out[4]);

// Turns a binary64 vector by a unit quaternion, in binary64.
void mvfxRotateDouble(const float q[4], const double v[3], double out[3]);

#endif
