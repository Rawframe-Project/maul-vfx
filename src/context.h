// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context: the host's allocator and task system, the table of the
// effects it owns, and the count of refused calls.

#ifndef MAUL_VFX_SRC_CONTEXT_H
#define MAUL_VFX_SRC_CONTEXT_H

#include "slots.h"
#include "tasks.h"

#include "maul-vfx/context.h"

struct mvfxContext
{
    mvfxAllocator allocator;
    mvfxTasks tasks;
    mvfxSlots effects;
    uint64_t misuse;
};

// Counts one refused call against the context.
void mvfxCountMisuse(mvfxContext* context);

#endif
