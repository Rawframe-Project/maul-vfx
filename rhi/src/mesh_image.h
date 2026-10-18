// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A mesh as the GPU spawns from it: a region of words
// in an emitter's grids buffer, the host writing it again when the mesh's
// serial changes. Its header (the elements, the vertices, the elements'
// and thresholds' first words from the region's start), each vertex's
// place and normal (six words), the elements its mode reads (none for
// vertices), and each element's threshold, the least 53-bit draw that
// passes it (two words, low first), so the GPU chooses the CPU path's
// element exactly with no binary64.

#ifndef MAUL_VFX_RHI_SRC_MESH_IMAGE_H
#define MAUL_VFX_RHI_SRC_MESH_IMAGE_H

#include "maul-vfx/mesh.h"
#include "maul-vfx/shape.h"

#include <stdint.h>

// The words of a region's header.
#define MVFX_MESH_HEAD 4u

// The bytes a mesh's region takes for a mode: 16, 24 a vertex, and for
// each element its mode reads its vertex indices' 4 each and 8.
int64_t mvfxMeshImageBytes(const mvfxMeshView* mesh, mvfxMeshMode mode);

// Writes a mesh's region for a mode into words, mvfxMeshImageBytes / 4
// of them.
void mvfxMeshImage(const mvfxMeshView* mesh, mvfxMeshMode mode, uint32_t* words);

// The least 53-bit draw k whose binary64 target, k 2^-53 times scale,
// reaches bound: the CPU path's element choice passes an element of
// running sum bound (scale the total) or a vertex past bound - 1 (scale
// the vertex count) exactly from that draw on; 2^53 for none.
uint64_t mvfxPassingDraw(double scale, double bound);

#endif
