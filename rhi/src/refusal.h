// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part's answer when its device refuses to make an object: Maul
// RHI's status where the family's shared one means the same (a device not
// ready yet is in a state a later call changes; a call from inside the
// device's allocator is busy), the platform error otherwise.

#ifndef MAUL_VFX_RHI_SRC_REFUSAL_H
#define MAUL_VFX_RHI_SRC_REFUSAL_H

#include "maul-rhi/base.h"
#include "maul-vfx/base.h"

static inline mvfxResult mvfxRhiRefusal(mrhiResult result)
{
    switch (result)
    {
    case mrhi_success:
        return mvfx_success;
    case mrhi_errorCapacity:
        return mvfx_errorCapacity;
    case mrhi_errorUnsupported:
        return mvfx_errorUnsupported;
    case mrhi_errorState:
        return mvfx_errorState;
    case mrhi_errorBusy:
        return mvfx_errorBusy;
    default:
        return mvfx_errorPlatform;
    }
}

#endif
