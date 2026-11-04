// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// How the device suites open their device, and what they ask of its
// adapter: the driver a cell names in MVFX_RHI_REQUIRED, and whether it is
// WARP, D3D12's software adapter, whose shader compiler some checks expect
// to fail.

#ifndef MAUL_VFX_TEST_RHI_DRIVER_H
#define MAUL_VFX_TEST_RHI_DRIVER_H

#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The driver MVFX_RHI_REQUIRED names, "vulkan" or "d3d12", into *driver,
// and *required whether it is set; false for any other value, which fails
// the run, so that a typo cannot turn the driver check off.
static inline bool RequiredDriver(bool* required, mrhiDriverKind* driver)
{
    const char* value = getenv("MVFX_RHI_REQUIRED");
    *required = value != NULL;
    *driver = mrhi_driverVulkan;
    if (value == NULL || strcmp(value, "vulkan") == 0)
    {
        return true;
    }
    if (strcmp(value, "d3d12") == 0)
    {
        *driver = mrhi_driverD3d12;
        return true;
    }
    printf("FAIL: MVFX_RHI_REQUIRED is \"%s\", which names no driver: set it to vulkan or "
           "d3d12, or leave it unset\n",
           value);
    return false;
}

// Whether the adapter is the driver MVFX_RHI_REQUIRED names, so that a cell
// meant for one driver cannot pass on another; any adapter when it is not
// set, none when it names no driver.
static inline bool IsRequiredDriver(mrhiInstance* instance, mrhiAdapterId adapter)
{
    bool required = false;
    mrhiDriverKind driver;
    if (!RequiredDriver(&required, &driver))
    {
        return false;
    }
    if (!required)
    {
        return true;
    }
    mrhiAdapterInfo info;
    if (mrhiAdapter_GetInfo(instance, adapter, &info) != mrhi_success || info.driver != driver)
    {
        printf("FAIL: the adapter is not the %s driver's, which MVFX_RHI_REQUIRED names\n",
               driver == mrhi_driverVulkan ? "vulkan" : "d3d12");
        return false;
    }
    return true;
}

// Whether the adapter is WARP: D3D12's Microsoft Basic Render Driver,
// vendor 0x1414 and device 0x8C.
static inline bool IsWarp(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiAdapterInfo info;
    return mrhiAdapter_GetInfo(instance, adapter, &info) == mrhi_success &&
           info.driver == mrhi_driverD3d12 && info.vendorId == 0x1414u && info.deviceId == 0x8Cu;
}

// Opens a device as def asks on the first adapter the instance's drivers
// find, software ones allowed: 0 with *instance, *adapter and *device made;
// 77 when the search succeeds and finds no adapter, so the test skips,
// unless MVFX_RHI_REQUIRED is set (1); 1 when MVFX_RHI_REQUIRED names no
// driver (before any search, so with or without an adapter), the instance
// is not made, the search fails, the adapter is not the driver
// MVFX_RHI_REQUIRED names, or its device is not made. Each result but 0
// says why. The caller destroys what was made, whatever the result.
static inline int OpenDevice(mrhiDeviceDef def, mrhiInstance** instance, mrhiAdapterId* adapter,
                             mrhiDevice** device)
{
    *instance = NULL;
    *device = NULL;
    bool required = false;
    mrhiDriverKind driver;
    if (!RequiredDriver(&required, &driver))
    {
        return 1;
    }
    mrhiInstanceDef instanceDef = mrhiDefaultInstanceDef();
    mrhiResult made = mrhiCreateInstance(&instanceDef, instance);
    if (made != mrhi_success)
    {
        printf("FAIL: the instance was not made: %s\n", mrhiGetResultName(made));
        return 1;
    }
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    search.allowSoftware = true;
    mrhiRequestId request;
    mrhiInstanceNotification record = {0};
    size_t count = 0;
    mrhiResult found = mrhiInstance_RequestAdapters(*instance, &search, &request);
    found = found == mrhi_success ? mrhiInstance_TakeNotification(*instance, &record) : found;
    found = found == mrhi_success ? record.outcome : found;
    found = found == mrhi_success ? mrhiInstance_GetAdapters(*instance, adapter, 1, &count) : found;
    if (found != mrhi_success)
    {
        printf("FAIL: the adapter search failed: %s\n", mrhiGetResultName(found));
        return 1;
    }
    if (count == 0)
    {
        if (required)
        {
            printf("FAIL: no adapter, and MVFX_RHI_REQUIRED is set\n");
            return 1;
        }
        printf("SKIP: no adapter\n");
        // hygiene: no device here, and no cell requires one
        return 77;
    }
    if (!IsRequiredDriver(*instance, *adapter))
    {
        return 1;
    }
    def.adapter = *adapter;
    mrhiResult opened = mrhiCreateDevice(*instance, &def, device, &request);
    opened = opened == mrhi_success ? mrhiInstance_TakeNotification(*instance, &record) : opened;
    opened = opened == mrhi_success ? record.outcome : opened;
    if (opened != mrhi_success)
    {
        printf("FAIL: the adapter's device was not made: %s\n", mrhiGetResultName(opened));
        return 1;
    }
    return 0;
}

#endif // MAUL_VFX_TEST_RHI_DRIVER_H
