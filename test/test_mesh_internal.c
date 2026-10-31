// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The mesh module's own sampler at a draw no step can be made to give:
// a draw of exactly 0 passes over a first triangle of no area; and the
// hull a mesh keeps, only when it has one. Where each mode places
// particles, by area, length, count and volume, is tested through the
// step in test_mesh.c.

#include "mesh.h"
#include "test_harness.h"

static void TestDrawOfZero(void)
{
    static const float flat[] = {0, 0, 0, 1, 0, 0, 2, 0, 0, 0, 0, 1, 1, 0, 1};
    static const uint32_t firstFlat[] = {0, 1, 2, 0, 1, 3};
    mvfxMeshDef def = mvfxDefaultMeshDef();
    def.positions = flat;
    def.vertexCount = 5;
    def.triangles = firstFlat;
    def.triangleCount = 2;
    CHECK(mvfxCheckMeshDef(&def) == mvfx_success, "a valid mesh");
    mvfxAllocator allocator = {0};
    mvfxMesh* mesh = mvfxBuildMesh(&def, &allocator);
    float p[3];
    float n[3];
    mvfxSampleMesh(mesh, mvfx_meshSurface, (mvfxWords4){0u, 0u, 0x80000000u, 0x80000000u}, p, n);
    CHECK(p[2] > 0.0f, "a draw of 0 skips the empty first triangle");
    mvfxReleaseMesh(mesh, &allocator);
}

static void TestHullKept(void)
{
    // A mesh keeps a hull only when a rigid particle takes one: a cube's
    // block holds it, a flat grid's of as many vertices holds none.
    static const float cube[] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0,
                                 0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1};
    static const float flat[] = {0, 0, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0,
                                 0, 1, 0, 1, 1, 0, 2, 1, 0, 3, 1, 0};
    mvfxMeshDef def = mvfxDefaultMeshDef();
    def.positions = cube;
    def.vertexCount = 8;
    mvfxAllocator allocator = {0};
    mvfxMesh* solid = mvfxBuildMesh(&def, &allocator);
    def.positions = flat;
    mvfxMesh* sheet = mvfxBuildMesh(&def, &allocator);
    CHECK(solid != NULL && solid->hull != NULL && solid->hullMeasure.count == 8 &&
              solid->hull->vertexCount == 8,
          "a cube's mesh keeps its hull");
    CHECK(sheet != NULL && sheet->hull == NULL &&
              sheet->hullMeasure.built == mvfx_errorUnsupported && sheet->hullMeasure.count == 0 &&
              solid->blockBytes >= sheet->blockBytes + sizeof(mvfxHull),
          "a flat mesh keeps no hull");
    mvfxReleaseMesh(solid, &allocator);
    mvfxReleaseMesh(sheet, &allocator);
}

int main(void)
{
    TestDrawOfZero();
    TestHullKept();
    return s_failures == 0 ? 0 : 1;
}
