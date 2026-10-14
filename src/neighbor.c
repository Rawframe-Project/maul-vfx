// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Neighbor grids. A particle's key is pcg4d of its integer cell
// coordinates masked to the table; a counting sort by key, placing from
// the last particle down, keeps particles of a key in their order, so a
// query visits its neighbors in one order on every platform.

#include "neighbor.h"

#include "elementary.h"

#include <math.h>
#include <string.h>

void mvfxBuildNeighborGrid(mvfxNeighborGrid* grid, const float* const p[3], const float* const v[3],
                           int32_t n, float cell)
{
    float inverse = 1.0f / cell;
    uint32_t size = grid->mask + 1u;
    memset(grid->table, 0, ((size_t)size + 1u) * sizeof(int32_t));
    for (int32_t i = 0; i < n; ++i)
    {
        const int32_t c[3] = {mvfxGridCell(p[0][i], inverse), mvfxGridCell(p[1][i], inverse),
                              mvfxGridCell(p[2][i], inverse)};
        grid->keys[i] = mvfxCellKey(c, grid->mask);
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
        for (int32_t k = 0; grid->values != nullptr && k < 3; ++k)
        {
            grid->values[(size_t)k * stride + (size_t)j] = p[k][i];
            grid->values[(size_t)(3 + k) * stride + (size_t)j] = v[k][i];
        }
    }
}

static void BuildGrid(mvfxInstance* instance, int32_t emitter, float cell)
{
    const float* p[3];
    const float* v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        v[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    mvfxEmitterState* state = &instance->emitters[emitter];
    mvfxBuildNeighborGrid(&state->grid, p, v, state->count, cell);
}

// Whether a neighbor an offset d away (its squared length square) lies in
// the view about a heading.
static bool Sees(const mvfxNeighborQuery* q, const float d[3], float square, const float h[3])
{
    float heading = h[0] * h[0] + h[1] * h[1] + h[2] * h[2];
    // A particle at rest sees all: 0 against 0.
    if (q->viewCosine <= -1.0f)
    {
        return true;
    }
    float dot = d[0] * h[0] + d[1] * h[1] + d[2] * h[2];
    return dot >= q->viewCosine * sqrtf(square * heading);
}

// Adds neighbor j, an offset d from the particle, to the flocking sums of
// the rules whose radii and view take it.
static void AddFlock(const mvfxNeighborQuery* q, int32_t j, const float d[3], float square,
                     const float heading[3], mvfxNeighborSums* sums)
{
    if (!Sees(q, d, square, heading))
    {
        return;
    }
    const mvfxNeighborGrid* grid = q->grid;
    size_t stride = (size_t)grid->capacity;
    bool apart = square < q->separationSquared && square > 0.0f;
    bool aligned = square < q->alignmentSquared;
    bool cohered = square < q->cohesionSquared;
    sums->aligned += aligned ? 1 : 0;
    sums->cohered += cohered ? 1 : 0;
    for (int32_t k = 0; k < 3; ++k)
    {
        sums->apart[k] -= apart ? d[k] / square : 0.0f;
        sums->alignedVelocity[k] +=
            aligned ? grid->values[(size_t)(3 + k) * stride + (size_t)j] : 0.0f;
        sums->coheredPlace[k] += cohered ? grid->values[(size_t)k * stride + (size_t)j] : 0.0f;
    }
}

// A particle's walk: its place and heading, and what it leaves out.
typedef struct Walk
{
    const mvfxNeighborQuery* query;
    const float* place;
    const float* heading;
    int32_t self;
} Walk;

// Adds the neighbors among a key's entries; false once one more than the
// most is found.
static bool VisitKey(const Walk* w, uint32_t key, mvfxNeighborSums* sums)
{
    const mvfxNeighborQuery* q = w->query;
    const mvfxNeighborGrid* grid = q->grid;
    size_t stride = (size_t)grid->capacity;
    for (int32_t j = grid->table[key]; j < grid->table[key + 1u]; ++j)
    {
        if (grid->index[j] == w->self)
        {
            continue;
        }
        float d[3];
        float square = 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            d[k] = grid->values[(size_t)k * stride + (size_t)j] - w->place[k];
            square += d[k] * d[k];
        }
        if (!(square < q->radiusSquared))
        {
            continue;
        }
        if (sums->count == q->most)
        {
            sums->capped = true;
            return false;
        }
        sums->count += 1;
        for (int32_t k = 0; k < 3; ++k)
        {
            sums->place[k] += grid->values[(size_t)k * stride + (size_t)j];
            sums->velocity[k] += grid->values[(size_t)(3 + k) * stride + (size_t)j];
        }
        if (q->flock)
        {
            AddFlock(q, j, d, square, w->heading, sums);
        }
    }
    return true;
}

mvfxNeighborSums mvfxSearchNeighbors(const mvfxNeighborQuery* query, int32_t particle,
                                     const float place[3], const float velocity[3])
{
    Walk walk = {query, place, velocity, query->own ? particle : -1};
    int32_t cell[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        cell[k] = mvfxGridCell(place[k], query->inverseCell);
    }
    uint32_t visited[27];
    int32_t visits = 0;
    mvfxNeighborSums sums = {0};
    // The 27 cells around it, x fastest, then y, then z, a key two cells
    // share visited once.
    for (int32_t n = 0; n < 27; ++n)
    {
        int32_t around[3] = {cell[0] + n % 3 - 1, cell[1] + n / 3 % 3 - 1, cell[2] + n / 9 - 1};
        uint32_t key = mvfxCellKey(around, query->grid->mask);
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
        if (!VisitKey(&walk, key, &sums))
        {
            break;
        }
    }
    return sums;
}

// The square of a flocking rule's radius when its weight counts; 0
// otherwise.
static float RuleSquared(float weight, float radius)
{
    return weight > 0.0f ? radius * radius : 0.0f;
}

mvfxNeighborQuery mvfxOpenNeighborQuery(const mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxNeighborDef* neighbors = &compiled->neighbors;
    const mvfxSteeringDef* steering = &compiled->steering;
    int32_t source = neighbors->source;
    float sine = 0.0f;
    float cosine = 1.0f;
    mvfxSinCos(steering->viewAngle, &sine, &cosine);
    bool flock = steering->enabled && (steering->separation > 0.0f || steering->alignment > 0.0f ||
                                       steering->cohesion > 0.0f);
    return (mvfxNeighborQuery){
        .grid = &instance->emitters[source].grid,
        .inverseCell = 1.0f / instance->effect->emitters[source].gridCell,
        .radiusSquared = neighbors->radius * neighbors->radius,
        .most = neighbors->maxNeighbors,
        .own = source == emitter,
        .flock = flock,
        .separationSquared = RuleSquared(steering->separation, steering->separationRadius),
        .alignmentSquared = RuleSquared(steering->alignment, steering->alignmentRadius),
        .cohesionSquared = RuleSquared(steering->cohesion, steering->cohesionRadius),
        // pi's cosine is -1 within its rounding: a view of pi sees all.
        .viewCosine = steering->viewAngle >= 3.14159274f ? -1.0f : cosine,
    };
}

void mvfxStoreNeighbors(mvfxInstance* instance, int32_t emitter, int32_t particle,
                        const mvfxNeighborSums* sums, const float place[3], const float velocity[3])
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    state->neighborsCapped += sums->capped ? 1u : 0u;
    float* counts = mvfxAttributeArray(instance, emitter, mvfx_attributeNeighborCount);
    counts[particle] = (float)sums->count;
    bool any = sums->count > 0;
    float inverse = any ? 1.0f / (float)sums->count : 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        float* center = mvfxAttributeArray(instance, emitter,
                                           (mvfxAttribute)(mvfx_attributeNeighborCenterX + k));
        float* mean = mvfxAttributeArray(instance, emitter,
                                         (mvfxAttribute)(mvfx_attributeNeighborVelocityX + k));
        center[particle] = any ? sums->place[k] * inverse - place[k] : 0.0f;
        mean[particle] = any ? sums->velocity[k] * inverse - velocity[k] : 0.0f;
    }
}

void mvfxFindNeighbors(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    if (compiled->gridCell > 0.0f)
    {
        BuildGrid(instance, emitter, compiled->gridCell);
    }
    if (!compiled->neighbors.enabled || compiled->steering.enabled)
    {
        // Steering walks the neighbors itself (mvfxSteerEmitter).
        return;
    }
    mvfxNeighborQuery query = mvfxOpenNeighborQuery(instance, emitter);
    const float* p[3];
    const float* v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        v[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    for (int32_t i = 0; i < instance->emitters[emitter].count; ++i)
    {
        float place[3] = {p[0][i], p[1][i], p[2][i]};
        float velocity[3] = {v[0][i], v[1][i], v[2][i]};
        mvfxNeighborSums sums = mvfxSearchNeighbors(&query, i, place, velocity);
        mvfxStoreNeighbors(instance, emitter, i, &sums, place, velocity);
    }
}
