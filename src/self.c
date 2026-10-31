// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Self-collision as in Macklin et al., "Unified Particle Physics for
// Real-Time Applications" (SIGGRAPH 2014): points of a cloth are spheres
// a thickness across, and pairs that start closer than the thickness in
// the rest shape are filtered out (their constraints keep them apart).
// As in Macklin et al., "Small Steps in Physics Simulation" (SCA 2019),
// candidates are found once a step, within the thickness plus what two
// points may close in it, and each substep, after the constraints, parts
// the pairs in one Jacobi pass with Macklin's constraint averaging (as
// the particle contacts do): each pair touching, from the same places,
// shares its overlap by the points' inverse masses, then takes away its
// sliding over the substep up to friction times the overlap (Coulomb's
// law on positions); each point moves by its pairs' mean. Every point
// moves only itself, so the GPU's pass is this loop.

#include "self.h"

#include "attach.h"
#include "neighbor.h"

#include <math.h>
#include <string.h>

// The thickness in the particles' space: a world-space emitter's scales
// with its instance, as its rest lengths do.
static float ThicknessOf(const mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    float scale = compiled->space == mvfx_spaceWorld ? instance->to.scale : 1.0f;
    return compiled->unit.selfThickness * scale;
}

// Whether points i and j of one unit are apart by less than the
// thickness at rest; points of different units never are.
static bool IsNearAtRest(const mvfxInstance* instance, int32_t emitter, int32_t i, int32_t j,
                         float thickness)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    int32_t points = compiled->unitPoints;
    if (i / points != j / points)
    {
        return false;
    }
    int32_t width = compiled->unitWidth;
    int32_t a = i % points;
    int32_t b = j % points;
    float across = (float)(b % width - a % width);
    int32_t rows = b / width - a / width;
    float down = (float)rows;
    float scale = thickness / compiled->unit.selfThickness;
    float square = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        float d = (across * compiled->unit.direction[k] + down * compiled->unit.downDirection[k]) *
                  compiled->unit.spacing * scale;
        square += d * d;
    }
    return square < thickness * thickness;
}

// How far two points may close in a step: twice the farthest one goes at
// the fastest speed and the emitter's gravity, from the step's start.
static float ClosingOf(const mvfxInstance* instance, int32_t emitter, double seconds)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxEmitterState* state = &instance->emitters[emitter];
    float fastest = 0.0f;
    for (int32_t i = 0; i < state->count; ++i)
    {
        float square = 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            const float* v =
                mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
            square += v[i] * v[i];
        }
        fastest = square > fastest ? square : fastest;
    }
    const float* g = compiled->gravity;
    float pull = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    if (compiled->space == mvfx_spaceLocal && compiled->gravitySpace == mvfx_spaceWorld)
    {
        // World gravity in the instance's space is over its scale.
        pull /= instance->to.scale;
    }
    float t = (float)seconds;
    return 2.0f * (sqrtf(fastest) * t + 0.5f * pull * t * t);
}

// Gathers point i's candidates: other points within reach, not near it
// at rest; false when it has more than the most.
static bool Gather(mvfxInstance* instance, int32_t emitter, int32_t i, const float* const x[3],
                   float reach, float thickness)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    const mvfxNeighborGrid* grid = &state->selfGrid;
    int32_t most = instance->effect->emitters[emitter].unit.selfContacts;
    int32_t* out = state->selfPairs + (size_t)i * (size_t)most;
    float inverseCell = 1.0f / reach;
    int32_t cell[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        cell[k] = mvfxGridCell(x[k][i], inverseCell);
    }
    uint32_t visited[27];
    int32_t visits = 0;
    int32_t count = 0;
    bool capped = false;
    for (int32_t n = 0; n < 27; ++n)
    {
        int32_t around[3] = {cell[0] + n % 3 - 1, cell[1] + n / 3 % 3 - 1, cell[2] + n / 9 - 1};
        uint32_t key = mvfxCellKey(around, grid->mask);
        bool seen = false;
        for (int32_t v = 0; v < visits; ++v)
        {
            seen = seen || visited[v] == key;
        }
        visited[visits] = key;
        visits += seen ? 0 : 1;
        for (int32_t e = seen ? 0 : grid->table[key]; !seen && e < grid->table[key + 1u]; ++e)
        {
            int32_t j = grid->index[e];
            float square = 0.0f;
            for (int32_t k = 0; k < 3; ++k)
            {
                float d = x[k][i] - x[k][j];
                square += d * d;
            }
            if (j == i || !(square < reach * reach) ||
                IsNearAtRest(instance, emitter, i, j, thickness))
            {
                continue;
            }
            capped = capped || count == most;
            out[count] = j;
            count += count < most ? 1 : 0;
        }
    }
    state->selfCounts[i] = count;
    return !capped;
}

void mvfxFindSelfContacts(mvfxInstance* instance, int32_t emitter, double seconds)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (!(compiled->unit.selfThickness > 0.0f) || compiled->unitPoints < 2)
    {
        return;
    }
    float thickness = ThicknessOf(instance, emitter);
    float reach = thickness + ClosingOf(instance, emitter, seconds);
    if (!(reach < INFINITY))
    {
        reach = thickness;
    }
    const float* x[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        x[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
    }
    mvfxBuildNeighborGrid(&state->selfGrid, x, nullptr, state->count, reach);
    for (int32_t i = 0; i < state->count; ++i)
    {
        state->selfContactCappedPointCount +=
            Gather(instance, emitter, i, x, reach, thickness) ? 0u : 1u;
    }
}

void mvfxSaveSelfStart(mvfxInstance* instance, int32_t emitter)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (!(instance->effect->emitters[emitter].unit.selfThickness > 0.0f))
    {
        return;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        const float* x =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        memcpy(state->selfStart + (size_t)k * (size_t)state->capacity, x,
               (size_t)state->count * sizeof(float));
    }
}

// A pair's view: places, places at the substep's start, weights.
typedef struct Pair
{
    float* x[3];
    const float* start[3];
    float thickness;
    float friction;
} Pair;

// Point i's move from its pair with point j of weights wi and wj: its
// share of their overlap along their line, less its share of their
// sliding since the substep's start (both points' overlap shares taken
// first) up to friction times the overlap; false where they do not
// touch.
static bool PartOf(const Pair* p, int32_t i, int32_t j, float wi, float wj, float out[3])
{
    float d[3];
    float square = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = p->x[k][i] - p->x[k][j];
        square += d[k] * d[k];
    }
    float length = sqrtf(square);
    float sum = wi + wj;
    if (!(length < p->thickness) || !(length > 0.0f) || !(sum > 0.0f))
    {
        return false;
    }
    float overlap = p->thickness - length;
    float n[3];
    float slide[3];
    float along = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        n[k] = d[k] / length;
        float xi = p->x[k][i] + wi / sum * overlap * n[k];
        float xj = p->x[k][j] - wj / sum * overlap * n[k];
        slide[k] = (xi - p->start[k][i]) - (xj - p->start[k][j]);
        along += slide[k] * n[k];
    }
    float tangent = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        slide[k] -= along * n[k];
        tangent += slide[k] * slide[k];
    }
    tangent = sqrtf(tangent);
    float limit = p->friction * overlap;
    float share = tangent > 0.0f ? (tangent < limit ? 1.0f : limit / tangent) : 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = wi / sum * overlap * n[k] - wi / sum * share * slide[k];
    }
    return true;
}

void mvfxSolveSelfContacts(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (!(compiled->unit.selfThickness > 0.0f))
    {
        return;
    }
    Pair p = {.thickness = ThicknessOf(instance, emitter), .friction = compiled->unit.selfFriction};
    for (int32_t k = 0; k < 3; ++k)
    {
        p.x[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        p.start[k] = state->selfStart + (size_t)k * (size_t)state->capacity;
    }
    bool pinned = mvfxHasAttribute(&compiled->plan, mvfx_attributeAnchorX);
    const uint32_t* flags = mvfxHasAttribute(&compiled->plan, mvfx_attributeAttachFlags)
                                ? mvfxAttributeArray(instance, emitter, mvfx_attributeAttachFlags)
                                : nullptr;
    int32_t most = compiled->unit.selfContacts;
    size_t stride = (size_t)state->capacity;
    for (int32_t i = 0; i < state->count; ++i)
    {
        const int32_t* pairs = state->selfPairs + (size_t)i * (size_t)most;
        float wi = mvfxPointWeight(compiled, flags, pinned, i);
        float sum[3] = {0.0f, 0.0f, 0.0f};
        int32_t touching = 0;
        for (int32_t c = 0; c < state->selfCounts[i]; ++c)
        {
            int32_t j = pairs[c];
            float move[3];
            if (PartOf(&p, i, j, wi, mvfxPointWeight(compiled, flags, pinned, j), move))
            {
                for (int32_t k = 0; k < 3; ++k)
                {
                    sum[k] += move[k];
                }
                touching += 1;
            }
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            state->selfMoves[(size_t)k * stride + (size_t)i] =
                touching > 0 ? sum[k] / (float)touching : 0.0f;
        }
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        const float* moves = state->selfMoves + (size_t)k * stride;
        for (int32_t i = 0; i < state->count; ++i)
        {
            p.x[k][i] += moves[i];
        }
    }
}
