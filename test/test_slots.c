// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Generation-checked slots.

#include "slots.h"
#include "test_harness.h"

static void TestTakeFreeAndStale(void)
{
    mvfxAllocator allocator = {0};
    mvfxSlots slots;
    CHECK(mvfxInitSlots(&slots, &allocator, 3), "allocated");
    int objects[4];
    uint32_t generations[4] = {0};
    uint32_t a = mvfxTakeSlot(&slots, &objects[0], &generations[0]);
    uint32_t b = mvfxTakeSlot(&slots, &objects[1], &generations[1]);
    uint32_t c = mvfxTakeSlot(&slots, &objects[2], &generations[2]);
    CHECK(a == 1 && b == 2 && c == 3 && slots.taken == 3, "slots in order");
    CHECK(mvfxTakeSlot(&slots, &objects[3], &generations[3]) == 0, "full");
    CHECK(mvfxSlotObject(&slots, b, generations[1]) == &objects[1], "found");
    CHECK(mvfxSlotObject(&slots, b, generations[1] + 1) == NULL, "another generation");
    CHECK(mvfxSlotObject(&slots, 0, 1) == NULL && mvfxSlotObject(&slots, 4, 1) == NULL,
          "null and out of range");
    mvfxFreeSlot(&slots, b);
    CHECK(mvfxSlotObject(&slots, b, generations[1]) == NULL, "stale after free");
    uint32_t again = mvfxTakeSlot(&slots, &objects[3], &generations[3]);
    CHECK(again == b && generations[3] == generations[1] + 1, "reused with the next generation");
    mvfxReleaseSlots(&slots, &allocator);
}

static void TestGenerationSkipsZero(void)
{
    mvfxAllocator allocator = {0};
    mvfxSlots slots;
    CHECK(mvfxInitSlots(&slots, &allocator, 1), "allocated");
    int object = 0;
    uint32_t generation = 0;
    slots.slots[0].generation = UINT32_MAX;
    uint32_t index1 = mvfxTakeSlot(&slots, &object, &generation);
    CHECK(generation == UINT32_MAX, "the last generation");
    mvfxFreeSlot(&slots, index1);
    CHECK(slots.slots[0].generation == 1, "wraps past 0 to 1");
    mvfxReleaseSlots(&slots, &allocator);
}

int main(void)
{
    TestTakeFreeAndStale();
    TestGenerationSkipsZero();
    return s_failures == 0 ? 0 : 1;
}
