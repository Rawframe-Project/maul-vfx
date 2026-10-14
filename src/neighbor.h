// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Neighbor grids: cell keys hashed into a table, a stable counting sort,
// queries over the 27 cells around a place.

#ifndef MVFX_NEIGHBOR_H
#define MVFX_NEIGHBOR_H

#include "instance.h"

// A query of an emitter's source grid from places in the emitter's space,
// which is the source's: emitters of one space in an instance share their
// anchor, which starts at the instance and moves with it at the same
// steps. Flocking's rules each have a squared radius (0 for a rule
// unused) and share the view's cosine.
typedef struct mvfxNeighborQuery
{
    const mvfxNeighborGrid* grid;
    float inverseCell;
    float radiusSquared;
    int32_t most;
    // Whether the grid is the emitter's own, so a particle leaves itself
    // out.
    bool own;
    bool flock;
    float separationSquared;
    float alignmentSquared;
    float cohesionSquared;
    float viewCosine;
} mvfxNeighborQuery;

// What a query found, in the grid's order: its neighbors' count and sums,
// whether more were left out; and for flocking, the separation's sum and
// the counts and sums of those within alignment's and cohesion's radii.
typedef struct mvfxNeighborSums
{
    int32_t count;
    float place[3];
    float velocity[3];
    bool capped;
    float apart[3];
    int32_t aligned;
    float alignedVelocity[3];
    int32_t cohered;
    float coheredPlace[3];
} mvfxNeighborSums;

// The query of a valid emitter that finds neighbors.
mvfxNeighborQuery mvfxOpenNeighborQuery(const mvfxInstance* instance, int32_t emitter);

// The neighbors of a particle at a place, heading along its velocity.
mvfxNeighborSums mvfxSearchNeighbors(const mvfxNeighborQuery* query, int32_t particle,
                                     const float place[3], const float velocity[3]);

// Stores a particle's neighbor attributes from its sums, counting a cap.
void mvfxStoreNeighbors(mvfxInstance* instance, int32_t emitter, int32_t particle,
                        const mvfxNeighborSums* sums, const float place[3],
                        const float velocity[3]);

// At an emitter's step, once its ended particles are gone: builds its grid
// from its particles (for an emitter queried), then gives each particle
// its neighbors' count and the offsets to their mean place and velocity
// (for an emitter that queries and does not steer), from its source's
// grid.
void mvfxFindNeighbors(mvfxInstance* instance, int32_t emitter);

#endif
