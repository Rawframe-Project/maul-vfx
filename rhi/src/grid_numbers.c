// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The numbers of an emitter's grids on the GPU: the neighbor grid's, with
// steering's and the pair force's, the contacts', the plexus links' and
// units' self-collision's.

#include "numbers.h"

#include <math.h>
#include <string.h>

uint32_t mvfxTableMask(int32_t capacity)
{
    uint32_t size = 2u;
    while (size < 2u * (uint32_t)capacity)
    {
        size *= 2u;
    }
    return size - 1u;
}

void mvfxContactNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                        uint32_t words[12])
{
    const mvfxContactDef* c = &view->contacts;
    uint32_t blocks = mvfxSortBlocks(plan->capacity);
    uint32_t groups = mvfxUpdateGroups(plan->capacity);
    const uint32_t head[8] = {(uint32_t)plan->capacity,
                              (uint32_t)c->maxContacts,
                              blocks < 65535u ? (blocks > 0u ? blocks : 1u) : 65535u,
                              groups > 0u ? groups : 1u,
                              mvfxTableMask(plan->capacity),
                              0u,
                              0u,
                              0u};
    memcpy(words, head, sizeof head);
    mvfxFloatWords(words + 8, c->radiusScale, c->restitution, c->friction, 0.0f);
}

void mvfxSelfNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                     uint32_t words[MVFX_SELF_WORDS])
{
    const mvfxUnitDef* u = &view->unit;
    const mvfxAttributeArrays* a = &view->arrays;
    uint32_t blocks = mvfxSortBlocks(plan->capacity);
    uint32_t groups = mvfxUpdateGroups(plan->capacity);
    const uint32_t head[12] = {(uint32_t)plan->capacity,
                               (uint32_t)u->selfContacts,
                               blocks < 65535u ? (blocks > 0u ? blocks : 1u) : 65535u,
                               groups > 0u ? groups : 1u,
                               (uint32_t)view->unitPoints,
                               (uint32_t)view->unitWidth,
                               mvfxTableMask(plan->capacity),
                               mvfxWordOf(a->attachFlags),
                               mvfxWordOf(a->position),
                               mvfxWordOf(a->velocity),
                               (uint32_t)a->count + 2u,
                               a->anchor >= 0 ? (u->pinFirst ? MVFX_UNIT_PIN_FIRST : 0u) |
                                                    (u->pinFirstRow ? MVFX_UNIT_PIN_ROW : 0u)
                                              : 0u};
    memcpy(words, head, sizeof head);
    // The core's float operations (src/self.c): the thickness scales with a
    // world-space emitter's instance; gravity's pull, world gravity's over
    // the scale in a local space; its travel over the step.
    float scale = view->space == mvfx_spaceWorld ? plan->to.scale : 1.0f;
    float thickness = u->selfThickness * scale;
    const float* g = view->gravity;
    float pull = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    if (view->space == mvfx_spaceLocal && view->gravitySpace == mvfx_spaceWorld)
    {
        pull /= plan->to.scale;
    }
    float t = (float)((double)plan->ticks / (double)MVFX_TICKS_PER_SECOND);
    mvfxFloatWords(words + 12, thickness, t, 0.5f * pull * t * t, u->selfFriction);
    mvfxFloatWords(words + 16, u->direction[0], u->direction[1], u->direction[2], u->spacing);
    mvfxFloatWords(words + 20, u->downDirection[0], u->downDirection[1], u->downDirection[2],
                   thickness / u->selfThickness);
}

uint32_t mvfxGridFlags(const mvfxEmitterView* view, bool own)
{
    bool finds = view->neighbors.enable;
    bool steers = view->steering.enable || view->pairRadius > 0.0f;
    return (view->gridCell > 0.0f ? MVFX_GRID_BUILDS : 0u) | (finds ? MVFX_GRID_FINDS : 0u) |
           (finds && own ? MVFX_GRID_OWN : 0u) | (steers ? MVFX_GRID_STEERS : 0u);
}

// The square of a flocking rule's radius where its weight counts, 0
// otherwise, as the core's query takes it.
static float RuleSquared(float weight, float radius)
{
    return weight > 0.0f ? radius * radius : 0.0f;
}

// Steering's and the pair force's numbers (grid.wgsl's): whether it
// flocks, the path's points and segments, the strength's table; the cruise
// speed, the most force, the plan's seconds, the view's cosine; the rules'
// weights and the strength's value; their radii squared and the pair
// force's; the seek and flee points with their weights; their radii and
// the path's weight and radius; the lookahead, the perimeter, the pair
// force's inverse radius and whether the path is closed; the path's
// points.
static void SteeringWords(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                          const mvfxStepTables* tables, uint32_t* words)
{
    const mvfxSteeringDef* s = &view->steering;
    const mvfxPlannedSteering* t = &plan->steering;
    bool flock = s->enable && (s->separation > 0.0f || s->alignment > 0.0f || s->cohesion > 0.0f);
    words[0] = flock ? MVFX_STEER_FLOCK : 0u;
    words[1] = (uint32_t)t->pointCount;
    words[2] = (uint32_t)t->segmentCount;
    words[3] = tables->pairStrength;
    float seconds = (float)((double)plan->ticks / (double)MVFX_TICKS_PER_SECOND);
    mvfxFloatWords(words + 4, s->cruiseSpeed, s->maxForce, seconds, t->viewCosine);
    mvfxFloatWords(words + 8, s->separation, s->alignment, s->cohesion, view->pairStrength.value);
    float pair = view->pairRadius;
    mvfxFloatWords(words + 12, RuleSquared(s->separation, s->separationRadius),
                   RuleSquared(s->alignment, s->alignmentRadius),
                   RuleSquared(s->cohesion, s->cohesionRadius), pair * pair);
    mvfxFloatWords(words + 16, t->seek[0], t->seek[1], t->seek[2], s->seek.weight);
    mvfxFloatWords(words + 20, t->flee[0], t->flee[1], t->flee[2], s->flee.weight);
    mvfxFloatWords(words + 24, s->seek.radius, s->flee.radius, s->path.weight, s->path.radius);
    mvfxFloatWords(words + 28, s->path.lookahead, t->perimeter, pair > 0.0f ? 1.0f / pair : 0.0f,
                   s->path.closed ? 1.0f : 0.0f);
    for (int32_t i = 0; i < t->pointCount; ++i)
    {
        mvfxFloatWords(words + 32 + 4 * i, t->path[i][0], t->path[i][1], t->path[i][2], 0.0f);
    }
}

void mvfxGridNumbers(const mvfxEmitterView* view, const mvfxEmitterView* source,
                     const mvfxEmitterPlan* plan, const mvfxStepTables* tables, uint32_t mask,
                     uint32_t sourceMask, int32_t sourceCapacity, bool own,
                     uint32_t words[MVFX_GRID_WORDS])
{
    memset(words, 0, MVFX_GRID_WORDS * sizeof(uint32_t));
    const mvfxNeighborDef* n = &view->neighbors;
    uint32_t flags = mvfxGridFlags(view, own);
    bool finds = (flags & MVFX_GRID_FINDS) != 0;
    bool builds = (flags & MVFX_GRID_BUILDS) != 0;
    words[0] = (uint32_t)plan->capacity;
    words[1] = (uint32_t)sourceCapacity;
    uint32_t blocks = mvfxSortBlocks(plan->capacity);
    uint32_t groups = mvfxUpdateGroups(plan->capacity);
    words[2] = blocks < 65535u ? (blocks > 0u ? blocks : 1u) : 65535u;
    words[3] = groups > 0u ? groups : 1u;
    words[4] = builds ? mask : 0u;
    words[5] = flags;
    words[6] = finds ? (uint32_t)n->maxNeighbors : 0u;
    words[7] = finds ? sourceMask : 0u;
    mvfxFloatWords(words + 8, builds ? 1.0f / view->gridCell : 0.0f,
                   finds ? 1.0f / source->gridCell : 0.0f, finds ? n->radius * n->radius : 0.0f,
                   0.0f);
    const mvfxAttributeArrays* a = &view->arrays;
    const uint32_t arrays[8] = {mvfxWordOf(a->position),
                                mvfxWordOf(a->velocity),
                                (uint32_t)a->count,
                                mvfxWordOf(a->neighborCount),
                                mvfxWordOf(a->neighborCenter),
                                mvfxWordOf(a->neighborVelocity),
                                0u,
                                0u};
    memcpy(words + 12, arrays, sizeof arrays);
    uint64_t end = (uint64_t)(plan->start + plan->ticks);
    words[MVFX_GRID_CLOCK] = (uint32_t)end;
    words[MVFX_GRID_CLOCK + 1] = (uint32_t)(end >> 32u);
    // The anchor's shift, as the step's: the core's step moves the places
    // by it before it builds the grid.
    const double* before = plan->anchorBefore;
    const double* after = plan->anchorAfter;
    mvfxFloatWords(words + MVFX_GRID_SHIFT, (float)(before[0] - after[0]),
                   (float)(before[1] - after[1]), (float)(before[2] - after[2]), 0.0f);
    if ((flags & MVFX_GRID_STEERS) != 0)
    {
        SteeringWords(view, plan, tables, words + MVFX_GRID_STEER);
    }
}

void mvfxPlexusNumbers(const mvfxEmitterView* view, const mvfxEmitterPlan* plan, uint32_t words[12])
{
    const mvfxPlexusDef* x = &view->plexus;
    uint32_t blocks = mvfxSortBlocks(plan->capacity);
    uint32_t groups = mvfxUpdateGroups(plan->capacity);
    const uint32_t head[8] = {
        (uint32_t)plan->capacity,      (uint32_t)x->maxPerParticle,
        (uint32_t)x->maxLinks,         blocks < 65535u ? (blocks > 0u ? blocks : 1u) : 65535u,
        mvfxTableMask(plan->capacity), mvfxWordOf(view->arrays.position),
        groups > 0u ? groups : 1u,     0u};
    memcpy(words, head, sizeof head);
    mvfxFloatWords(words + 8, 1.0f / x->distance, x->distance * x->distance, 0.0f, 0.0f);
}

uint32_t mvfxLinkSpan(int32_t maxLinks)
{
    return ((2u * (uint32_t)maxLinks + MVFX_SORT_BLOCK - 1u) / MVFX_SORT_BLOCK) * MVFX_SORT_BLOCK;
}

void mvfxLinkNumbers(const mvfxEmitterView* view, const mvfxRibbonSink* sink,
                     uint32_t words[MVFX_RIBBON_WORDS])
{
    const mvfxPlexusDef* x = &view->plexus;
    memset(words, 0, MVFX_RIBBON_WORDS * sizeof(uint32_t));
    const uint32_t arrays[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, mvfx_ribbonStretch,
                                mvfx_ribbonFaceCamera};
    memcpy(words, arrays, sizeof arrays);
    mvfxFloatWords(words + 4, 1.0f, 0.0f, x->width, 0.0f);
    mvfxFloatWords(words + 8, 0.0f, 0.0f, 0.0f, x->fade ? x->distance : 0.0f);
    words[12] = 0xFFFFFFFFu;
    words[13] = 0xFFFFFFFFu;
    words[14] =
        (uint32_t)(sink->capacity < (int64_t)UINT32_MAX ? sink->capacity : (int64_t)UINT32_MAX);
    words[15] = sink->countWord;
    words[16] = 1u;
    const uint32_t sinks[4] = {sink->kind, sink->sides, sink->caps ? 1u : 0u,
                               mvfxLinkSpan(x->maxLinks)};
    memcpy(words + 24, sinks, sizeof sinks);
}
