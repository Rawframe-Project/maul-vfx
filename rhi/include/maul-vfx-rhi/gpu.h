// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part, the optional target maul-vfx-rhi: effects simulated with
// Maul RHI on the host's device. A GPU object holds the part's kernels,
// whose pipelines the device builds asynchronously: the host passes the
// device's notifications to mvfxGpu_Notify until mvfxGpu_IsReady. The
// core never depends on it; it is a static library, whose functions
// MVFX_RHI_API marks.

#ifndef MAUL_VFX_RHI_GPU_H
#define MAUL_VFX_RHI_GPU_H

#include "maul-rhi/device.h"
#include "maul-rhi/frame.h"
#include "maul-vfx/base.h"

#include <stdbool.h>
#include <stdint.h>

#define MVFX_RHI_API extern

#ifdef __cplusplus
extern "C"
{
#endif

    // The part's kernels on a device.
    typedef struct mvfxGpu mvfxGpu;

    // How a GPU object is made. Build it with mvfxDefaultGpuDef.
    typedef struct mvfxGpuDef
    {
        uint32_t cookie;
        mvfxAllocator allocator;
        // The device it runs on; the object does not own it, and the device
        // outlives it.
        mrhiDevice* device;
    } mvfxGpuDef;

// The cookie of a GPU def, which its default sets and mvfxCreateGpu
// checks.
#define MVFX_GPU_DEF_COOKIE 0x6D766770u

    /// Returns the default GPU def: the C library's allocation, no device.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MVFX_RHI_API mvfxGpuDef mvfxDefaultGpuDef(void);

    /// Creates a GPU object: the part's kernels as shaders, their pipelines
    /// requested from the device, built asynchronously.
    ///
    /// @param def     The def, from mvfxDefaultGpuDef, naming a device.
    /// @param gpuOut  Receives the object; NULL on failure.
    /// @return `mvfx_success`; `mvfx_errorInvalid` for a NULL argument, a def
    ///         without its cookie or a device, or an allocator with one of
    ///         its two functions alone; `mvfx_errorCapacity` when the
    ///         allocator fails; `mvfx_errorState` when the device refuses a
    ///         kernel's shader or pipeline.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_NODISCARD MVFX_RHI_API mvfxResult mvfxCreateGpu(const mvfxGpuDef* def, mvfxGpu** gpuOut);

    /// Destroys a GPU object: its pipelines and shaders. No frame of the
    /// device may still use them.
    ///
    /// @param gpu  The object; NULL does nothing.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_RHI_API void mvfxDestroyGpu(mvfxGpu* gpu);

    /// Takes a device notification: a pipeline of the object built, or
    /// failed to build.
    ///
    /// @param gpu           The object.
    /// @param notification  A notification from mrhiNextDeviceNotification.
    /// @return Whether it answered one of the object's requests (another
    ///         owner's notifications are the host's to route).
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_RHI_API bool mvfxGpu_Notify(mvfxGpu* gpu, const mrhiDeviceNotification* notification);

    /// Returns whether every kernel's pipeline is built.
    ///
    /// @param gpu  The object.
    /// @return Whether it is ready; false for NULL or a pipeline that failed.
    /// @par Thread safety
    /// Safe from any thread; the device is used by one thread at a time.
    MVFX_RHI_API bool mvfxGpu_IsReady(const mvfxGpu* gpu);

#ifdef __cplusplus
}
#endif

#endif
