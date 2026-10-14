// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Output records: each live particle's values for a layout's fields, in
// the world (relative to a view's origin) and with values over life.

#ifndef MAUL_VFX_SRC_OUTPUT_H
#define MAUL_VFX_SRC_OUTPUT_H

#include "instance.h"
#include "layout.h"

// Writes the records of an emitter's particles [0, count) into a buffer.
void mvfxWriteRecords(const mvfxInstance* instance, int32_t emitter, const mvfxOutputLayout* layout,
                      const double viewOrigin[3], unsigned char* records, int64_t count);

#endif
