// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect defs checked against every range their fields have.

#ifndef MAUL_VFX_SRC_EFFECT_CHECK_H
#define MAUL_VFX_SRC_EFFECT_CHECK_H

#include "effect.h"
#include "field_path.h"

// Whether a def and every emitter def in it are valid: mvfx_errorLimit
// for a value past one of the library's named bounds (MVFX_MAX_*,
// MVFX_MIN_*), mvfx_errorInvalid for anything else wrong. The first check
// that fails decides, and is recorded with the field it refuses below
// root, the path at the def.
mvfxResult mvfxCheckEffectDef(const mvfxFieldPath* root, const mvfxEffectDef* def);

#endif
