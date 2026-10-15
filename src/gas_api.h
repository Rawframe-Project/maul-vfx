// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gases as the host reads them: their boxes in the world.

#ifndef MAUL_VFX_SRC_GAS_API_H
#define MAUL_VFX_SRC_GAS_API_H

#include "instance.h"

#include "maul-vfx/output.h"

// The box in the world around a gas's box now.
mvfxBounds mvfxGasBounds(const mvfxInstance* instance, int32_t gas);

#endif
