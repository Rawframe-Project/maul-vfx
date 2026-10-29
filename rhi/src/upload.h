// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The GPU part's uploads, counted: each write into a frame's buffer adds
// its bytes to the count of the frame being recorded, which
// mvfxGpuSim_UploadBytes reports.

#ifndef MAUL_VFX_RHI_SRC_UPLOAD_H
#define MAUL_VFX_RHI_SRC_UPLOAD_H

#include "maul-rhi/encoder.h"

#include <stdbool.h>
#include <stdint.h>

// Records a write of bytes into a buffer at offset in a pass; whether the
// device took it, its bytes added to uploaded when it did.
static inline bool mvfxUpload(mrhiDevice* device, uint64_t* uploaded, mrhiPassId pass,
                              mrhiResourceId buffer, uint64_t offset, const void* data,
                              uint64_t bytes)
{
    bool written = mrhiPass_WriteBuffer(device, pass, buffer, offset, data, bytes) == mrhi_success;
    *uploaded += written ? bytes : 0u;
    return written;
}

#endif
