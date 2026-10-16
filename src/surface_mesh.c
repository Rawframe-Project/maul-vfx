// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A surface gas's mesh by marching cubes (Lorensen and Cline, SIGGRAPH
// 1987), or marching squares in 2D, between its cells' centers. A face
// whose diagonal corners are alike is decided by its bilinear saddle (the
// asymptotic decider: Nielson and Hamann, Visualization 1991), from the
// face's four values alone, so the cubes sharing it agree; the cases come
// from marching_table.c. Each sample owns the edges leaving it toward +X,
// +Y and +Z; two layers of their vertices (rows, in 2D) live in the host's
// scratch.

#include "context.h"
#include "gas.h"
#include "gas_api.h"
#include "instance.h"
#include "marching_table.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

// The most centers a cube's variant has.
#define MAX_CENTERS 4

// A walk over a surface's samples: the distance less the level, the box
// into the world, the host's buffers and origin, the edges' vertices of
// two layers (rows) of samples, and the mesh's counts.
typedef struct Mesher
{
    const float* phi;
    const int32_t* n;
    // 3, or 2 for a 2D surface.
    int32_t axes;
    float level;
    float cell;
    mvfxSpaceMap map;
    const double* origin;
    const mvfxSurfaceMeshBuffers* out;
    int32_t* layers[2];
    int64_t vertices;
    int64_t indices;
    // Whether a triangle (segment) did not fit: none after it is written.
    bool stopped;
} Mesher;

// A sample's value less the level.
static float ValueAt(const Mesher* m, const int32_t p[3])
{
    return m->phi[((int64_t)p[2] * m->n[1] + p[1]) * m->n[0] + p[0]] - m->level;
}

// The distance's gradient at a sample, in the box's frame: central
// differences inside, one-sided at the box's faces.
static void GradientAt(const Mesher* m, const int32_t p[3], float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = 0.0f;
        if (k >= m->axes)
        {
            continue;
        }
        int32_t lo[3] = {p[0], p[1], p[2]};
        int32_t hi[3] = {p[0], p[1], p[2]};
        lo[k] = p[k] > 0 ? p[k] - 1 : p[k];
        hi[k] = p[k] + 1 < m->n[k] ? p[k] + 1 : p[k];
        out[k] = (ValueAt(m, hi) - ValueAt(m, lo)) / ((float)(hi[k] - lo[k]) * m->cell);
    }
}

// Where an edge from sample p along axis crosses the level, in the box's
// frame, and the gradient there.
static void EdgePoint(const Mesher* m, const int32_t p[3], int32_t axis, float place[3],
                      float gradient[3])
{
    int32_t q[3] = {p[0], p[1], p[2]};
    q[axis] += 1;
    float a = ValueAt(m, p);
    float t = a / (a - ValueAt(m, q));
    float ga[3];
    float gb[3];
    GradientAt(m, p, ga);
    GradientAt(m, q, gb);
    for (int32_t k = 0; k < 3; ++k)
    {
        float at = (float)p[k] + (k == axis ? t : 0.0f);
        place[k] = (at + 0.5f - 0.5f * (float)m->n[k]) * m->cell;
        gradient[k] = ga[k] + t * (gb[k] - ga[k]);
    }
}

// Writes a vertex at place, with its gradient's direction as its normal,
// when the buffers hold it; its index.
static int32_t Emit(Mesher* m, const float place[3], const float gradient[3])
{
    int64_t i = m->vertices++;
    if (i >= m->out->vertexCapacity)
    {
        return (int32_t)i;
    }
    double world[3];
    mvfxUnmapPoint(&m->map, place, world);
    for (int32_t k = 0; k < 3; ++k)
    {
        m->out->positions[3 * i + k] = (float)(world[k] - m->origin[k]);
    }
    if (m->out->normals != nullptr)
    {
        float normal[3];
        mvfxUnmapVector(&m->map, gradient, normal);
        float length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        for (int32_t k = 0; k < 3; ++k)
        {
            m->out->normals[3 * i + k] = length > 0.0f ? normal[k] / length : 0.0f;
        }
    }
    return (int32_t)i;
}

// The vertex on the edge from sample p along axis, made when first met.
static int32_t EdgeVertex(Mesher* m, const int32_t p[3], int32_t axis)
{
    int32_t* layer = m->layers[p[m->axes - 1] & 1];
    int64_t at = m->axes == 3 ? (int64_t)p[1] * m->n[0] + p[0] : p[0];
    int32_t* id = &layer[at * m->axes + axis];
    if (*id < 0)
    {
        float place[3];
        float gradient[3];
        EdgePoint(m, p, axis, place, gradient);
        *id = Emit(m, place, gradient);
    }
    return *id;
}

// Cube (x, y, z)'s edge e: its sample and axis.
static int32_t CubeEdge(const int32_t cube[3], int32_t e, int32_t p[3])
{
    int32_t axis = e / 4;
    int32_t j = e % 4;
    int32_t first = axis == 0 ? 1 : 0;
    int32_t second = axis == 2 ? 1 : 2;
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = cube[k];
    }
    p[first] += j & 1;
    p[second] += j >> 1 & 1;
    return axis;
}

// Writes a triangle (segment) of count vertices when it and all before it
// fit; counts it.
static void Primitive(Mesher* m, const int32_t* ids, int32_t count)
{
    bool fits = !m->stopped && m->indices + count <= m->out->indexCapacity;
    for (int32_t i = 0; fits && i < count; ++i)
    {
        fits = ids[i] < m->out->vertexCapacity;
    }
    for (int32_t i = 0; fits && i < count; ++i)
    {
        m->out->indices[m->indices + i] = (uint32_t)ids[i];
    }
    m->stopped = !fits;
    m->indices += count;
}

// Whether a face of four values (in a cycle) joins its inside corners: its
// bilinear saddle below the level. The signs alone decide, the same in
// either cube's cycle.
static bool JoinsInside(float a, float b, float c, float d)
{
    float above = a * c - b * d;
    float below = a + c - b - d;
    return above != 0.0f && (above < 0.0f) != (below < 0.0f);
}

// A cube's case and the choice of its ambiguous faces.
static int32_t CubeVariant(const float v[8])
{
    int32_t index = 0;
    for (int32_t i = 0; i < 8; ++i)
    {
        index |= v[i] < 0.0f ? 1 << i : 0;
    }
    int32_t choice = 0;
    int32_t bit = 0;
    for (int32_t f = 0; f < 6; ++f)
    {
        const uint8_t* c = mvfxCubeFaces[f];
        bool in[4];
        for (int32_t i = 0; i < 4; ++i)
        {
            in[i] = v[c[i]] < 0.0f;
        }
        if (in[0] == in[2] && in[1] == in[3] && in[0] != in[1])
        {
            choice |= JoinsInside(v[c[0]], v[c[1]], v[c[2]], v[c[3]]) ? 1 << bit : 0;
            ++bit;
        }
    }
    return mvfxCubeVariantFirst[index] + choice;
}

// Variant v's center k of cube: the mean of its edges' points.
static int32_t CenterVertex(Mesher* m, const int32_t cube[3], int32_t v, int32_t k)
{
    const uint8_t* run = &mvfxCubeCenterEdges[mvfxCubeCenterFirst[v]];
    for (int32_t skip = 0; skip < k; ++skip)
    {
        run += 1 + run[0];
    }
    float place[3] = {0.0f, 0.0f, 0.0f};
    float gradient[3] = {0.0f, 0.0f, 0.0f};
    for (int32_t i = 0; i < run[0]; ++i)
    {
        int32_t p[3];
        int32_t axis = CubeEdge(cube, run[1 + i], p);
        float x[3];
        float g[3];
        EdgePoint(m, p, axis, x, g);
        for (int32_t j = 0; j < 3; ++j)
        {
            place[j] += x[j] / (float)run[0];
            gradient[j] += g[j] / (float)run[0];
        }
    }
    return Emit(m, place, gradient);
}

static void MarchCube(Mesher* m, const int32_t cube[3])
{
    float v[8];
    for (int32_t i = 0; i < 8; ++i)
    {
        const int32_t p[3] = {cube[0] + (i & 1), cube[1] + (i >> 1 & 1), cube[2] + (i >> 2)};
        v[i] = ValueAt(m, p);
    }
    int32_t variant = CubeVariant(v);
    int32_t centers[MAX_CENTERS] = {-1, -1, -1, -1};
    for (int32_t t = mvfxCubeTriangleFirst[variant]; t < mvfxCubeTriangleFirst[variant + 1]; ++t)
    {
        int32_t ids[3];
        for (int32_t i = 0; i < 3; ++i)
        {
            int32_t e = mvfxCubeTriangleEdges[3 * t + i];
            if (e >= 12)
            {
                int32_t k = e - 12;
                centers[k] = centers[k] >= 0 ? centers[k] : CenterVertex(m, cube, variant, k);
                ids[i] = centers[k];
                continue;
            }
            int32_t p[3];
            int32_t axis = CubeEdge(cube, e, p);
            ids[i] = EdgeVertex(m, p, axis);
        }
        Primitive(m, ids, 3);
    }
}

static void MarchSquare(Mesher* m, int32_t x, int32_t y)
{
    float v[4];
    int32_t index = 0;
    for (int32_t i = 0; i < 4; ++i)
    {
        const int32_t p[3] = {x + (i & 1), y + (i >> 1), 0};
        v[i] = ValueAt(m, p);
        index |= v[i] < 0.0f ? 1 << i : 0;
    }
    // The cycle 0, 1, 3, 2: ambiguous when 0 and 3 are alike, 1 and 2 too.
    bool ambiguous = (index == 6 || index == 9);
    int32_t variant =
        mvfxSquareVariantFirst[index] + (ambiguous && JoinsInside(v[0], v[1], v[3], v[2]) ? 1 : 0);
    for (int32_t s = mvfxSquareSegmentFirst[variant]; s < mvfxSquareSegmentFirst[variant + 1]; ++s)
    {
        int32_t ids[2];
        for (int32_t i = 0; i < 2; ++i)
        {
            // Bottom, top, left, right: along X from row y or y + 1, along
            // Y from column x or x + 1.
            int32_t e = mvfxSquareSegmentEdges[2 * s + i];
            const int32_t p[3] = {x + (e == 3 ? 1 : 0), y + (e == 1 ? 1 : 0), 0};
            ids[i] = EdgeVertex(m, p, e < 2 ? 0 : 1);
        }
        Primitive(m, ids, 2);
    }
}

// The layer (row) of samples s, its edges' vertices not yet made.
static void ClearLayer(Mesher* m, int32_t s)
{
    int64_t points = m->axes == 3 ? (int64_t)m->n[0] * m->n[1] : m->n[0];
    memset(m->layers[s & 1], 0xFF, (size_t)(points * m->axes) * sizeof(int32_t));
}

static void Walk(Mesher* m)
{
    int32_t last = m->axes - 1;
    ClearLayer(m, 0);
    ClearLayer(m, 1);
    for (int32_t s = 0; s + 1 < m->n[last]; ++s)
    {
        if (s > 0)
        {
            ClearLayer(m, s + 1);
        }
        for (int32_t y = 0; m->axes == 3 && y + 1 < m->n[1]; ++y)
        {
            for (int32_t x = 0; x + 1 < m->n[0]; ++x)
            {
                const int32_t cube[3] = {x, y, s};
                MarchCube(m, cube);
            }
        }
        for (int32_t x = 0; m->axes == 2 && x + 1 < m->n[0]; ++x)
        {
            MarchSquare(m, x, s);
        }
    }
}

// The scratch a surface gas's mesh takes.
static int64_t ScratchBytes(const mvfxCompiledGas* gas)
{
    const int32_t* n = gas->def.size;
    return n[2] > 1 ? 24 * (int64_t)n[0] * n[1] : 16 * (int64_t)n[0];
}

// The live instance with a surface gas of that index, else NULL with the
// result (misuse counted for a gas not a surface).
static const mvfxInstance* FindSurface(mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                                       mvfxResult* result)
{
    const mvfxInstance* live = mvfxFindGas(context, instance, gas, result);
    if (live != nullptr && live->effect->gases[gas].def.kind != mvfx_gasSurface)
    {
        mvfxCountMisuse(context);
        *result = mvfx_errorInvalid;
        return nullptr;
    }
    return live;
}

mvfxResult mvfxInstance_GetSurfaceMeshScratchBytes(mvfxContext* context, mvfxInstanceId instance,
                                                   int32_t gas, int64_t* bytesOut)
{
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = FindSurface(context, instance, gas, &result);
    if (live == nullptr)
    {
        return result;
    }
    if (bytesOut == nullptr)
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    *bytesOut = ScratchBytes(&live->effect->gases[gas]);
    return mvfx_success;
}

// Whether a pointer holds a buffer of size: NULL only for size 0, aligned
// to 4 bytes.
static bool IsBufferValid(const void* p, int64_t size)
{
    return size >= 0 && (p != nullptr || size == 0) && (uintptr_t)p % 4u == 0u;
}

// Whether a mesh call's arguments are in range.
static bool IsMeshValid(const double origin[3], float level, const mvfxSurfaceMeshBuffers* b,
                        const int64_t* vertexCountOut, const int64_t* indexCountOut)
{
    if (origin == nullptr || b == nullptr || vertexCountOut == nullptr ||
        indexCountOut == nullptr || !isfinite(level))
    {
        return false;
    }
    bool valid = IsBufferValid(b->positions, b->vertexCapacity) && IsBufferValid(b->normals, 0) &&
                 IsBufferValid(b->indices, b->indexCapacity) &&
                 IsBufferValid(b->scratch, b->scratchBytes);
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = fabs(origin[k]) <= 0x1p52;
    }
    return valid;
}

mvfxResult mvfxInstance_WriteSurfaceMesh(mvfxContext* context, mvfxInstanceId instance, int32_t gas,
                                         const double origin[3], float level,
                                         const mvfxSurfaceMeshBuffers* buffers,
                                         int64_t* vertexCountOut, int64_t* indexCountOut)
{
    mvfxResult result = mvfx_success;
    const mvfxInstance* live = FindSurface(context, instance, gas, &result);
    if (live == nullptr)
    {
        return result;
    }
    if (!IsMeshValid(origin, level, buffers, vertexCountOut, indexCountOut))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    const mvfxCompiledGas* compiled = &live->effect->gases[gas];
    int64_t bytes = ScratchBytes(compiled);
    if (buffers->scratchBytes < bytes)
    {
        return mvfx_errorCapacity;
    }
    const mvfxGasState* state = &live->gases[gas];
    int32_t* layer = buffers->scratch;
    int64_t half = bytes / 2 / (int64_t)sizeof(int32_t);
    Mesher m = {.phi = state->scalars[0][state->current],
                .n = compiled->def.size,
                .axes = compiled->def.size[2] > 1 ? 3 : 2,
                .level = level,
                .cell = compiled->def.cellSize,
                .map = mvfxGasMap(live, gas, mvfx_spaceWorld),
                .origin = origin,
                .out = buffers,
                .layers = {layer, layer + half}};
    Walk(&m);
    *vertexCountOut = m.vertices;
    *indexCountOut = m.indices;
    return mvfx_success;
}
