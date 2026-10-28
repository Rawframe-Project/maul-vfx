// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's sine and cosine for every finite float, its
// exponential for every float from -104 to 89, and its power for every
// positive normal float to a set of exponents, against the oracle of
// elementary_reference.h. Minutes in an optimized build: built, and run by
// hand and on a schedule, not by the default test set.

#include "elementary_reference.h"

static void SweepSinCos(void)
{
    Tally sines = {0};
    Tally cosines = {0};
    for (uint32_t bits = 0; bits <= 0x7F7FFFFFu; ++bits)
    {
        float x = 0;
        memcpy(&x, &bits, sizeof x);
        CheckSinCos(&sines, &cosines, x);
        CheckSinCos(&sines, &cosines, -x);
    }
    Report("sine", &sines);
    Report("cosine", &cosines);
}

static void SweepExp(void)
{
    Tally tally = {0};
    for (uint32_t bits = 0; bits <= 0x42B20000u; ++bits)
    {
        float x = 0;
        memcpy(&x, &bits, sizeof x);
        CheckExp(&tally, x);
    }
    for (uint32_t bits = 0x80000000u; bits <= 0xC2D00000u; ++bits)
    {
        float x = 0;
        memcpy(&x, &bits, sizeof x);
        CheckExp(&tally, x);
    }
    Report("exponential", &tally);
}

static void SweepPow(void)
{
    static const float exponents[] = {2.0f, 0.5f, 1.0f / 2.4f, 2.4f, -1.0f, 3.0f};
    Tally tally = {0};
    for (size_t i = 0; i < sizeof exponents / sizeof exponents[0]; ++i)
    {
        for (uint32_t bits = 0x00800000u; bits < 0x7F800000u; ++bits)
        {
            float x = 0;
            memcpy(&x, &bits, sizeof x);
            CheckPow(&tally, x, exponents[i]);
        }
    }
    Report("power", &tally);
}

int main(void)
{
    SweepSinCos();
    SweepExp();
    SweepPow();
    return s_failures == 0 ? 0 : 1;
}
