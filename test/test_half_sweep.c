// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// binary32 to binary16 for every float, against the oracle of
// half_reference.h. A few minutes in an optimized build: built, and run
// by hand and on a schedule, not by the default test set.

#include "half_reference.h"

int main(void)
{
    uint32_t bits = 0;
    do
    {
        CheckBits(bits);
    } while (++bits != 0);
    CHECK(s_checked == 0x100000000u, "every float checked");
    return s_failures == 0 ? 0 : 1;
}
