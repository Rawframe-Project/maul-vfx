// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layouts on a context.

#ifndef MAUL_VFX_SRC_LAYOUT_API_H
#define MAUL_VFX_SRC_LAYOUT_API_H

#include "layout.h"

// The live layout an id names, or NULL.
mvfxOutputLayout* mvfxContextLayout(const mvfxContext* context, mvfxLayoutId layout);

#endif
