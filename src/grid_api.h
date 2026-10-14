// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Grids on a context.

#ifndef MAUL_VFX_SRC_GRID_API_H
#define MAUL_VFX_SRC_GRID_API_H

#include "grid.h"

// The live grid an id names, or NULL.
mvfxGrid* mvfxContextGrid(const mvfxContext* context, mvfxGridId grid);

// Finds a valid collider's grid: NULL for a primitive; stale for an id
// that names no live grid; invalid (misuse) for one not of one component,
// or a heightfield's not of one point along Y.
mvfxResult mvfxFindColliderGrid(mvfxContext* context, const mvfxCollider* collider,
                                mvfxGrid** gridOut);

#endif
