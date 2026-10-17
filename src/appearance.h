// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a particle shows, taken where it is read (output, events,
// ribbons) from its stored initial values, its age and its speed by the
// values over life, never written back.

#ifndef MAUL_VFX_SRC_APPEARANCE_H
#define MAUL_VFX_SRC_APPEARANCE_H

#include "instance.h"

// Particle i's normalized age at the instance's clock, held to [0, 1].
float mvfxNormalizedAge(const mvfxInstance* instance, int32_t emitter, int32_t i);

// The most particles a run of the functions below takes.
#define MVFX_APPEARANCE_RUN 64

// The normalized ages of a run of n particles, at most
// MVFX_APPEARANCE_RUN, given by index.
void mvfxNormalizedAges(const mvfxInstance* instance, int32_t emitter, const int32_t* run,
                        int32_t n, float out[]);

// The colours (red, green, blue, alpha) of a run of particles of those
// ages (NULL: each taken where needed).
void mvfxColorsOf(const mvfxInstance* instance, int32_t emitter, const int32_t* run, int32_t n,
                  const float ages[], float out[][4]);

// Their sizes along X, Y and Z, in the first three of each four.
void mvfxSizesOf(const mvfxInstance* instance, int32_t emitter, const int32_t* run, int32_t n,
                 const float ages[], float out[][4]);

// Its colour (red, green, blue, alpha).
void mvfxColorOf(const mvfxInstance* instance, int32_t emitter, int32_t i, float out[4]);

// Its size along X, Y and Z.
void mvfxSizeOf(const mvfxInstance* instance, int32_t emitter, int32_t i, float out[3]);

// The largest a stored float attribute is in size over an emitter's live
// particles; 1 when it is not stored (its value then).
float mvfxLargestStored(const mvfxInstance* instance, int32_t emitter, mvfxAttribute attribute);

// Its flipbook frame.
uint32_t mvfxFrameOf(const mvfxInstance* instance, int32_t emitter, int32_t i);

// Custom attribute c's components.
void mvfxCustomOf(const mvfxInstance* instance, int32_t emitter, int32_t i, int32_t c,
                  float out[4]);

#endif
