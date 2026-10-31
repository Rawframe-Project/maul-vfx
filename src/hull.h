// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Convex hulls of a mesh's vertices for rigid particles: built by
// Quickhull, faces as polygons, with the hull's volume and second moments
// about its centroid, the hull moved so the centroid is its origin.

#ifndef MAUL_VFX_SRC_HULL_H
#define MAUL_VFX_SRC_HULL_H

#include "maul-vfx/effect.h"

// The most faces and face corners a hull of MVFX_MAX_HULL_VERTICES has
// (Euler's formula: at most 2V - 4 faces and 3V - 6 edges, each edge two
// faces' corner).
#define MVFX_MAX_HULL_FACES (2 * MVFX_MAX_HULL_VERTICES - 4)
#define MVFX_MAX_HULL_LOOP  (6 * MVFX_MAX_HULL_VERTICES - 12)

// A hull about its centroid: its vertices; each face's outward unit normal
// and offset (n . x = d on it) and its corners, loop[faceStart[f]] up to
// loop[faceStart[f + 1]], counterclockwise seen from outside; its volume
// and second moments at unit density (xx, yy, zz, xy, xz, yz: the
// integrals of x x, ... over it), and its centroid in the mesh's frame.
typedef struct mvfxHull
{
    int32_t vertexCount;
    int32_t faceCount;
    float vertices[MVFX_MAX_HULL_VERTICES][3];
    float planes[MVFX_MAX_HULL_FACES][4];
    uint16_t faceStart[MVFX_MAX_HULL_FACES + 1];
    uint8_t loop[MVFX_MAX_HULL_LOOP];
    float volume;
    float moments[6];
    float centroid[3];
} mvfxHull;

// What the hull of a mesh's points is: how many vertices it has, and
// whether a rigid particle takes it.
typedef struct mvfxHullMeasure
{
    // mvfx_success with the count of the hull's vertices (0 for points
    // that span no volume), or mvfx_errorLimit, the count then 0, for a
    // hull past a build's room (at least MVFX_MAX_HULL_COUNT vertices).
    mvfxResult counted;
    int32_t count;
    // mvfx_success when the hull was built, mvfx_errorUnsupported when the
    // points span no volume, mvfx_errorLimit when the hull has more than
    // MVFX_MAX_HULL_VERTICES vertices.
    mvfxResult built;
} mvfxHullMeasure;

// Measures the hull of count points (x, y and z each, finite) into
// measure, and when it is built, builds it into out, all from one
// build; mvfx_errorCapacity when the allocator fails.
mvfxResult mvfxMeasureHull(const float* points, int32_t count, const mvfxAllocator* allocator,
                           mvfxHullMeasure* measure, mvfxHull* out);

// Builds the hull of count points into out: the build of
// mvfxMeasureHull's, or mvfx_errorCapacity when the allocator fails.
mvfxResult mvfxBuildHull(const float* points, int32_t count, const mvfxAllocator* allocator,
                         mvfxHull* out);

#endif
