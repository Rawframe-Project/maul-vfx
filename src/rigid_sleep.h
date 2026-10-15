// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sleeping rigid particles: still times, islands by the contacts between
// bodies, holding sleeping bodies still and waking them.

#ifndef MAUL_VFX_SRC_RIGID_SLEEP_H
#define MAUL_VFX_SRC_RIGID_SLEEP_H

#include "rigid_body.h"

// Whether body i sleeps.
static inline bool mvfxBodyAsleep(const mvfxRigidSolve* s, int32_t i)
{
    return s->sleep != nullptr && s->sleep[i] < 0.0f;
}

// After a substep's integration: each sleeping body's forces checked
// against those it fell asleep under (waking it when they changed), and
// its pose and velocities put back.
void mvfxHoldSleeping(const mvfxRigidSolve* s, int32_t first, int32_t end);

// At a step's end, over seconds: the first count bodies' still times, then
// their islands by the last substep's contacts between them, each asleep
// when all its bodies have been still the sleep time, else awake.
void mvfxSettleBodies(const mvfxRigidSolve* s, int32_t count, float seconds);

#endif
