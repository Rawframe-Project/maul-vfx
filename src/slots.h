// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A table of generation-checked slots: an id is a 1-based slot and the
// generation the slot had when the object took it, so an id outliving
// its object is told apart from the slot's next occupant.

#ifndef MAUL_VFX_SRC_SLOTS_H
#define MAUL_VFX_SRC_SLOTS_H

#include "maul-vfx/base.h"

#include <stdbool.h>

// One slot: its occupant (NULL when free), its generation, and the next
// free slot (1-based, 0 for none) while it is free.
typedef struct mvfxSlot
{
    void* object;
    uint32_t generation;
    uint32_t nextFree;
} mvfxSlot;

// The table: its slots, how many, the first free one (1-based, 0 for
// none) and how many are taken.
typedef struct mvfxSlots
{
    mvfxSlot* slots;
    uint32_t capacity;
    uint32_t firstFree;
    uint32_t taken;
} mvfxSlots;

// Allocates a table of capacity free slots, every generation 1; false
// when the allocator fails.
bool mvfxInitSlots(mvfxSlots* slots, const mvfxAllocator* allocator, uint32_t capacity);

// Returns a table's memory.
void mvfxReleaseSlots(mvfxSlots* slots, const mvfxAllocator* allocator);

// Puts object in a free slot and returns its 1-based index, with its
// generation in generationOut; 0 when every slot is taken.
uint32_t mvfxTakeSlot(mvfxSlots* slots, void* object, uint32_t* generationOut);

// The object an id names, or NULL when the id is null, out of range or
// stale.
void* mvfxSlotObject(const mvfxSlots* slots, uint32_t index1, uint32_t generation);

// Frees the slot of a live id; its generation moves on, past 0, so the
// id goes stale.
void mvfxFreeSlot(mvfxSlots* slots, uint32_t index1);

#endif
