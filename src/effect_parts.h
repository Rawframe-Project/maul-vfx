// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The checks and compiled forms of an emitter's parts beside its values:
// neighbors, steering, contacts, its pair force, plexus links and units.

#ifndef MAUL_VFX_SRC_EFFECT_PARTS_H
#define MAUL_VFX_SRC_EFFECT_PARTS_H

#include "effect.h"
#include "field_path.h"

// Whether every emitter's neighbors, steering, contacts, pair force, plexus
// links and units are valid against its effect, which mvfxCheckEffectDef
// found valid otherwise: mvfx_errorLimit for a value past a named bound,
// mvfx_errorInvalid for anything else wrong. Names the field it refuses
// below root, the path at the def.
mvfxResult mvfxCheckEffectParts(const mvfxFieldPath* root, const mvfxEffectDef* def);

// Adds those parts to an effect mvfxCompileEffect compiled from the def:
// each emitter's copied, neighbor sources resolved to indices and grids'
// cells sized by the largest radius querying each.
void mvfxCompileEffectParts(const mvfxEffectDef* def, mvfxEffect* effect);

#endif
