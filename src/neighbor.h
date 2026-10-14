// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Neighbor grids: cell keys hashed into a table, a stable counting sort,
// queries over the 27 cells around a place.

#ifndef MVFX_NEIGHBOR_H
#define MVFX_NEIGHBOR_H

#include "instance.h"

// At an emitter's step, once its ended particles are gone: builds its grid
// from its particles (for an emitter queried), then gives each particle
// its neighbors' count and the offsets to their mean place and velocity
// (for an emitter that queries), from its source's grid.
void mvfxFindNeighbors(mvfxInstance* instance, int32_t emitter);

#endif
