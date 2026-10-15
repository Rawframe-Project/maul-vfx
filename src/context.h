// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context: the host's allocator and task system, the tables of the
// effects, instances, meshes, grids and layouts it owns, and the count of
// refused calls.

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
    mvfxSlots instances;
    mvfxSlots meshes;
    mvfxSlots grids;
    mvfxSlots layouts;
    // How many instances the context has created, which seeds those whose
    // def gives no seed.
    uint64_t instancesCreated;
    // How many grids' values it has made or changed, their serials.
    uint64_t gridValues;
    // The particles its instances hold, against limits.particles (0 for no
    // budget).
    int64_t particlesHeld;
    int64_t particleBudget;
    // Atomic: outputs for several views may count it at once.
    _Atomic uint64_t misuse;
};

// Counts one refused call against the context.
void mvfxCountMisuse(mvfxContext* context);

#endif
