// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The checks and compiled forms of an emitter's parts beside its values:
// neighbors, steering, contacts, its pair force, plexus links and units.

#ifndef MAUL_VFX_SRC_EFFECT_PARTS_H
#define MAUL_VFX_SRC_EFFECT_PARTS_H

#include "effect.h"

// Whether every emitter's neighbors, steering, contacts, pair force, plexus
// links and units are valid against its effect, which mvfxIsEffectDefValid
// found valid otherwise.
bool mvfxAreEffectPartsValid(const mvfxEffectDef* def);

// Adds those parts to an effect mvfxCompileEffect compiled from the def:
// each emitter's copied, neighbor sources resolved to indices and grids'
// cells sized by the largest radius querying each.
void mvfxCompileEffectParts(const mvfxEffectDef* def, mvfxEffect* effect);

#endif
