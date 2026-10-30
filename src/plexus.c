// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Plexus links from a grid of the places at a step's end, of cells the
// link distance: each particle in index order takes the later ones within
// the distance in the 27 cells around it, in the grid's order.

#include "plexus.h"

#include "neighbor.h"

// The links particle i owns, after those found; past the most it owns or
// the emitter keeps, only counted.
static void OwnLinks(mvfxInstance* instance, int32_t emitter, int32_t i, const float* const p[3],
                     float inverseCell)
{
    const mvfxPlexusDef* def = &instance->effect->emitters[emitter].plexus;
    mvfxEmitterState* state = &instance->emitters[emitter];
    mvfxPlexusRoom* room = &state->plexus;
    const mvfxNeighborGrid* grid = &room->grid;
    float most = def->distance * def->distance;
    int32_t cell[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        cell[k] = mvfxGridCell(p[k][i], inverseCell);
    }
    uint32_t visited[27];
    int32_t visits = 0;
    int32_t owned = 0;
    for (int32_t n = 0; n < 27; ++n)
    {
        int32_t around[3] = {cell[0] + n % 3 - 1, cell[1] + n / 3 % 3 - 1, cell[2] + n / 9 - 1};
        uint32_t key = mvfxCellKey(around, grid->mask);
        bool seen = false;
        for (int32_t v = 0; v < visits; ++v)
        {
            seen = seen || visited[v] == key;
        }
        if (seen)
        {
            continue;
        }
        visited[visits++] = key;
        for (int32_t e = grid->table[key]; e < grid->table[key + 1u]; ++e)
        {
            int32_t j = grid->index[e];
            float square = 0.0f;
            for (int32_t k = 0; k < 3; ++k)
            {
                float d = p[k][j] - p[k][i];
                square += d * d;
            }
            if (j <= i || !(square < most))
            {
                continue;
            }
            if (owned == def->maxPerParticle || room->linkCount == def->maxLinks)
            {
                state->linksDropped += 1u;
                continue;
            }
            room->links[2 * room->linkCount] = i;
            room->links[2 * room->linkCount + 1] = j;
            room->linkCount += 1;
            owned += 1;
        }
    }
}

void mvfxFindLinks(mvfxInstance* instance, int32_t emitter)
{
    const mvfxPlexusDef* def = &instance->effect->emitters[emitter].plexus;
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (!def->enable)
    {
        return;
    }
    state->plexus.linkCount = 0;
    const float* p[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
    }
    mvfxBuildNeighborGrid(&state->plexus.grid, p, nullptr, state->count, def->distance);
    float inverseCell = 1.0f / def->distance;
    for (int32_t i = 0; i < state->count; ++i)
    {
        OwnLinks(instance, emitter, i, p, inverseCell);
    }
}
