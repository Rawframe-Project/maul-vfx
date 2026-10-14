// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context: the host's allocator and task system, and the count of
// refused calls.

#ifndef MAUL_VFX_SRC_CONTEXT_H
#define MAUL_VFX_SRC_CONTEXT_H

#include "tasks.h"

#include "maul-vfx/context.h"

struct mvfxContext
{
    mvfxAllocator allocator;
    mvfxTasks tasks;
    uint64_t misuse;
};

// Counts one refused call against the context.
void mvfxCountMisuse(mvfxContext* context);

#endif
