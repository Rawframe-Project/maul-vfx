// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An effect's emitters as compiled, for a simulation elsewhere
// (maul-vfx/plan.h).

#ifndef MAUL_VFX_SRC_EMITTER_VIEW_H
#define MAUL_VFX_SRC_EMITTER_VIEW_H

#include "effect.h"

#include "maul-vfx/plan.h"

// What a compiled emitter uses beyond the plain sprite's: the
// MVFX_FEATURE_ bits.
uint64_t mvfxFeaturesOf(const mvfxCompiledEmitter* emitter);

#endif
