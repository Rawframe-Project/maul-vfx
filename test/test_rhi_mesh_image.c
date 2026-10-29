// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A mesh's region as the GPU part writes it (rhi/src/mesh_image.h): the
// element its thresholds choose for a draw is the one the CPU path's
// sampler chooses, for random draws and for the draws on each side of
// every threshold, on triangles of uneven areas with some of none, for
// the vertices, and for a mesh of no area at all; the header, the
// vertices and the elements as laid out. No device needed.

#include "mesh.h"
#include "mesh_image.h"
#include "random.h"
#include "test_harness.h"

#include <stdlib.h>
#include <string.h>

#define TRIANGLES 9

// The element a region's thresholds choose for a 53-bit draw, as the GPU
// searches them: the first whose threshold passes the draw, the last when
// the draw reaches every one.
static int32_t Chosen(const uint32_t* region, uint64_t k)
{
    uint32_t count = region[0];
    const uint32_t* thresholds = region + region[3];
    uint32_t low = 0;
    uint32_t high = count - 1;
    while (low < high)
    {
        uint32_t middle = (low + high) / 2;
        uint64_t threshold = thresholds[2 * middle] | (uint64_t)thresholds[2 * middle + 1] << 32u;
        if (k < threshold)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    return (int32_t)low;
}

// The draw words whose first two give the 53-bit draw k, as the CPU path
// reads them (the high word's top 27 bits, then the low word's top 26).
static mvfxWords4 WordsOf(uint64_t k, uint32_t rest)
{
    return (mvfxWords4){(uint32_t)(k >> 26u) << 5u, (uint32_t)(k & 0x3FFFFFFu) << 6u, rest,
                        rest ^ 0x9E3779B9u};
}

static uint64_t DrawOf(mvfxWords4 words)
{
    return (uint64_t)(words.x >> 5u) << 26u | (words.y >> 6u);
}

static mvfxMesh* Build(const float* positions, int32_t vertices, const uint32_t* triangles,
                       int32_t count)
{
    mvfxMeshDef def = mvfxDefaultMeshDef();
    def.positions = positions;
    def.vertexCount = vertices;
    def.triangles = triangles;
    def.triangleCount = count;
    CHECK(mvfxCheckMeshDef(&def) == mvfx_success, "a valid mesh");
    mvfxAllocator allocator = {0};
    return mvfxBuildMesh(&def, &allocator);
}

static uint32_t* Region(const mvfxMesh* mesh, mvfxMeshMode mode)
{
    mvfxMeshView view = mvfxViewOfMesh(mesh);
    uint32_t* words = malloc((size_t)mvfxMeshImageBytes(&view, mode));
    mvfxMeshImage(&view, mode, words);
    return words;
}

// Whether the region's choice is the CPU sampler's for the draw words:
// each triangle t lies in x from 10 t, so the sample's place names it.
static bool SameTriangle(const mvfxMesh* mesh, const uint32_t* region, mvfxWords4 words)
{
    float p[3];
    float n[3];
    mvfxSampleMesh(mesh, mvfx_meshSurface, words, p, n);
    return (int32_t)(p[0] / 10.0f + 0.001f) == Chosen(region, DrawOf(words));
}

static void TestTriangles(void)
{
    // Triangles of uneven areas (a right triangle of legs 1 and h each),
    // the third, fourth and last of no area.
    static const float heights[TRIANGLES] = {1.0f, 3.0f, 0.0f,  0.0f, 0.01f,
                                             7.0f, 2.5f, 1e-6f, 0.0f};
    float positions[9 * TRIANGLES];
    uint32_t triangles[3 * TRIANGLES];
    for (int32_t t = 0; t < TRIANGLES; ++t)
    {
        float x = 10.0f * (float)t;
        const float corners[9] = {x, 0, 0, x + 1.0f, 0, 0, x, heights[t], 0};
        for (int32_t k = 0; k < 9; ++k)
        {
            positions[9 * t + k] = corners[k];
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            triangles[3 * t + k] = (uint32_t)(3 * t + k);
        }
    }
    mvfxMesh* mesh = Build(positions, 3 * TRIANGLES, triangles, TRIANGLES);
    uint32_t* region = Region(mesh, mvfx_meshSurface);
    bool laid = region[0] == TRIANGLES && region[1] == 3 * TRIANGLES &&
                region[2] == MVFX_MESH_HEAD + 18 * TRIANGLES &&
                region[3] == region[2] + 3 * TRIANGLES && region[region[2] + 5] == 5;
    float x4 = 0.0f;
    memcpy(&x4, &region[MVFX_MESH_HEAD + 6 * 3], sizeof x4);
    CHECK(laid && x4 == 10.0f, "the header, vertices and elements laid out");
    bool same = true;
    for (uint32_t i = 0; i < 100000; ++i)
    {
        same = same && SameTriangle(mesh, region, mvfxPcg4d((mvfxWords4){i, 77u, 5u, 1u}));
    }
    CHECK(same, "random draws choose the CPU path's triangle");
    // Every threshold and its neighbors, and the ends of the draws.
    const uint32_t* thresholds = region + region[3];
    bool edges = SameTriangle(mesh, region, WordsOf(0, 3)) &&
                 SameTriangle(mesh, region, WordsOf((UINT64_C(1) << 53u) - 1, 3));
    for (int32_t t = 0; t < TRIANGLES; ++t)
    {
        uint64_t k = thresholds[2 * t] | (uint64_t)thresholds[2 * t + 1] << 32u;
        for (int64_t d = -2; d <= 2; ++d)
        {
            int64_t at = (int64_t)k + d;
            if (at >= 0 && at < (INT64_C(1) << 53))
            {
                edges = edges && SameTriangle(mesh, region, WordsOf((uint64_t)at, 11u));
            }
        }
    }
    CHECK(edges, "the draws about every threshold choose the CPU path's triangle");
    free(region);
    mvfxAllocator allocator = {0};
    mvfxReleaseMesh(mesh, &allocator);
}

static void TestVerticesAndNone(void)
{
    // Seven vertices along X, 10 apart: the vertex chosen is the CPU's.
    static const float line[] = {0, 0,  0, 10, 0,  0, 20, 0,  0, 30, 0,
                                 0, 40, 0, 0,  50, 0, 0,  60, 0, 0};
    static const uint32_t triangles[] = {0, 1, 2, 2, 3, 4, 4, 5, 6};
    mvfxMesh* mesh = Build(line, 7, triangles, 3);
    uint32_t* region = Region(mesh, mvfx_meshVertices);
    const uint32_t* thresholds = region + region[3];
    bool same = region[0] == 7 && region[3] == region[2];
    for (uint32_t i = 0; i < 20000; ++i)
    {
        mvfxWords4 words = mvfxPcg4d((mvfxWords4){i, 3u, 9u, 2u});
        if (i < 7 * 5)
        {
            uint64_t k = thresholds[2 * (i / 5)] | (uint64_t)thresholds[2 * (i / 5) + 1] << 32u;
            int64_t at = (int64_t)k + (int64_t)(i % 5) - 2;
            words = WordsOf(at >= 0 && at < (INT64_C(1) << 53) ? (uint64_t)at : 0, i);
        }
        float p[3];
        float n[3];
        mvfxSampleMesh(mesh, mvfx_meshVertices, words, p, n);
        same = same && (int32_t)(p[0] / 10.0f + 0.001f) == Chosen(region, DrawOf(words));
    }
    CHECK(same, "the vertex chosen is the CPU path's");
    free(region);
    // Triangles all of no area: every threshold past every draw, the
    // first chosen, as the CPU path chooses.
    region = Region(mesh, mvfx_meshSurface);
    bool first = true;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        mvfxWords4 words = mvfxPcg4d((mvfxWords4){i, 8u, 8u, 8u});
        float p[3];
        float n[3];
        mvfxSampleMesh(mesh, mvfx_meshSurface, words, p, n);
        first = first && Chosen(region, DrawOf(words)) == 0 && p[0] <= 20.0f + 1e-5f;
    }
    CHECK(first, "a mesh of no area: the first triangle");
    free(region);
    mvfxAllocator allocator = {0};
    mvfxReleaseMesh(mesh, &allocator);
}

static void TestPassingDraw(void)
{
    // The least draw whose target reaches the bound, and none for a bound
    // past every target.
    uint64_t k = mvfxPassingDraw(3.0, 1.0);
    bool least = ((double)k * 0x1p-53) * 3.0 >= 1.0 && ((double)(k - 1) * 0x1p-53) * 3.0 < 1.0;
    CHECK(least, "the least draw reaching a bound");
    CHECK(mvfxPassingDraw(3.0, 0.0) == 0 && mvfxPassingDraw(1.0, 2.0) == UINT64_C(1) << 53u,
          "a bound of 0 and one past every target");
}

int main(void)
{
    TestTriangles();
    TestVerticesAndNone();
    TestPassingDraw();
    return s_failures == 0 ? 0 : 1;
}
