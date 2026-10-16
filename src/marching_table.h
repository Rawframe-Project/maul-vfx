// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Marching cubes' and marching squares' cases (src/marching_table.c, which
// tools/gen_marching_cubes.py writes). A cube's corner i sits at (i & 1,
// i >> 1 & 1, i >> 2 & 1), inside when its value is below the level; a
// case's bit i is corner i's. Each case has one variant for each choice
// of its ambiguous faces (in face order, bit j for its j-th: set when the
// face's inside corners join), each variant its triangles (three edges
// each, 0 to 11 the cube's edges, 12 on the variant's centers in order)
// facing out of the inside, and its centers, each a vertex inside the cube
// at the mean of its edges' vertices. A square's corner i sits at (i & 1,
// i >> 1 & 1); its segments' outside lies on their right.

#ifndef MAUL_VFX_SRC_MARCHING_TABLE_H
#define MAUL_VFX_SRC_MARCHING_TABLE_H

#include <stdint.h>

// Each edge's corners, lower first: four along X, four along Y, four along
// Z.
extern const uint8_t mvfxCubeEdges[12][2];

// Each face's corners in a cycle (-X, +X, -Y, +Y, -Z, +Z): corners 0 and 2
// of the cycle are diagonal, as are 1 and 3.
extern const uint8_t mvfxCubeFaces[6][4];

// Each case's first variant; a case has one variant for each choice of
// its ambiguous faces.
extern const uint16_t mvfxCubeVariantFirst[256];

// Each variant's first triangle (and one past the last's last), and the
// triangles' edges.
extern const uint16_t mvfxCubeTriangleFirst[];
extern const uint8_t mvfxCubeTriangleEdges[];

// Each variant's first byte of centers (and one past the last's last): its
// centers in order, each its edge count, then its edges.
extern const uint16_t mvfxCubeCenterFirst[];
extern const uint8_t mvfxCubeCenterEdges[];

// Each edge's corners, lower first: bottom, top, left, right.
extern const uint8_t mvfxSquareEdges[4][2];

// Each square case's first variant (the two ambiguous cases have two:
// outside corners joined, then inside), each variant's first segment (and
// one past the last's last), and the segments' edges.
extern const uint8_t mvfxSquareVariantFirst[16];
extern const uint8_t mvfxSquareSegmentFirst[];
extern const uint8_t mvfxSquareSegmentEdges[];

#endif
