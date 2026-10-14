// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An emitter's births in tick order: its rate's and its bursts', loop by
// loop from its start delay, until its loops end or the instance stops.
//
// The rate's j-th birth (from 0) is at the first tick where the integral
// of the rate since the emitter began reaches j + 1, so a rate of 0
// spawns nothing. The integral runs on across loops (a loop adds its
// whole integral), so a fraction a loop owes carries into the next and a
// birth rounding across a loop's end keeps its tick. A burst's c-th firing
// of a loop is at its time plus c intervals, in ticks, before the loop's
// end. On one tick, bursts come first, by their index, then the rate;
// serials follow this order.

#ifndef MAUL_VFX_SRC_SPAWN_H
#define MAUL_VFX_SRC_SPAWN_H

#include "instance.h"

#include <stdbool.h>

// No tick: a source with no birth by the limit asked.
#define MVFX_NO_TICK INT64_MAX

// A working copy of an emitter's cursors, read in order and saved back.
typedef struct mvfxBirths
{
    const mvfxInstance* instance;
    int32_t emitter;
    mvfxRateCursor rate;
    mvfxBurstCursor bursts[MVFX_MAX_BURSTS];
} mvfxBirths;

// Opens an emitter's births at its cursors.
void mvfxOpenBirths(const mvfxInstance* instance, int32_t emitter, mvfxBirths* births);

// The next births at one tick not past limit: their count (at least 1),
// with the tick in tickOut; 0 when there are none by limit.
int64_t mvfxNextBirths(mvfxBirths* births, int64_t limit, int64_t* tickOut);

// Writes the cursors back to the emitter.
void mvfxSaveBirths(mvfxInstance* instance, const mvfxBirths* births);

// Whether the emitter may still spawn: it has not been stopped, its loops
// have not ended, and some source of it has births ahead.
bool mvfxHasBirthsAhead(const mvfxInstance* instance, int32_t emitter);

// The tick in a loop of the j-th rate birth (from 0), at least 0, when the
// loops before it owed their whole integrals; MVFX_NO_TICK when the rate
// never reaches it; before the running maximum is taken.
int64_t mvfxRateBirthTick(const mvfxCompiledEmitter* emitter, uint64_t loop, uint64_t j);

#endif
