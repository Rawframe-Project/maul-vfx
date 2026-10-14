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

// Its colour (red, green, blue, alpha).
void mvfxColorOf(const mvfxInstance* instance, int32_t emitter, int32_t i, float out[4]);

// Its size along X, Y and Z.
void mvfxSizeOf(const mvfxInstance* instance, int32_t emitter, int32_t i, float out[3]);

// Its flipbook frame.
uint32_t mvfxFrameOf(const mvfxInstance* instance, int32_t emitter, int32_t i);

// Custom attribute c's components.
void mvfxCustomOf(const mvfxInstance* instance, int32_t emitter, int32_t i, int32_t c,
                  float out[4]);

#endif
