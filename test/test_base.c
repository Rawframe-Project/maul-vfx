// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Versions and result names.

#include "test_harness.h"

#include "maul-vfx/base.h"

#include <string.h>

static void TestVersionMatchesHeader(void)
{
    mvfxVersion version = mvfxGetVersion();
    CHECK(version.major == MVFX_VERSION_MAJOR, "major version");
    CHECK(version.minor == MVFX_VERSION_MINOR, "minor version");
    CHECK(version.patch == MVFX_VERSION_PATCH, "patch version");
}

static void TestResultNames(void)
{
    static const struct
    {
        mvfxResult result;
        const char* name;
    } cases[] = {
        {mvfx_success, "mvfx_success"},
        {mvfx_errorInvalid, "mvfx_errorInvalid"},
        {mvfx_errorCapacity, "mvfx_errorCapacity"},
        {mvfx_errorLimit, "mvfx_errorLimit"},
        {mvfx_errorVersion, "mvfx_errorVersion"},
        {mvfx_errorStale, "mvfx_errorStale"},
        {mvfx_errorBusy, "mvfx_errorBusy"},
        {mvfx_errorState, "mvfx_errorState"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        CHECK(strcmp(mvfxResultName(cases[i].result), cases[i].name) == 0, cases[i].name);
    }
    CHECK(strcmp(mvfxResultName(12345), "unknown result") == 0, "unknown name");
    CHECK(strcmp(mvfxResultName(-8), "unknown result") == 0, "one past the last error");
}

static void TestHash64KnownValues(void)
{
    // The empty input is the seed; one byte is one FNV-1a step; eight bytes
    // are one round. The values are fixed by the hash's frozen constants.
    CHECK(mvfxHash64(MVFX_HASH_INIT, NULL, 0) == MVFX_HASH_INIT, "empty input keeps the seed");
    uint8_t one = 0x61;
    CHECK(mvfxHash64(MVFX_HASH_INIT, &one, 1) == 0xAF63DC4C8601EC8Cull, "one byte");
    uint8_t eight[8] = {0};
    uint64_t round = MVFX_HASH_INIT * 0x9E3779B97F4A7C15ull;
    round ^= round >> 32;
    CHECK(mvfxHash64(MVFX_HASH_INIT, eight, 8) == round, "one zero word");
    uint8_t nine[9] = {0};
    CHECK(mvfxHash64(MVFX_HASH_INIT, nine, 9) == (round ^ 0) * 0x100000001B3ull,
          "a word and a byte");
}

int main(void)
{
    TestVersionMatchesHeader();
    TestResultNames();
    TestHash64KnownValues();
    return s_failures == 0 ? 0 : 1;
}
