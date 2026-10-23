// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Internal invariants only: states that cannot happen unless the
// library itself is wrong. Caller input and effect files are refused
// with a status, never checked here. Debug, sanitizer and fuzzing builds
// stop at a broken invariant, where a debugger shows it; release builds
// (NDEBUG) compile the check away. The library prints nothing.

#ifndef MAUL_VFX_SRC_INVARIANT_H
#define MAUL_VFX_SRC_INVARIANT_H

#if defined(NDEBUG)
// sizeof keeps the operands used without evaluating them.
#define MVFX_ASSERT(cond) ((void)sizeof(cond))
#else
#define MVFX_ASSERT(cond) ((cond) ? (void)0 : __builtin_trap())
#endif

#endif
