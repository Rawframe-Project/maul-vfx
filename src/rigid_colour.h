// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The pair pool's runs coloured for the pair passes (coloured
// Gauss-Seidel): no two runs of a colour share a body, so a colour's runs
// may be solved in any order, the GPU's at once.

#ifndef MAUL_VFX_SRC_RIGID_COLOUR_H
#define MAUL_VFX_SRC_RIGID_COLOUR_H

#include "instance.h"

// Colours the runs of state's pair pool among its first count bodies, as
// Jones and Plassmann's rule over fixed priorities gives: a run of bodies
// a below b has pcg4d of (a, b, a salt, 0)'s first word, higher first and
// the lower pool index on a tie; greedy in that order, each run takes the
// least colour no run sharing a body with it has taken. Leaves the runs'
// starts, their order for the passes (by colour, then pool order) and
// their count and colours' in state.
void mvfxColourBodyPairs(mvfxEmitterState* state, int32_t count);

#endif
