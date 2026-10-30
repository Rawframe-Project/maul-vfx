// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layouts on a context.

#ifndef MAUL_VFX_SRC_LAYOUT_API_H
#define MAUL_VFX_SRC_LAYOUT_API_H

#include "layout.h"

// The live layout an id names, or NULL.
mvfxOutputLayout* mvfxContextLayout(const mvfxContext* context, mvfxLayoutId layout);

// Zeroes capacity records of a layout at records, after a refused call.
// Their size is the layout's stride, which only the context knows, so
// with a NULL context or a layout that names nothing live they are left:
// no clear could know where the caller's buffer ends.
void mvfxClearRecords(const mvfxContext* context, mvfxLayoutId layout, void* records,
                      int64_t capacity);

// Zeroes an expansion's vertices (mvfxClearRecords) and indices after a
// refused call.
void mvfxClearExpandBuffers(const mvfxContext* context, mvfxLayoutId layout,
                            const mvfxExpandBuffers* buffers);

#endif
