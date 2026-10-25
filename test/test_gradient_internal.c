// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The baked gradient's read against its own function: a NaN place reads
// the first colour, as no public path passes one (#46).

#include "gradient.h"
#include "test_harness.h"

#include <math.h>

static void TestNotANumber(void)
{
    const mvfxColorKey colors[2] = {{0.0f, 1, 0, 0}, {1.0f, 0, 0, 1}};
    const mvfxAlphaKey alphas[2] = {{0.0f, 0.5f}, {1.0f, 1}};
    const mvfxGradient gradient = {colors, 2, alphas, 2};
    static mvfxGradientTable table;
    mvfxBakeGradient(&gradient, &table);
    float rgba[4] = {0};
    mvfxGradientTableAt(&table, NAN, rgba);
    CHECK(rgba[0] == 1.0f && rgba[1] == 0.0f && rgba[2] == 0.0f && rgba[3] == 0.5f,
          "a NaN place reads the first colour");
    mvfxGradientTableAt(&table, INFINITY, rgba);
    CHECK(rgba[0] == 0.0f && rgba[2] == 1.0f && rgba[3] == 1.0f, "infinity reads the last");
}

int main(void)
{
    TestNotANumber();
    return s_failures == 0 ? 0 : 1;
}
