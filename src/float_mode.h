// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The floating-point environment the library's results are defined in
// (docs/internals/determinism.md): round to nearest, subnormals kept. A
// host may run its threads with flush-to-zero or another rounding mode;
// every task the library runs enters this mode and leaves the thread as
// it found it.

#ifndef MAUL_VFX_SRC_FLOAT_MODE_H
#define MAUL_VFX_SRC_FLOAT_MODE_H

#include <stdint.h>

// The thread's floating-point control state as it was before
// mvfxEnterFloatMode.
typedef struct mvfxFloatMode
{
    uint64_t saved;
} mvfxFloatMode;

// Sets round to nearest with subnormals kept on the calling thread and
// returns what it replaced.
mvfxFloatMode mvfxEnterFloatMode(void);

// Restores what mvfxEnterFloatMode replaced.
void mvfxLeaveFloatMode(mvfxFloatMode mode);

#endif
