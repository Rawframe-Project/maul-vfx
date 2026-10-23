// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The effects a context owns, as the library's other parts reach them.

#ifndef MAUL_VFX_SRC_EFFECT_API_H
#define MAUL_VFX_SRC_EFFECT_API_H

#include "effect.h"

#include <stdbool.h>

// The compiled effect an id names on the context, or NULL when the id is
// stale.
mvfxEffect* mvfxContextEffect(const mvfxContext* context, mvfxEffectId effect);

// Creates an effect from a def on a context that takes changing calls:
// mvfxCreateEffect's work after its argument checks. A refusal a correct
// program never meets with a def of its own (an invalid def, a grid or a
// mesh that does not fit what the def asks of it) sets misuseOut; the
// caller counts it when the def is the program's, and not when it was
// read from a file, whose contents are untrusted data.
mvfxResult mvfxCreateEffectOf(mvfxContext* context, const mvfxEffectDef* def,
                              mvfxEffectId* effectOut, bool* misuseOut);

#endif
