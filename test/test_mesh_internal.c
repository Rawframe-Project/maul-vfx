// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The mesh module's own sampler at a draw no step can be made to give:
// a draw of exactly 0 passes over a first triangle of no area. Where
// each mode places particles, by area, length, count and volume, is
// tested through the step in test_mesh.c.

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
    CHECK(mvfxIsMeshDefValid(&def), "a valid mesh");
    mvfxAllocator allocator = {0};
    mvfxMesh* mesh = mvfxBuildMesh(&def, &allocator);
    float p[3];
    float n[3];
    mvfxSampleMesh(mesh, mvfx_meshSurface, (mvfxWords4){0u, 0u, 0x80000000u, 0x80000000u}, p, n);
    CHECK(p[2] > 0.0f, "a draw of 0 skips the empty first triangle");
    mvfxReleaseMesh(mesh, &allocator);
}

int main(void)
{
    TestDrawOfZero();
    return s_failures == 0 ? 0 : 1;
}
