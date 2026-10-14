// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Ribbons at output: an emitter's live particles chained by ribbon id
// through a stable sort, each chain's points written with their place
// along it.

#ifndef MAUL_VFX_SRC_RIBBON_H
#define MAUL_VFX_SRC_RIBBON_H

#include "instance.h"

#include "maul-vfx/output.h"

// Where an emitter's current ribbon's pinned head is, in the world: its
// shape's origin at the step's end. False when it pins none, or may no
// longer spawn.
bool mvfxRibbonHeadPlace(const mvfxInstance* instance, int32_t emitter, double out[3]);

#endif
