// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Switches for the tests and the fuzz targets, which no host needs.
// Nothing here is MVFX_API: the library's symbols are hidden but for the
// public functions, so a shared build exports none of these, and no
// header here is installed. A test reaches what it needs through these
// functions, never through the library's structs.

#ifndef MAUL_VFX_SRC_TEST_HOOKS_H
#define MAUL_VFX_SRC_TEST_HOOKS_H

#include "maul-vfx/context.h"
#include "maul-vfx/instance.h"

#include <stdint.h>

// Sets the births an emitter of a live instance has owed so far, so its
// next serials are owed's low 32 bits on: a test reaches the serials'
// 32-bit wrap without four billion births. Returns false for an id that
// names no live instance or an emitter out of range.
bool mvfxTestSetOwed(mvfxContext* context, mvfxInstanceId instance, int32_t emitter, uint64_t owed);

#endif // MAUL_VFX_SRC_TEST_HOOKS_H
