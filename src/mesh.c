// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Meshes: their block, edges, normals, running sums, and sampling. A
// triangle's point is Osada, Funkhouser, Chazelle and Dobkin's (2002); a
// tetrahedron's is Rocchini and Cignoni's folding of a cube (2000): both
// uniform, with a fixed count of draws.

#include "mesh.h"

#include "allocator.h"
#include "direction.h"
#include "refusal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Whether low <= x <= high; false for NaN.
static bool IsWithin(float x, float low, float high)
{
    return x >= low && x <= high;
}

mvfxResult mvfxCheckPositions(const float* positions, const float* normals, int32_t vertexCount)
{
    mvfxResult result = mvfx_success;
    for (int64_t i = 0; i < 3 * (int64_t)vertexCount; ++i)
    {
        mvfxResult position = mvfxBounded(positions[i], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT);
        if (position == mvfx_errorInvalid ||
            (normals != nullptr && !IsWithin(normals[i], -2.0f, 2.0f)))
        {
            return mvfx_errorInvalid;
        }
        result = mvfxFirst(result, position);
    }
    return result;
}

static bool AreIndicesValid(const uint32_t* indices, int64_t count, int32_t vertexCount)
{
    for (int64_t i = 0; i < count; ++i)
    {
        if (indices[i] >= (uint32_t)vertexCount)
        {
            return false;
        }
    }
    return true;
}

mvfxResult mvfxCheckMeshDef(const mvfxMeshDef* def)
{
    if (def == nullptr || def->cookie != MVFX_MESH_DEF_COOKIE || def->positions == nullptr ||
        def->vertexCount < 1 || def->triangleCount < 0 || def->tetrahedronCount < 0 ||
        (def->triangleCount > 0 && def->triangles == nullptr) ||
        (def->tetrahedronCount > 0 && def->tetrahedra == nullptr))
    {
        return mvfx_errorInvalid;
    }
    // The counts bound the arrays read below.
    if (def->vertexCount > MVFX_MAX_MESH_VERTICES || def->triangleCount > MVFX_MAX_MESH_ELEMENTS ||
        def->tetrahedronCount > MVFX_MAX_MESH_ELEMENTS)
    {
        return mvfx_errorLimit;
    }
    if (!AreIndicesValid(def->triangles, 3 * (int64_t)def->triangleCount, def->vertexCount) ||
        !AreIndicesValid(def->tetrahedra, 4 * (int64_t)def->tetrahedronCount, def->vertexCount))
    {
        return mvfx_errorInvalid;
    }
    return mvfxCheckPositions(def->positions, def->normals, def->vertexCount);
}

// Orders edges by their lower vertex, then their higher.
static int CompareEdges(const void* a, const void* b)
{
    const uint32_t* x = a;
    const uint32_t* y = b;
    if (x[0] != y[0])
    {
        return x[0] < y[0] ? -1 : 1;
    }
    return x[1] < y[1] ? -1 : (x[1] > y[1] ? 1 : 0);
}

// The triangles' edges, the lower vertex first, sorted, each once.
static int32_t BuildEdges(const uint32_t* triangles, int32_t triangleCount, uint32_t* edges)
{
    int64_t count = 0;
    for (int32_t t = 0; t < triangleCount; ++t)
    {
        for (int32_t side = 0; side < 3; ++side)
        {
            uint32_t a = triangles[3 * t + side];
            uint32_t b = triangles[3 * t + (side + 1) % 3];
            edges[2 * count] = a < b ? a : b;
            edges[2 * count + 1] = a < b ? b : a;
            ++count;
        }
    }
    qsort(edges, (size_t)count, 2 * sizeof(uint32_t), CompareEdges);
    int64_t kept = 0;
    for (int64_t i = 0; i < count; ++i)
    {
        if (kept > 0 && edges[2 * kept - 2] == edges[2 * i] &&
            edges[2 * kept - 1] == edges[2 * i + 1])
        {
            continue;
        }
        edges[2 * kept] = edges[2 * i];
        edges[2 * kept + 1] = edges[2 * i + 1];
        ++kept;
    }
    return (int32_t)kept;
}

mvfxMesh* mvfxBuildMesh(const mvfxMeshDef* def, const mvfxAllocator* allocator)
{
    // The hull first: the block keeps one only when it is built.
    mvfxHull hull;
    mvfxHullMeasure measure;
    if (mvfxMeasureHull(def->positions, def->vertexCount, allocator, &measure, &hull) !=
        mvfx_success)
    {
        return nullptr;
    }
    size_t vertices = (size_t)def->vertexCount;
    size_t triangles = (size_t)def->triangleCount;
    size_t tetrahedra = (size_t)def->tetrahedronCount;
    mvfxLayout layout = {0};
    (void)mvfxLayoutAdd(&layout, 1, sizeof(mvfxMesh), alignof(mvfxMesh));
    size_t positionsAt = mvfxLayoutAdd(&layout, 3 * vertices, sizeof(float), alignof(float));
    size_t normalsAt = mvfxLayoutAdd(&layout, 3 * vertices, sizeof(float), alignof(float));
    size_t trianglesAt = mvfxLayoutAdd(&layout, 3 * triangles, sizeof(uint32_t), alignof(uint32_t));
    size_t edgesAt = mvfxLayoutAdd(&layout, 6 * triangles, sizeof(uint32_t), alignof(uint32_t));
    size_t tetrahedraAt =
        mvfxLayoutAdd(&layout, 4 * tetrahedra, sizeof(uint32_t), alignof(uint32_t));
    size_t edgeSumsAt = mvfxLayoutAdd(&layout, 3 * triangles, sizeof(double), alignof(double));
    size_t triangleSumsAt = mvfxLayoutAdd(&layout, triangles, sizeof(double), alignof(double));
    size_t tetrahedronSumsAt = mvfxLayoutAdd(&layout, tetrahedra, sizeof(double), alignof(double));
    size_t hullAt = mvfxLayoutAdd(&layout, measure.built == mvfx_success ? 1 : 0, sizeof(mvfxHull),
                                  alignof(mvfxHull));
    if (layout.overflow)
    {
        return nullptr;
    }
    unsigned char* block = mvfxAllocate(allocator, layout.size, layout.alignment);
    if (block == nullptr)
    {
        return nullptr;
    }
    mvfxMesh* mesh = (mvfxMesh*)block;
    *mesh = (mvfxMesh){
        .positions = (float*)(block + positionsAt),
        .normals = (float*)(block + normalsAt),
        .vertexCount = def->vertexCount,
        .triangles = (uint32_t*)(block + trianglesAt),
        .triangleCount = def->triangleCount,
        .edges = (uint32_t*)(block + edgesAt),
        .tetrahedra = (uint32_t*)(block + tetrahedraAt),
        .tetrahedronCount = def->tetrahedronCount,
        .edgeSums = (double*)(block + edgeSumsAt),
        .triangleSums = (double*)(block + triangleSumsAt),
        .tetrahedronSums = (double*)(block + tetrahedronSumsAt),
        .hullMeasure = measure,
        .hull = measure.built == mvfx_success ? (mvfxHull*)(block + hullAt) : nullptr,
        .blockBytes = layout.size,
    };
    if (mesh->hull != nullptr)
    {
        *mesh->hull = hull;
    }
    if (triangles > 0)
    {
        memcpy(mesh->triangles, def->triangles, 3 * triangles * sizeof(uint32_t));
    }
    if (tetrahedra > 0)
    {
        memcpy(mesh->tetrahedra, def->tetrahedra, 4 * tetrahedra * sizeof(uint32_t));
    }
    mesh->edgeCount = BuildEdges(mesh->triangles, mesh->triangleCount, mesh->edges);
    mvfxMoveMesh(mesh, def->positions, def->normals);
    return mesh;
}

void mvfxReleaseMesh(mvfxMesh* mesh, const mvfxAllocator* allocator)
{
    mvfxRelease(allocator, mesh, mesh->blockBytes, alignof(mvfxMesh));
}

// b - a for vertices a and b.
static void Difference(const mvfxMesh* mesh, uint32_t a, uint32_t b, double out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (double)mesh->positions[3 * b + k] - (double)mesh->positions[3 * a + k];
    }
}

static void Cross(const double a[3], const double b[3], double out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static double Norm(const double v[3])
{
    return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// The vertices' normals from the triangles: each triangle's cross product
// (twice its area along its normal) summed at its vertices, normalized.
static void ComputeNormals(mvfxMesh* mesh)
{
    for (int64_t i = 0; i < 3 * (int64_t)mesh->vertexCount; ++i)
    {
        mesh->normals[i] = 0.0f;
    }
    for (int32_t t = 0; t < mesh->triangleCount; ++t)
    {
        const uint32_t* v = &mesh->triangles[3 * t];
        double ab[3];
        double ac[3];
        double cross[3];
        Difference(mesh, v[0], v[1], ab);
        Difference(mesh, v[0], v[2], ac);
        Cross(ab, ac, cross);
        for (int32_t corner = 0; corner < 3; ++corner)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                mesh->normals[3 * v[corner] + k] += (float)cross[k];
            }
        }
    }
    for (int32_t i = 0; i < mesh->vertexCount; ++i)
    {
        float* n = &mesh->normals[3 * i];
        if (!mvfxNormalize(n))
        {
            n[0] = 0.0f;
            n[1] = 0.0f;
            n[2] = 1.0f;
        }
    }
}

mvfxMeshView mvfxViewOfMesh(const mvfxMesh* mesh)
{
    return (mvfxMeshView){
        .positions = mesh->positions,
        .normals = mesh->normals,
        .vertexCount = mesh->vertexCount,
        .triangles = mesh->triangles,
        .triangleCount = mesh->triangleCount,
        .edges = mesh->edges,
        .edgeCount = mesh->edgeCount,
        .tetrahedra = mesh->tetrahedra,
        .tetrahedronCount = mesh->tetrahedronCount,
        .triangleSums = mesh->triangleSums,
        .edgeSums = mesh->edgeSums,
        .tetrahedronSums = mesh->tetrahedronSums,
        .serial = mesh->serial,
    };
}

void mvfxMoveMesh(mvfxMesh* mesh, const float* positions, const float* normals)
{
    size_t values = 3 * (size_t)mesh->vertexCount;
    memcpy(mesh->positions, positions, values * sizeof(float));
    mesh->givenNormals = normals != nullptr;
    if (normals != nullptr)
    {
        memcpy(mesh->normals, normals, values * sizeof(float));
    }
    else
    {
        ComputeNormals(mesh);
    }
    double sum = 0.0;
    for (int32_t e = 0; e < mesh->edgeCount; ++e)
    {
        double d[3];
        Difference(mesh, mesh->edges[2 * e], mesh->edges[2 * e + 1], d);
        sum += Norm(d);
        mesh->edgeSums[e] = sum;
    }
    sum = 0.0;
    for (int32_t t = 0; t < mesh->triangleCount; ++t)
    {
        const uint32_t* v = &mesh->triangles[3 * t];
        double ab[3];
        double ac[3];
        double cross[3];
        Difference(mesh, v[0], v[1], ab);
        Difference(mesh, v[0], v[2], ac);
        Cross(ab, ac, cross);
        sum += 0.5 * Norm(cross);
        mesh->triangleSums[t] = sum;
    }
    sum = 0.0;
    for (int32_t t = 0; t < mesh->tetrahedronCount; ++t)
    {
        const uint32_t* v = &mesh->tetrahedra[4 * t];
        double ab[3];
        double ac[3];
        double ad[3];
        double cross[3];
        Difference(mesh, v[0], v[1], ab);
        Difference(mesh, v[0], v[2], ac);
        Difference(mesh, v[0], v[3], ad);
        Cross(ac, ad, cross);
        sum += fabs(ab[0] * cross[0] + ab[1] * cross[1] + ab[2] * cross[2]) / 6.0;
        mesh->tetrahedronSums[t] = sum;
    }
}

bool mvfxMeshHasMode(const mvfxMesh* mesh, mvfxMeshMode mode)
{
    switch (mode)
    {
    case mvfx_meshVertices:
        return true;
    case mvfx_meshEdges:
    case mvfx_meshSurface:
        return mesh->triangleCount > 0;
    case mvfx_meshVolume:
        return mesh->tetrahedronCount > 0;
    default:
        return false;
    }
}

// A uniform binary64 in [0, 1) from two words, 53 bits, so that a choice
// among millions of elements keeps its proportions.
static double UnitDouble(uint32_t high, uint32_t low)
{
    return ((double)(high >> 5u) * 0x1p26 + (double)(low >> 6u)) * 0x1p-53;
}

// The element whose share of the running sums holds unit (in [0, 1)): the
// first whose sum passes unit times the total, so an element of measure 0
// is never chosen; the first when every measure is 0.
static int32_t Choose(const double* sums, int32_t count, double unit)
{
    if (!(sums[count - 1] > 0.0))
    {
        return 0;
    }
    double target = unit * sums[count - 1];
    int32_t low = 0;
    int32_t high = count - 1;
    while (low < high)
    {
        int32_t middle = low + (high - low) / 2;
        if (sums[middle] > target)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    return low;
}

// The weighted blend of vertices' attributes (positions or normals).
static void Blend(const float* values, const uint32_t* vertices, const float* weights,
                  int32_t count, float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        float sum = 0.0f;
        for (int32_t i = 0; i < count; ++i)
        {
            sum += weights[i] * values[3 * vertices[i] + k];
        }
        out[k] = sum;
    }
}

static void SampleTriangle(const mvfxMesh* mesh, mvfxWords4 words, float position[3],
                           float normal[3])
{
    int32_t t = Choose(mesh->triangleSums, mesh->triangleCount, UnitDouble(words.x, words.y));
    // Osada and others: (1 - sqrt r1) A + sqrt r1 (1 - r2) B + sqrt r1 r2 C.
    float root = sqrtf(mvfxUnitFloat(words.z));
    float r2 = mvfxUnitFloat(words.w);
    float weights[3] = {1.0f - root, root * (1.0f - r2), root * r2};
    const uint32_t* v = &mesh->triangles[3 * t];
    Blend(mesh->positions, v, weights, 3, position);
    Blend(mesh->normals, v, weights, 3, normal);
    if (!mvfxNormalize(normal))
    {
        double ab[3];
        double ac[3];
        double cross[3];
        Difference(mesh, v[0], v[1], ab);
        Difference(mesh, v[0], v[2], ac);
        Cross(ab, ac, cross);
        for (int32_t k = 0; k < 3; ++k)
        {
            normal[k] = (float)cross[k];
        }
        if (!mvfxNormalize(normal))
        {
            normal[0] = 0.0f;
            normal[1] = 0.0f;
            normal[2] = 1.0f;
        }
    }
}

static void SampleEdge(const mvfxMesh* mesh, mvfxWords4 words, float position[3], float normal[3])
{
    int32_t e = Choose(mesh->edgeSums, mesh->edgeCount, UnitDouble(words.x, words.y));
    float t = mvfxUnitFloat(words.z);
    float weights[2] = {1.0f - t, t};
    const uint32_t* v = &mesh->edges[2 * e];
    Blend(mesh->positions, v, weights, 2, position);
    Blend(mesh->normals, v, weights, 2, normal);
    if (!mvfxNormalize(normal))
    {
        normal[0] = 0.0f;
        normal[1] = 0.0f;
        normal[2] = 1.0f;
    }
}

static void SampleTetrahedron(const mvfxMesh* mesh, mvfxWords4 words, float position[3],
                              float normal[3])
{
    int32_t t = Choose(mesh->tetrahedronSums, mesh->tetrahedronCount, UnitDouble(words.x, words.y));
    mvfxWords4 more = mvfxPcg4dMix(words);
    float s = mvfxUnitFloat(words.z);
    float u = mvfxUnitFloat(words.w);
    float w = mvfxUnitFloat(more.x);
    // Rocchini and Cignoni: fold the unit cube into the corner tetrahedron
    // s + u + w <= 1, keeping the density uniform.
    if (s + u > 1.0f)
    {
        s = 1.0f - s;
        u = 1.0f - u;
    }
    if (u + w > 1.0f)
    {
        float swap = w;
        w = 1.0f - s - u;
        u = 1.0f - swap;
    }
    else if (s + u + w > 1.0f)
    {
        float swap = w;
        w = s + u + w - 1.0f;
        s = 1.0f - u - swap;
    }
    float weights[4] = {1.0f - s - u - w, s, u, w};
    Blend(mesh->positions, &mesh->tetrahedra[4 * t], weights, 4, position);
    mvfxRandomDirection(more.y, more.z, normal);
}

void mvfxSampleMesh(const mvfxMesh* mesh, mvfxMeshMode mode, mvfxWords4 words, float position[3],
                    float normal[3])
{
    switch (mode)
    {
    case mvfx_meshEdges:
        SampleEdge(mesh, words, position, normal);
        break;
    case mvfx_meshSurface:
        SampleTriangle(mesh, words, position, normal);
        break;
    case mvfx_meshVolume:
        SampleTetrahedron(mesh, words, position, normal);
        break;
    default:
    {
        int32_t i = (int32_t)(UnitDouble(words.x, words.y) * (double)mesh->vertexCount);
        i = i < mesh->vertexCount ? i : mesh->vertexCount - 1;
        for (int32_t k = 0; k < 3; ++k)
        {
            position[k] = mesh->positions[3 * i + k];
            normal[k] = mesh->normals[3 * i + k];
        }
        if (!mvfxNormalize(normal))
        {
            normal[0] = 0.0f;
            normal[1] = 0.0f;
            normal[2] = 1.0f;
        }
        break;
    }
    }
}
