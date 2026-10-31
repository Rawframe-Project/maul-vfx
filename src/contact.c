// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particle contacts after Macklin et al., "Unified Particle Physics for
// Real-Time Applications" (SIGGRAPH 2014): a grid of the places after
// motion; passes in which each particle reads the last pass's places and
// writes its own, moved by half of each overlap averaged over its
// contacts; then one pass of impulses on the pairs still touching that
// close. Masses are equal, so an isolated pair's momentum is kept.

#include "contact.h"

#include "appearance.h"
#include "neighbor.h"

#include <math.h>
#include <string.h>

// How far past their radii's sum two particles still touch for impulses,
// as a share of the sum; and where projection parts a pair to, halfway
// into the slop, so that a parted pair ends clear of both edges (the
// projection's touch at the sum, the impulses' past the slop) rather
// than on one, where rounding alone would decide its next contact.
#define CONTACT_SLOP 1e-3f
#define CONTACT_REST (1.0f + 0.5f * CONTACT_SLOP)

// A pass over the contacts: the grid, the places it was built from (for
// each particle's cells) and the places now.
typedef struct Pass
{
    const mvfxNeighborGrid* grid;
    float inverseCell;
    const float* built[3];
    const float* at[3];
    const float* radii;
    int32_t most;
    // The share of the radii's sum two particles touch within.
    float reach;
} Pass;

// The particles touching particle i, at most the most, in the grid's
// order; capped when it touches more.
static int32_t Touching(const Pass* p, int32_t i, int32_t out[MVFX_MAX_NEIGHBORS], bool* capped)
{
    const mvfxNeighborGrid* grid = p->grid;
    int32_t cell[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        cell[k] = mvfxGridCell(p->built[k][i], p->inverseCell);
    }
    uint32_t visited[27];
    int32_t visits = 0;
    int32_t count = 0;
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
                float d = p->at[k][i] - p->at[k][j];
                square += d * d;
            }
            float reach = (p->radii[i] + p->radii[j]) * p->reach;
            if (j == i || !(square < reach * reach))
            {
                continue;
            }
            if (count == p->most)
            {
                *capped = true;
                return count;
            }
            out[count++] = j;
        }
    }
    return count;
}

// The unit line from j to i and its length; false when they share a place.
static bool LineOf(const Pass* p, int32_t i, int32_t j, float n[3], float* length)
{
    float square = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        n[k] = p->at[k][i] - p->at[k][j];
        square += n[k] * n[k];
    }
    *length = sqrtf(square);
    for (int32_t k = 0; k < 3; ++k)
    {
        n[k] = *length > 0.0f ? n[k] / *length : 0.0f;
    }
    return *length > 0.0f;
}

// One projection pass: each particle's place moved by half of each overlap
// (to the rest distance) along the line from the other, averaged, into
// moved.
static void Project(const Pass* p, int32_t count, float* const moved[3], uint64_t* capped)
{
    int32_t touching[MVFX_MAX_NEIGHBORS];
    for (int32_t i = 0; i < count; ++i)
    {
        bool more = false;
        int32_t n = Touching(p, i, touching, &more);
        *capped += more ? 1u : 0u;
        float shift[3] = {0.0f, 0.0f, 0.0f};
        int32_t m = 0;
        for (int32_t c = 0; c < n; ++c)
        {
            int32_t j = touching[c];
            float line[3];
            float length = 0.0f;
            if (!LineOf(p, i, j, line, &length))
            {
                // No direction to part them along.
                continue;
            }
            float half = 0.5f * ((p->radii[i] + p->radii[j]) * CONTACT_REST - length);
            for (int32_t k = 0; k < 3; ++k)
            {
                shift[k] += half * line[k];
            }
            m += 1;
        }
        float share = m > 0 ? 1.0f / (float)m : 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            moved[k][i] = p->at[k][i] + shift[k] * share;
        }
    }
}

// The impulse pass: for each pair touching and closing, restitution sends
// back its share of the closing speed and friction takes its share of the
// sliding speed, half to each, averaged over each particle's; into kick.
static void Impulses(const Pass* p, const mvfxContactDef* def, int32_t count,
                     const float* const v[3], float* const kick[3])
{
    int32_t touching[MVFX_MAX_NEIGHBORS];
    for (int32_t i = 0; i < count; ++i)
    {
        bool more = false;
        int32_t n = Touching(p, i, touching, &more);
        float change[3] = {0.0f, 0.0f, 0.0f};
        int32_t m = 0;
        for (int32_t c = 0; c < n; ++c)
        {
            int32_t j = touching[c];
            float line[3];
            float length = 0.0f;
            float relative[3] = {v[0][i] - v[0][j], v[1][i] - v[1][j], v[2][i] - v[2][j]};
            if (!LineOf(p, i, j, line, &length))
            {
                continue;
            }
            float closing = relative[0] * line[0] + relative[1] * line[1] + relative[2] * line[2];
            if (!(closing < 0.0f))
            {
                continue;
            }
            for (int32_t k = 0; k < 3; ++k)
            {
                float sliding = relative[k] - closing * line[k];
                change[k] += 0.5f * (-(1.0f + def->restitution) * closing * line[k] -
                                     def->friction * sliding);
            }
            m += 1;
        }
        float share = m > 0 ? 1.0f / (float)m : 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            kick[k][i] = change[k] * share;
        }
    }
}

// Each particle's radius, and the largest.
static float Radii(const mvfxInstance* instance, int32_t emitter, float* radii)
{
    const mvfxContactDef* def = &instance->effect->emitters[emitter].contacts;
    float largest = 0.0f;
    for (int32_t i = 0; i < instance->emitters[emitter].count; ++i)
    {
        float size[3];
        mvfxSizeOf(instance, emitter, i, size);
        float r = fabsf(size[0]) * def->radiusScale;
        radii[i] = r;
        largest = r > largest ? r : largest;
    }
    return largest;
}

void mvfxSolveContacts(mvfxInstance* instance, int32_t emitter)
{
    const mvfxContactDef* def = &instance->effect->emitters[emitter].contacts;
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (!def->enable || state->count == 0)
    {
        return;
    }
    mvfxContactRoom* room = &state->contacts;
    float largest = Radii(instance, emitter, room->radii);
    float cell = 2.0f * largest * (1.0f + CONTACT_SLOP);
    if (!(cell > 0.0f && cell < INFINITY))
    {
        return;
    }
    size_t stride = (size_t)room->grid.capacity;
    float* x[3];
    float* v[3];
    float* built[3];
    float* moved[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        x[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        v[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
        built[k] = room->built + (size_t)k * stride;
        moved[k] = room->moved + (size_t)k * stride;
        memcpy(built[k], x[k], (size_t)state->count * sizeof(float));
    }
    mvfxBuildNeighborGrid(&room->grid, (const float* const*)built, nullptr, state->count, cell);
    Pass pass = {.grid = &room->grid,
                 .inverseCell = 1.0f / cell,
                 .built = {built[0], built[1], built[2]},
                 .at = {x[0], x[1], x[2]},
                 .radii = room->radii,
                 .most = def->maxContacts,
                 .reach = 1.0f};
    for (int32_t n = 0; n < def->iterations; ++n)
    {
        uint64_t capped = 0;
        Project(&pass, state->count, moved, &capped);
        // Counted once a step, in the first pass.
        state->contactsDropped += n == 0 ? capped : 0u;
        for (int32_t k = 0; k < 3; ++k)
        {
            memcpy(x[k], moved[k], (size_t)state->count * sizeof(float));
        }
    }
    pass.reach = 1.0f + CONTACT_SLOP;
    Impulses(&pass, def, state->count, (const float* const*)v, moved);
    for (int32_t k = 0; k < 3; ++k)
    {
        for (int32_t i = 0; i < state->count; ++i)
        {
            v[k][i] += moved[k][i];
        }
    }
}
