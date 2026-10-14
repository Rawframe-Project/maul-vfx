// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The effects a context owns, as the library's other parts reach them.

#ifndef MAUL_VFX_SRC_EFFECT_API_H
#define MAUL_VFX_SRC_EFFECT_API_H

#include "effect.h"

// The compiled effect an id names on the context, or NULL when the id is
// stale.
mvfxEffect* mvfxContextEffect(const mvfxContext* context, mvfxEffectId effect);

#endif
