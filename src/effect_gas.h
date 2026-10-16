// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The checks of an effect def's gases against its emitters.

#ifndef MAUL_VFX_SRC_EFFECT_GAS_H
#define MAUL_VFX_SRC_EFFECT_GAS_H

#include "maul-vfx/effect.h"

// Whether every gas's forces are in range, every gas an emitter's forces
// follow or it splats into is one of the effect's, no gas's force follows
// a gas, every grid liquid names a liquid gas, each liquid gas is named by
// exactly one, and none is splatted into; for a def mvfxIsEffectDefValid
// found valid otherwise.
bool mvfxAreEffectGasesValid(const mvfxEffectDef* def);

// The emitter whose liquid moves on gas g as its grid: -1 for none, -2
// for more than one.
int32_t mvfxLiquidOf(const mvfxEffectDef* def, int32_t g);

#endif
