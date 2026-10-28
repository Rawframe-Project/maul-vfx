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
    // The family's shared errors at their shared values, the library's
    // own from -32 down.
    static const struct
    {
        mvfxResult result;
        int32_t value;
        const char* name;
    } cases[] = {
        {mvfx_success, 0, "mvfx_success"},
        {mvfx_errorInvalid, -1, "mvfx_errorInvalid"},
        {mvfx_errorCapacity, -2, "mvfx_errorCapacity"},
        {mvfx_errorStale, -3, "mvfx_errorStale"},
        {mvfx_errorUnsupported, -4, "mvfx_errorUnsupported"},
        {mvfx_errorPlatform, -5, "mvfx_errorPlatform"},
        {mvfx_errorState, -6, "mvfx_errorState"},
        {mvfx_errorVersion, -7, "mvfx_errorVersion"},
        {mvfx_errorFormat, -8, "mvfx_errorFormat"},
        {mvfx_errorLimit, -9, "mvfx_errorLimit"},
        {mvfx_errorBusy, -10, "mvfx_errorBusy"},
        {mvfx_errorMissing, -32, "mvfx_errorMissing"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        CHECK(cases[i].result == cases[i].value, cases[i].name);
        CHECK(strcmp(mvfxResultName(cases[i].result), cases[i].name) == 0, cases[i].name);
    }
    CHECK(strcmp(mvfxResultName(12345), "unknown result") == 0, "unknown name");
    CHECK(strcmp(mvfxResultName(-11), "unknown result") == 0, "past the shared errors");
    CHECK(strcmp(mvfxResultName(-33), "unknown result") == 0, "past the library's own");
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
