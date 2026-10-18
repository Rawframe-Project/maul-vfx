// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A mesh as shapes sample it, in one block: its vertices' positions and
// normals, its triangles, edges and tetrahedra, and for each kind of
// element the running sums (binary64) of its lengths, areas or volumes, by
// which an element is chosen with a binary search.

#ifndef MAUL_VFX_SRC_MESH_H
#define MAUL_VFX_SRC_MESH_H

#include "random.h"

#include "maul-vfx/mesh.h"
#include "maul-vfx/shape.h"

// A mesh.
typedef struct mvfxMesh
{
    float* positions;
    float* normals;
    // Whether the host gave the normals.
    bool givenNormals;
    int32_t vertexCount;
    uint32_t* triangles;
    int32_t triangleCount;
    // Each pair of vertices a triangle joins, the lower first, once.
    uint32_t* edges;
    int32_t edgeCount;
    uint32_t* tetrahedra;
    int32_t tetrahedronCount;
    // Running sums: element i's sum includes it.
    double* edgeSums;
    double* triangleSums;
    double* tetrahedronSums;
    // The effects and instances that spawn from it.
    int64_t users;
    // A serial new at its creation and each change of its positions.
    uint64_t serial;
    size_t blockBytes;
} mvfxMesh;

// A mesh as its public view gives it.
mvfxMeshView mvfxViewOfMesh(const mvfxMesh* mesh);

// Whether a mesh def is valid.
bool mvfxIsMeshDefValid(const mvfxMeshDef* def);

// Whether positions (and normals, when given) for vertexCount vertices are
// finite and in range.
bool mvfxArePositionsValid(const float* positions, const float* normals, int32_t vertexCount);

// Builds a mesh from a valid def in one block from allocator; NULL when
// the allocator fails.
mvfxMesh* mvfxBuildMesh(const mvfxMeshDef* def, const mvfxAllocator* allocator);

// Gives a mesh valid new positions and normals and rebuilds its normals
// (unless given) and sums.
void mvfxMoveMesh(mvfxMesh* mesh, const float* positions, const float* normals);

// Returns a mesh's block.
void mvfxReleaseMesh(mvfxMesh* mesh, const mvfxAllocator* allocator);

// Whether a mesh has the elements a mode needs.
bool mvfxMeshHasMode(const mvfxMesh* mesh, mvfxMeshMode mode);

// A point on or in a mesh for a mode, and the normal there, in the mesh's
// space, from four words (and words mixed from them).
void mvfxSampleMesh(const mvfxMesh* mesh, mvfxMeshMode mode, mvfxWords4 words, float position[3],
                    float normal[3]);

#endif
