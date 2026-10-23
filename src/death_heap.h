// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A binary min-heap of death ticks over caller memory, which replays an
// emitter's step in tick order when its capacity may be reached.

#ifndef MAUL_VFX_SRC_DEATH_HEAP_H
#define MAUL_VFX_SRC_DEATH_HEAP_H

#include <stdint.h>

// The heap: its ticks, how many it holds and how many it may.
typedef struct mvfxDeathHeap
{
    int64_t* ticks;
    int32_t count;
    int32_t capacity;
} mvfxDeathHeap;

// Orders the heap's count ticks into a heap.
void mvfxBuildDeathHeap(mvfxDeathHeap* heap);

// Adds a tick; the caller refuses a birth that finds the heap full.
void mvfxPushDeath(mvfxDeathHeap* heap, int64_t tick);

// Removes every tick at or before tick and returns how many it removed.
int32_t mvfxPopDeathsBy(mvfxDeathHeap* heap, int64_t tick);

#endif
