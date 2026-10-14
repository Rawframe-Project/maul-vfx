// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A particle's initial values, drawn from pcg4d at its serial with a salt
// per purpose (record mvfx-0001): its life, its place on the emitter's
// shape and its velocity.

#ifndef MAUL_VFX_SRC_BIRTH_H
#define MAUL_VFX_SRC_BIRTH_H

#include "instance.h"

// One over the life of the particle of a serial, drawn from the emitter's
// range.
float mvfxInverseLifeOf(const mvfxInstance* instance, int32_t emitter, uint64_t serial);

// Appends a particle to the emitter: every attribute from its birth.
void mvfxAppendBirth(mvfxInstance* instance, int32_t emitter, const mvfxBirth* birth,
                     float inverseLife);

#endif
