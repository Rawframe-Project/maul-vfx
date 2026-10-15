// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles meeting each other: pairs found by the neighbor grid at
// the predicted poses, their contacts solved with both bodies' masses.

#ifndef MAUL_VFX_SRC_RIGID_PAIR_H
#define MAUL_VFX_SRC_RIGID_PAIR_H

#include "rigid_body.h"

// Finds the contacts between an emitter's count bodies at their poses into
// its pool (each pair's in a run, in the bodies' order), their speeds
// before, and marks the bodies touched; past the pool counted.
void mvfxFindBodyPairs(const mvfxRigidSolve* s, int32_t count);

// A pass over the pairs, their contacts as a body's with the world's,
// each pair's friction after its contacts; backward from the last.
void mvfxPairPass(const mvfxRigidSolve* s, mvfxContactStage stage, bool backward);

// Strikes the bodies of the pairs met faster than still.
void mvfxStrikePairs(const mvfxRigidSolve* s);

#endif
