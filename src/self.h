// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Units' self-collision: points kept a thickness apart, their candidates
// found once a step, parted in each substep.

#ifndef MAUL_VFX_SRC_SELF_H
#define MAUL_VFX_SRC_SELF_H

#include "instance.h"

// Finds an emitter of units' self-collision candidates at a step's start,
// for a step of a length.
void mvfxFindSelfContacts(mvfxInstance* instance, int32_t emitter, double seconds);

// Keeps the places at a substep's start, for friction.
void mvfxSaveSelfStart(mvfxInstance* instance, int32_t emitter);

// Parts each candidate pair closer than the thickness, after a substep's
// constraints, and slows its sliding by friction.
void mvfxSolveSelfContacts(mvfxInstance* instance, int32_t emitter);

#endif
