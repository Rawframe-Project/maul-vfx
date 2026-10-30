// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Versions and result names.

#include "maul-vfx/base.h"

mvfxVersion mvfxGetVersion(void)
{
    return (mvfxVersion){MVFX_VERSION_MAJOR, MVFX_VERSION_MINOR, MVFX_VERSION_PATCH};
}

const char* mvfxGetResultName(mvfxResult result)
{
    switch (result)
    {
    case mvfx_success:
        return "mvfx_success";
    case mvfx_errorInvalid:
        return "mvfx_errorInvalid";
    case mvfx_errorCapacity:
        return "mvfx_errorCapacity";
    case mvfx_errorStale:
        return "mvfx_errorStale";
    case mvfx_errorUnsupported:
        return "mvfx_errorUnsupported";
    case mvfx_errorPlatform:
        return "mvfx_errorPlatform";
    case mvfx_errorState:
        return "mvfx_errorState";
    case mvfx_errorVersion:
        return "mvfx_errorVersion";
    case mvfx_errorFormat:
        return "mvfx_errorFormat";
    case mvfx_errorLimit:
        return "mvfx_errorLimit";
    case mvfx_errorBusy:
        return "mvfx_errorBusy";
    case mvfx_errorMissing:
        return "mvfx_errorMissing";
    default:
        return "unknown result";
    }
}
