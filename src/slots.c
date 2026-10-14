// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Generation-checked slots with a free list.

#include "slots.h"

#include "allocator.h"

bool mvfxInitSlots(mvfxSlots* slots, const mvfxAllocator* allocator, uint32_t capacity)
{
    *slots = (mvfxSlots){0};
    mvfxSlot* table =
        mvfxAllocate(allocator, (size_t)capacity * sizeof(mvfxSlot), alignof(mvfxSlot));
    if (table == nullptr)
    {
        return false;
    }
    for (uint32_t i = 0; i < capacity; ++i)
    {
        table[i] = (mvfxSlot){.generation = 1, .nextFree = i + 2 <= capacity ? i + 2 : 0};
    }
    *slots = (mvfxSlots){.slots = table, .capacity = capacity, .firstFree = capacity > 0 ? 1 : 0};
    return true;
}

void mvfxReleaseSlots(mvfxSlots* slots, const mvfxAllocator* allocator)
{
    mvfxRelease(allocator, slots->slots, (size_t)slots->capacity * sizeof(mvfxSlot),
                alignof(mvfxSlot));
    *slots = (mvfxSlots){0};
}

uint32_t mvfxTakeSlot(mvfxSlots* slots, void* object, uint32_t* generationOut)
{
    uint32_t index1 = slots->firstFree;
    if (index1 == 0)
    {
        return 0;
    }
    mvfxSlot* slot = &slots->slots[index1 - 1];
    slots->firstFree = slot->nextFree;
    slot->object = object;
    slot->nextFree = 0;
    slots->taken += 1;
    *generationOut = slot->generation;
    return index1;
}

void* mvfxSlotObject(const mvfxSlots* slots, uint32_t index1, uint32_t generation)
{
    if (index1 == 0 || index1 > slots->capacity)
    {
        return nullptr;
    }
    const mvfxSlot* slot = &slots->slots[index1 - 1];
    return slot->generation == generation ? slot->object : nullptr;
}

void mvfxFreeSlot(mvfxSlots* slots, uint32_t index1)
{
    mvfxSlot* slot = &slots->slots[index1 - 1];
    slot->object = nullptr;
    slot->generation = slot->generation == UINT32_MAX ? 1 : slot->generation + 1;
    slot->nextFree = slots->firstFree;
    slots->firstFree = index1;
    slots->taken -= 1;
}
