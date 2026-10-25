// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// binary32 to binary16 for every float a binary16 grid takes (finite, at
// most 65,504 in size), against the oracle of half_reference.h. A few
// minutes in an optimized build: built, and run by hand and on a
// schedule, not by the default test set.

#include "half_reference.h"

static HalfGrid s_grid;

int main(void)
{
    OpenHalfGrid(&s_grid);
    uint64_t taken = 0;
    uint32_t bits = 0;
    do
    {
        taken += QueueHalf(&s_grid, bits) ? 1 : 0;
    } while (++bits != 0);
    CloseHalfGrid(&s_grid);
    // The floats of each sign up to 65,504 (0x477FE000), both zeros
    // among them.
    CHECK(taken == 2u * (0x477FE000u + 1u) && s_grid.checked == taken, "every float checked");
    CHECK(s_grid.differing == 0, "every float the oracle's half");
    return s_failures == 0 ? 0 : 1;
}
