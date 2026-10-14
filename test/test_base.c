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

int main(void)
{
    TestVersionMatchesHeader();
    TestResultNames();
    return s_failures == 0 ? 0 : 1;
}
