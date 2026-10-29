// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Curl noise: three of Perlin's improved gradient noises (Perlin,
// "Improving Noise", SIGGRAPH 2002) with analytic gradients, their
// gradients at each lattice point picked by pcg4d of the point and a
// seed, and the curl of their sum over octaves (Bridson, Hourihan and
// Nordenstam, "Curl-Noise for Procedural Fluid Flow", SIGGRAPH 2007).

#ifndef MAUL_VFX_SRC_NOISE_H
#define MAUL_VFX_SRC_NOISE_H

#include "maul-vfx/effect.h"

// Whether a turbulence's noise is within its ranges: mvfx_errorLimit for
// a frequency, octaves or scroll past their named bounds,
// mvfx_errorInvalid for anything else out of range.
mvfxResult mvfxCheckNoise(const mvfxNoise* noise);

// A lattice coordinate's cell, wrapped to 32 bits, and the fraction past
// it in binary32, as the noises take them: exact for any finite
// coordinate; one that is not finite reads as cell 0, fraction 0.
uint32_t mvfxNoiseCell(double p, float* fraction);

// The three noises' values and gradients at p, in lattice units: each
// within [-1, 1] or near, 0 at every lattice point.
void mvfxGradientNoise3(const double p[3], uint32_t seed, float value[3], float gradient[3][3]);

// The curl of the noise's potential at place, in the force's space, at
// time seconds.
void mvfxCurlNoise(const mvfxNoise* noise, uint32_t seed, const double place[3], double time,
                   float out[3]);

#endif
