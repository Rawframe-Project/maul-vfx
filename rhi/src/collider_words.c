// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A collider's words as the kernels read them
// (shared/collider_numbers.wgsl), with a box's faces' offsets for the sim
// kernel's clip of a turned box.

#include "numbers.h"

#include <string.h>

// A box collider's faces' offsets, n . x = d on each, +X, -X, +Y, -Y, +Z
// and -Z of its frame: its axes turned and dotted with its center, plus
// its half size, in binary64.
static void BoxFaces(const mvfxPlannedCollider* collider, float faces[6])
{
    const double x = (double)collider->rotation[0];
    const double y = (double)collider->rotation[1];
    const double z = (double)collider->rotation[2];
    const double w = (double)collider->rotation[3];
    for (int32_t f = 0; f < 6; ++f)
    {
        double v[3] = {0.0, 0.0, 0.0};
        v[f / 2] = f % 2 == 0 ? 1.0 : -1.0;
        const double tx = 2.0 * (y * v[2] - z * v[1]);
        const double ty = 2.0 * (z * v[0] - x * v[2]);
        const double tz = 2.0 * (x * v[1] - y * v[0]);
        const double n[3] = {v[0] + w * tx + (y * tz - z * ty), v[1] + w * ty + (z * tx - x * tz),
                             v[2] + w * tz + (x * ty - y * tx)};
        double d = (double)collider->halfExtents[f / 2];
        for (int32_t k = 0; k < 3; ++k)
        {
            d += n[k] * (double)collider->center[k];
        }
        faces[f] = (float)d;
    }
}

void mvfxColliderWords(const mvfxPlannedCollider* collider, const uint32_t grid[5],
                       uint32_t words[MVFX_COLLIDER_WORDS])
{
    memset(words, 0, MVFX_COLLIDER_WORDS * sizeof(uint32_t));
    words[0] = collider->kind;
    words[1] = collider->inside ? 1u : 0u;
    mvfxFloatWords(words + 4, collider->center[0], collider->center[1], collider->center[2],
                   collider->radius);
    mvfxFloatWords(words + 8, collider->normal[0], collider->normal[1], collider->normal[2], 0.0f);
    mvfxFloatWords(words + 12, collider->halfExtents[0], collider->halfExtents[1],
                   collider->halfExtents[2], 0.0f);
    memcpy(words + 16, collider->rotation, 4 * sizeof(float));
    memcpy(words + 20, grid, 4 * sizeof(uint32_t));
    words[24] = grid[4];
    mvfxFloatWords(words + 28, collider->scale, collider->grow, 0.0f, 0.0f);
    if (collider->kind == mvfx_colliderBox)
    {
        // A box has no grid: its words hold its faces' offsets instead
        // (n . x = d on each, +X, -X, +Y, -Y, +Z and -Z of its frame), in
        // binary64 until each is rounded, for the sim kernel's clip of a
        // turned box about a body (the core's mvfxClipTurnedBox).
        float faces[6];
        BoxFaces(collider, faces);
        memcpy(words + 20, faces, sizeof faces);
    }
}
