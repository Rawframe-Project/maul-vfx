// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A binary min-heap of int64 ticks.

#include "death_heap.h"

#include "invariant.h"

static void SiftDown(mvfxDeathHeap* heap, int32_t at)
{
    int64_t* ticks = heap->ticks;
    for (;;)
    {
        int32_t left = 2 * at + 1;
        if (left >= heap->count)
        {
            return;
        }
        int32_t least = left;
        if (left + 1 < heap->count && ticks[left + 1] < ticks[left])
        {
            least = left + 1;
        }
        if (ticks[at] <= ticks[least])
        {
            return;
        }
        int64_t swap = ticks[at];
        ticks[at] = ticks[least];
        ticks[least] = swap;
        at = least;
    }
}

void mvfxBuildDeathHeap(mvfxDeathHeap* heap)
{
    for (int32_t at = heap->count / 2 - 1; at >= 0; --at)
    {
        SiftDown(heap, at);
    }
}

void mvfxPushDeath(mvfxDeathHeap* heap, int64_t tick)
{
    MVFX_ASSERT(heap->count < heap->capacity);
    int64_t* ticks = heap->ticks;
    int32_t at = heap->count;
    heap->count += 1;
    ticks[at] = tick;
    while (at > 0)
    {
        int32_t parent = (at - 1) / 2;
        if (ticks[parent] <= ticks[at])
        {
            return;
        }
        int64_t swap = ticks[at];
        ticks[at] = ticks[parent];
        ticks[parent] = swap;
        at = parent;
    }
}

int32_t mvfxPopDeathsBy(mvfxDeathHeap* heap, int64_t tick)
{
    int32_t removed = 0;
    while (heap->count > 0 && heap->ticks[0] <= tick)
    {
        heap->count -= 1;
        heap->ticks[0] = heap->ticks[heap->count];
        SiftDown(heap, 0);
        ++removed;
    }
    return removed;
}
