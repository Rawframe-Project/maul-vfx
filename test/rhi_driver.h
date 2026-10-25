// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the device suites ask of the adapter they open: the driver a cell
// names in MVFX_RHI_REQUIRED, and whether it is WARP, D3D12's software
// adapter, whose shader compiler some checks expect to fail.

#ifndef MAUL_VFX_TEST_RHI_DRIVER_H
#define MAUL_VFX_TEST_RHI_DRIVER_H

#include "maul-rhi/instance.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Whether the adapter is the driver MVFX_RHI_REQUIRED names, "vulkan" or
// "d3d12", so that a cell meant for one driver cannot pass on another; any
// other value takes any adapter.
static inline bool IsRequiredDriver(mrhiInstance* instance, mrhiAdapterId adapter)
{
    const char* required = getenv("MVFX_RHI_REQUIRED");
    mrhiDriverKind driver;
    if (required != NULL && strcmp(required, "vulkan") == 0)
    {
        driver = mrhi_driverVulkan;
    }
    else if (required != NULL && strcmp(required, "d3d12") == 0)
    {
        driver = mrhi_driverD3d12;
    }
    else
    {
        return true;
    }
    mrhiAdapterInfo info;
    if (mrhiGetAdapterInfo(instance, adapter, &info) != mrhi_success || info.driver != driver)
    {
        printf("FAIL: the adapter is not the %s driver's, which MVFX_RHI_REQUIRED names\n",
               required);
        return false;
    }
    return true;
}

// Whether the adapter is WARP: D3D12's Microsoft Basic Render Driver,
// vendor 0x1414 and device 0x8C.
static inline bool IsWarp(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiAdapterInfo info;
    return mrhiGetAdapterInfo(instance, adapter, &info) == mrhi_success &&
           info.driver == mrhi_driverD3d12 && info.vendorId == 0x1414u && info.deviceId == 0x8Cu;
}

#endif // MAUL_VFX_TEST_RHI_DRIVER_H
