// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The allocator wrapper and block layouts.

#include "allocator.h"
#include "test_harness.h"

#include <stdint.h>
#include <string.h>

static void TestZeroedAllocatorServesLargeAlignments(void)
{
    mvfxAllocator allocator = {0};
    static const size_t alignments[] = {1, 8, 16, 32, 64, 128, 256, 4096};
    for (size_t i = 0; i < sizeof alignments / sizeof alignments[0]; ++i)
    {
        for (size_t size = 1; size < 300; size += 37)
        {
            unsigned char* memory = mvfxAllocate(&allocator, size, alignments[i]);
            CHECK(memory != NULL, "allocated");
            CHECK(((uintptr_t)memory & (alignments[i] - 1)) == 0, "aligned as asked");
            // Every byte is the caller's: writing them all must not touch
            // what the release needs.
            memset(memory, 0xA5, size);
            mvfxRelease(&allocator, memory, size, alignments[i]);
        }
    }
}

static void TestReleaseOfNullDoesNothing(void)
{
    mvfxAllocator allocator = {0};
    mvfxRelease(&allocator, NULL, 64, 64);
    CHECK(true, "released NULL");
}

static void TestAllocatorValidity(void)
{
    mvfxAllocator none = {0};
    CHECK(mvfxIsAllocatorValid(&none), "zeroed is valid");
    mvfxAllocator half = {0};
    half.context = &half;
    CHECK(mvfxIsAllocatorValid(&half), "a context alone does not matter");
}

static void TestLayoutAlignsAndSums(void)
{
    mvfxLayout layout = {0};
    size_t first = mvfxLayoutAdd(&layout, 3, 1, 1);
    size_t second = mvfxLayoutAdd(&layout, 5, sizeof(float), 64);
    size_t third = mvfxLayoutAdd(&layout, 1, sizeof(uint64_t), 8);
    CHECK(first == 0, "first part at 0");
    CHECK(second == 64, "second part at the next 64");
    CHECK(third == 88, "third part after the second, aligned to 8");
    CHECK(layout.size == 96, "size covers every part");
    CHECK(layout.alignment == 64, "largest alignment kept");
    CHECK(!layout.overflow, "no overflow");
}

static void TestLayoutOverflowIsSticky(void)
{
    mvfxLayout layout = {0};
    (void)mvfxLayoutAdd(&layout, SIZE_MAX / 2, 4, 4);
    CHECK(layout.overflow, "count times size past size_t");
    CHECK(mvfxLayoutAdd(&layout, 1, 1, 1) == 0, "later parts get 0");
    CHECK(layout.overflow, "still overflowed");

    mvfxLayout padded = {.size = SIZE_MAX - 2};
    (void)mvfxLayoutAdd(&padded, 1, 1, 64);
    CHECK(padded.overflow, "padding past size_t");

    mvfxLayout ending = {.size = SIZE_MAX - 8};
    (void)mvfxLayoutAdd(&ending, 16, 1, 1);
    CHECK(ending.overflow, "end past size_t");
}

int main(void)
{
    TestZeroedAllocatorServesLargeAlignments();
    TestReleaseOfNullDoesNothing();
    TestAllocatorValidity();
    TestLayoutAlignsAndSums();
    TestLayoutOverflowIsSticky();
    return s_failures == 0 ? 0 : 1;
}
