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

// Builds the hull of count points (x, y and z each, finite) into out;
// mvfx_errorInvalid when they span no volume, mvfx_errorLimit when the
// hull has more than MVFX_MAX_HULL_VERTICES vertices, mvfx_errorCapacity
// when the allocator fails.
mvfxResult mvfxBuildHull(const float* points, int32_t count, const mvfxAllocator* allocator,
                         mvfxHull* out);

// Counts the vertices of the hull of count points, however many up to a
// build's room (at least MVFX_MAX_HULL_COUNT); mvfx_errorInvalid when they
// span no volume, mvfx_errorLimit past the room, mvfx_errorCapacity when
// the allocator fails (out then 0).
mvfxResult mvfxCountHullVertices(const float* points, int32_t count, const mvfxAllocator* allocator,
                                 int32_t* out);

#endif
