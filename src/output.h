// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Output records: each live particle's values for a layout's fields, in
// the world (relative to a view's origin) and with values over life.

#ifndef MAUL_VFX_SRC_OUTPUT_H
#define MAUL_VFX_SRC_OUTPUT_H

#include "instance.h"
#include "layout.h"

// The order records follow: indices into the pool, or the pool's own
// order of live particles, or its reverse.
typedef struct mvfxRecordOrder
{
    const uint32_t* indices;
    bool reverse;
    int32_t live;
} mvfxRecordOrder;

// Writes the records of the first count particles of an order.
void mvfxWriteRecords(const mvfxInstance* instance, int32_t emitter, const mvfxOutputLayout* layout,
                      const double viewOrigin[3], const mvfxRecordOrder* order,
                      unsigned char* records, int64_t count);

#endif
