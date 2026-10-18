// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "mesh_image.h"

#include <math.h>
#include <string.h>

// A draw of 2^53, past every draw: an element never passed.
#define NEVER (UINT64_C(1) << 53u)

// The elements a mode chooses among, their running sums (NULL for the
// vertices, chosen uniformly) and the vertex indices each has.
typedef struct Elements
{
    int32_t count;
    const double* sums;
    const uint32_t* vertices;
    uint32_t per;
} Elements;

static Elements ElementsOf(const mvfxMeshView* mesh, mvfxMeshMode mode)
{
    switch (mode)
    {
    case mvfx_meshEdges:
        return (Elements){mesh->edgeCount, mesh->edgeSums, mesh->edges, 2};
    case mvfx_meshSurface:
        return (Elements){mesh->triangleCount, mesh->triangleSums, mesh->triangles, 3};
    case mvfx_meshVolume:
        return (Elements){mesh->tetrahedronCount, mesh->tetrahedronSums, mesh->tetrahedra, 4};
    default:
        return (Elements){mesh->vertexCount, nullptr, nullptr, 0};
    }
}

int64_t mvfxMeshImageBytes(const mvfxMeshView* mesh, mvfxMeshMode mode)
{
    Elements elements = ElementsOf(mesh, mode);
    return 4 * (int64_t)MVFX_MESH_HEAD + 24 * (int64_t)mesh->vertexCount +
           (4 * (int64_t)elements.per + 8) * (int64_t)elements.count;
}

// The CPU path's target for a draw: the draw's unit, exact in binary64,
// times the scale, rounded once.
static double Target(uint64_t k, double scale)
{
    return ((double)k * 0x1p-53) * scale;
}

uint64_t mvfxPassingDraw(double scale, double bound)
{
    if (!(bound > 0.0))
    {
        return 0;
    }
    // From the quotient's estimate, a few steps to the exact least draw:
    // the target is monotone in the draw.
    double estimate = ceil(bound / scale * 0x1p53);
    uint64_t k = estimate >= 0x1p53 ? NEVER : (uint64_t)fmax(estimate, 0.0);
    while (k > 0 && Target(k - 1, scale) >= bound)
    {
        --k;
    }
    while (k < NEVER && Target(k, scale) < bound)
    {
        ++k;
    }
    return k;
}

void mvfxMeshImage(const mvfxMeshView* mesh, mvfxMeshMode mode, uint32_t* words)
{
    Elements elements = ElementsOf(mesh, mode);
    uint32_t vertexWords = 6u * (uint32_t)mesh->vertexCount;
    uint32_t elementsFirst = MVFX_MESH_HEAD + vertexWords;
    uint32_t thresholdsFirst = elementsFirst + elements.per * (uint32_t)elements.count;
    words[0] = (uint32_t)elements.count;
    words[1] = (uint32_t)mesh->vertexCount;
    words[2] = elementsFirst;
    words[3] = thresholdsFirst;
    for (int32_t v = 0; v < mesh->vertexCount; ++v)
    {
        memcpy(words + MVFX_MESH_HEAD + 6 * v, mesh->positions + 3 * v, 3 * sizeof(float));
        memcpy(words + MVFX_MESH_HEAD + 6 * v + 3, mesh->normals + 3 * v, 3 * sizeof(float));
    }
    if (elements.per > 0)
    {
        memcpy(words + elementsFirst, elements.vertices,
               (size_t)elements.per * (size_t)elements.count * sizeof(uint32_t));
    }
    // Each element passed from its threshold on: by its running sum of the
    // total, or a vertex by the next index of the count (the last never,
    // the CPU path holding a draw past it to the last); every element
    // never passed when the total is 0, the first chosen.
    double total = elements.sums != nullptr ? elements.sums[elements.count - 1] : 0.0;
    uint32_t* thresholds = words + thresholdsFirst;
    for (int32_t i = 0; i < elements.count; ++i)
    {
        uint64_t k = NEVER;
        if (elements.sums == nullptr)
        {
            k = i + 1 < elements.count ? mvfxPassingDraw((double)elements.count, (double)(i + 1))
                                       : NEVER;
        }
        else if (total > 0.0)
        {
            k = mvfxPassingDraw(total, elements.sums[i]);
        }
        thresholds[2 * i] = (uint32_t)k;
        thresholds[2 * i + 1] = (uint32_t)(k >> 32u);
    }
}
