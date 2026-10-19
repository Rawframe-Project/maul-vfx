// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// One emitter's step to a new time on its instance's clock: particles
// whose life has ended are removed, keeping the rest in order, and what
// the emitter owes by the new time is born at its exact ticks. The result
// is the state stepping one tick at a time would reach (each tick's deaths
// before its births), so it does not depend on how time was split.

#ifndef MAUL_VFX_SRC_EMITTER_STEP_H
#define MAUL_VFX_SRC_EMITTER_STEP_H

#include "instance.h"

// The first tick at which a particle born at birth with the given
// inverse life has ended: the least clock after its birth where its age
// times its inverse life reaches 1, by the test the step removes it by.
int64_t mvfxDeathTick(int64_t birth, float inverseLife);

// A beam's strike: segments + 1 births at one tick, all with the first's
// lifetime.
typedef struct mvfxStrike
{
    uint64_t first;
    int64_t tick;
    float inverseLife;
    int32_t points;
} mvfxStrike;

// Moves a beam's strikes on to the instance's clock: each at the tick the
// last one's points die (its first at the emitter's start), while the
// instance has not stopped; one that dies by the clock is skipped, its
// serials still owed. Past MAX_STRIKES_A_STEP strikes the next is struck
// at the clock. Returns whether a strike lives past the clock (at most
// one does: the next is at its death), with its serials taken. The step
// strikes by it and the planner alike.
bool mvfxNextStrike(mvfxInstance* instance, int32_t emitter, mvfxStrike* strike);

// Steps emitter e of instance to the instance's clock.
void mvfxStepEmitter(mvfxInstance* instance, int32_t emitter);

#endif
