// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Neighbor grids. A particle's key is pcg4d of its integer cell
// coordinates masked to the table; a counting sort by key, placing from
// the last particle down, keeps particles of a key in their order, so a
// query visits its neighbors in one order on every platform.

#include "neighbor.h"

#include "random.h"

#include <math.h>
#include <string.h>

// The salt that sets grid keys apart from any other hash.
#define GRID_SALT 0x4E474244u

// The greatest cell coordinate in size: places farther out share the
// outermost cells.
#define MAX_CELL 0x1p30

static int32_t CellOf(float x, float inverseCell)
{
    double c = floor((double)x * (double)inverseCell);
    c = !(c >= -MAX_CELL) ? -MAX_CELL : c > MAX_CELL ? MAX_CELL : c;
    return (int32_t)c;
}

static uint32_t KeyOf(const int32_t cell[3], uint32_t mask)
{
    mvfxWords4 h =
        mvfxPcg4d((mvfxWords4){(uint32_t)cell[0], (uint32_t)cell[1], (uint32_t)cell[2], GRID_SALT});
    return h.x & mask;
}

static void BuildGrid(mvfxInstance* instance, int32_t emitter, float cell)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    mvfxNeighborGrid* grid = &state->grid;
    const float* p[3];
    const float* v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        v[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    float inverse = 1.0f / cell;
    uint32_t size = grid->mask + 1u;
    int32_t n = state->count;
    memset(grid->table, 0, ((size_t)size + 1u) * sizeof(int32_t));
    for (int32_t i = 0; i < n; ++i)
    {
        int32_t c[3] = {CellOf(p[0][i], inverse), CellOf(p[1][i], inverse),
                        CellOf(p[2][i], inverse)};
        grid->keys[i] = KeyOf(c, grid->mask);
        grid->table[grid->keys[i]] += 1;
    }
    // Each key's end, then from the last particle down each takes the slot
    // before its key's end, which leaves the key's start.
    int32_t sum = 0;
    for (uint32_t k = 0; k < size; ++k)
    {
        sum += grid->table[k];
        grid->table[k] = sum;
    }
    grid->table[size] = n;
    size_t stride = (size_t)grid->capacity;
    for (int32_t i = n - 1; i >= 0; --i)
    {
        int32_t j = --grid->table[grid->keys[i]];
        grid->index[j] = i;
        for (int32_t k = 0; k < 3; ++k)
        {
            grid->values[(size_t)k * stride + (size_t)j] = p[k][i];
            grid->values[(size_t)(3 + k) * stride + (size_t)j] = v[k][i];
        }
    }
}

// A query of a source's grid from a place in the querying emitter's
// space, which is the source's: emitters of one space in an instance
// share their anchor, which starts at the instance and moves with it at
// the same steps.
typedef struct Query
{
    const mvfxNeighborGrid* grid;
    float inverseCell;
    float radiusSquared;
    int32_t most;
    // The particle a query of the emitter's own grid leaves out, else -1.
    int32_t self;
} Query;

// What a query found: its neighbors' count and sums, and whether more
// were left out.
typedef struct Found
{
    int32_t count;
    float place[3];
    float velocity[3];
    bool capped;
} Found;

// Adds the neighbors among a key's entries; false once one more than the
// most is found.
static bool VisitKey(const Query* q, uint32_t key, const float place[3], Found* found)
{
    const mvfxNeighborGrid* grid = q->grid;
    size_t stride = (size_t)grid->capacity;
    for (int32_t j = grid->table[key]; j < grid->table[key + 1u]; ++j)
    {
        if (grid->index[j] == q->self)
        {
            continue;
        }
        float d[3];
        float square = 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            d[k] = grid->values[(size_t)k * stride + (size_t)j] - place[k];
            square += d[k] * d[k];
        }
        if (!(square < q->radiusSquared))
        {
            continue;
        }
        if (found->count == q->most)
        {
            found->capped = true;
            return false;
        }
        found->count += 1;
        for (int32_t k = 0; k < 3; ++k)
        {
            found->place[k] += grid->values[(size_t)k * stride + (size_t)j];
            found->velocity[k] += grid->values[(size_t)(3 + k) * stride + (size_t)j];
        }
    }
    return true;
}

// The neighbors of a place in the source's space: the 27 cells around it,
// z, then y, then x from -1 to 1, a key shared by two cells once.
static Found Search(const Query* q, const float place[3])
{
    int32_t cell[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        cell[k] = CellOf(place[k], q->inverseCell);
    }
    uint32_t visited[27];
    int32_t visits = 0;
    Found found = {0};
    for (int32_t n = 0; n < 27; ++n)
    {
        int32_t around[3] = {cell[0] + n % 3 - 1, cell[1] + n / 3 % 3 - 1, cell[2] + n / 9 - 1};
        uint32_t key = KeyOf(around, q->grid->mask);
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
        if (!VisitKey(q, key, place, &found))
        {
            break;
        }
    }
    return found;
}

static Query QueryOf(const mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxNeighborDef* neighbors = &compiled->neighbors;
    int32_t source = neighbors->source;
    Query q = {
        .grid = &instance->emitters[source].grid,
        .inverseCell = 1.0f / instance->effect->emitters[source].gridCell,
        .radiusSquared = neighbors->radius * neighbors->radius,
        .most = neighbors->maxNeighbors,
        .self = source == emitter ? 0 : -1,
    };
    return q;
}

static void QueryNeighbors(mvfxInstance* instance, int32_t emitter)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    Query q = QueryOf(instance, emitter);
    bool own = q.self == 0;
    const float* p[3];
    const float* v[3];
    float* center[3];
    float* velocity[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        v[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
        center[k] = mvfxAttributeArray(instance, emitter,
                                       (mvfxAttribute)(mvfx_attributeNeighborCenterX + k));
        velocity[k] = mvfxAttributeArray(instance, emitter,
                                         (mvfxAttribute)(mvfx_attributeNeighborVelocityX + k));
    }
    float* counts = mvfxAttributeArray(instance, emitter, mvfx_attributeNeighborCount);
    for (int32_t i = 0; i < state->count; ++i)
    {
        q.self = own ? i : -1;
        float place[3] = {p[0][i], p[1][i], p[2][i]};
        Found found = Search(&q, place);
        state->neighborsCapped += found.capped ? 1u : 0u;
        counts[i] = (float)found.count;
        float inverse = found.count > 0 ? 1.0f / (float)found.count : 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            bool any = found.count > 0;
            center[k][i] = any ? found.place[k] * inverse - place[k] : 0.0f;
            velocity[k][i] = any ? found.velocity[k] * inverse - v[k][i] : 0.0f;
        }
    }
}

void mvfxFindNeighbors(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    if (compiled->gridCell > 0.0f)
    {
        BuildGrid(instance, emitter, compiled->gridCell);
    }
    if (compiled->neighbors.enabled)
    {
        QueryNeighbors(instance, emitter);
    }
}
