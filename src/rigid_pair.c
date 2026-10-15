// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid_pair.h"

#include "appearance.h"
#include "direction.h"
#include "neighbor.h"
#include "rigid_contact.h"
#include "rigid_sleep.h"

#include <math.h>

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Body i's shape and pose.
static mvfxBodyShape ShapeAt(const mvfxRigidSolve* s, int32_t i, mvfxBodyPose* pose)
{
    float size[3];
    mvfxBodyMass mass;
    mvfxSizeOf(s->instance, s->emitter, i, size);
    for (int32_t k = 0; k < 3; ++k)
    {
        pose->x[k] = s->x[k][i];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        pose->q[k] = s->q[k][i];
    }
    return mvfxBodyShapeOf(s->rigid, s->hull, size, &mass);
}

// A contact's offsets from both bodies' centers and its depth, at the
// bodies' poses or at their start poses.
static float PairDepth(const mvfxPairContact* c, const mvfxBody* a, const mvfxBody* b, bool start,
                       float ra[3], float rb[3])
{
    mvfxRotate(start ? a->q0 : a->q, c->onA, ra);
    mvfxRotate(start ? b->q0 : b->q, c->onB, rb);
    const float* xa = start ? a->x0 : a->x;
    const float* xb = start ? b->x0 : b->x;
    const float gap[3] = {xa[0] + ra[0] - xb[0] - rb[0], xa[1] + ra[1] - xb[1] - rb[1],
                          xa[2] + ra[2] - xb[2] - rb[2]};
    return Dot(gap, c->normal);
}

// The speed along the normal of a's point from b's.
static float PairSpeed(const mvfxPairContact* c, const mvfxBody* a, const mvfxBody* b,
                       const float ra[3], const float rb[3])
{
    float va[3];
    float vb[3];
    mvfxBodyPointVelocity(a, ra, va);
    mvfxBodyPointVelocity(b, rb, vb);
    const float v[3] = {va[0] - vb[0], va[1] - vb[1], va[2] - vb[2]};
    return Dot(v, c->normal);
}

// Adds a's contacts with b to the pool, as far as it holds them.
static void AddPair(const mvfxRigidSolve* s, int32_t a, int32_t b, const mvfxBodyContacts* list)
{
    mvfxEmitterState* state = &s->instance->emitters[s->emitter];
    int64_t room = (int64_t)s->rigid->bodyContacts * state->count;
    mvfxBody bodyA = mvfxLoadBody(s, a);
    mvfxBody bodyB = mvfxLoadBody(s, b);
    for (int32_t i = 0; i < list->count; ++i)
    {
        if (state->rigidPairCount >= room)
        {
            state->rigidContactsCapped += (uint64_t)(list->count - i);
            return;
        }
        const mvfxBodyContact* from = &list->items[i];
        mvfxPairContact* c = &state->rigidPairs[state->rigidPairCount++];
        *c = (mvfxPairContact){.a = a, .b = b};
        const float offset[3] = {from->surface[0] - bodyB.x[0], from->surface[1] - bodyB.x[1],
                                 from->surface[2] - bodyB.x[2]};
        const float inverse[4] = {-bodyB.q[0], -bodyB.q[1], -bodyB.q[2], bodyB.q[3]};
        mvfxRotate(inverse, offset, c->onB);
        for (int32_t k = 0; k < 3; ++k)
        {
            c->onA[k] = from->body[k];
            c->normal[k] = from->normal[k];
        }
        float ra[3];
        float rb[3];
        (void)PairDepth(c, &bodyA, &bodyB, false, ra, rb);
        c->speed = PairSpeed(c, &bodyA, &bodyB, ra, rb);
    }
    s->flags[a] |= list->count > 0 ? MVFX_RIGID_TOUCHED : 0;
    s->flags[b] |= list->count > 0 ? MVFX_RIGID_TOUCHED : 0;
}

// The most levels of the pair grid: bodies' reaches from the smallest to
// 2^23 times it each have their own.
#define MOST_LEVELS 24

// Body i's reach.
static float ReachOf(const mvfxRigidSolve* s, int32_t i)
{
    return s->mass[MVFX_RIGID_REACH * s->stride + (size_t)i];
}

// The level of a reach: the least L with reach at most base 2^L, its cells
// 2 base 2^L wide (so two bodies of it or below that meet are in
// neighboring cells).
static int32_t LevelOf(float reach, float base)
{
    int32_t level = 0;
    float bound = base;
    while (reach > bound && level < MOST_LEVELS - 1)
    {
        bound *= 2.0f;
        level += 1;
    }
    return level;
}

// Moves v[root] down the heap of v[0, end) to its place.
static void SiftDown(int32_t* v, int32_t root, int32_t end)
{
    for (int32_t child = 2 * root + 1; child < end; child = 2 * root + 1)
    {
        child += child + 1 < end && v[child + 1] > v[child] ? 1 : 0;
        if (v[root] >= v[child])
        {
            return;
        }
        int32_t swap = v[root];
        v[root] = v[child];
        v[child] = swap;
        root = child;
    }
}

// Sorts n indexes ascending by heapsort.
static void SortIndexes(int32_t* v, int32_t n)
{
    for (int32_t start = n / 2 - 1; start >= 0; --start)
    {
        SiftDown(v, start, n);
    }
    for (int32_t end = n - 1; end > 0; --end)
    {
        int32_t top = v[0];
        v[0] = v[end];
        v[end] = top;
        SiftDown(v, 0, end);
    }
}

// A level's cell width: twice the base times 2^level.
static float CellOf(float base, int32_t level)
{
    return (float)ldexp(2.0 * (double)base, level);
}

// The pair grid: its smallest reach and its levels with bodies.
typedef struct Levels
{
    float base;
    uint32_t present;
} Levels;

// Body a's candidates at one level of the grid: its bodies within reach
// whose pair with a is a's to find (those of a's level after it, all of
// a higher one), appended from at.
static int32_t CandidatesAt(const mvfxRigidSolve* s, int32_t a, int32_t level, const Levels* levels,
                            int32_t at)
{
    mvfxEmitterState* state = &s->instance->emitters[s->emitter];
    const mvfxNeighborGrid* grid = &state->rigidGrid;
    float inverseCell = 1.0f / CellOf(levels->base, level);
    int32_t own = LevelOf(ReachOf(s, a), levels->base);
    int32_t cell[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        cell[k] = mvfxGridCell(s->x[k][a], inverseCell);
    }
    uint32_t visited[27];
    int32_t visits = 0;
    for (int32_t n = 0; n < 27; ++n)
    {
        int32_t around[3] = {cell[0] + n % 3 - 1, cell[1] + n / 3 % 3 - 1, cell[2] + n / 9 - 1};
        uint32_t key = mvfxLevelKey(around, level, grid->mask);
        bool seen = false;
        for (int32_t v = 0; v < visits; ++v)
        {
            seen = seen || visited[v] == key;
        }
        visited[visits] = key;
        visits += seen ? 0 : 1;
        for (int32_t e = seen ? 0 : grid->table[key]; !seen && e < grid->table[key + 1u]; ++e)
        {
            int32_t b = grid->index[e];
            state->rigidExamined += 1u;
            float reach = ReachOf(s, a) + ReachOf(s, b);
            float square = 0.0f;
            for (int32_t k = 0; k < 3; ++k)
            {
                float d = s->x[k][a] - s->x[k][b];
                square += d * d;
            }
            // Of this level (keys of others may share the slot), a's to
            // find, and not two sleeping bodies, which stay as they lie.
            bool mine = LevelOf(ReachOf(s, b), levels->base) == level && (level > own || b > a);
            bool resting = mvfxBodyAsleep(s, a) && mvfxBodyAsleep(s, b);
            if (mine && !resting && square < reach * reach)
            {
                state->rigidCandidates[at++] = b;
            }
        }
    }
    return at;
}

// Body a's pairs to find, met in the order of the other body's index
// (whatever the grid's layout), each as its lower index against its
// higher.
static void PairsOf(const mvfxRigidSolve* s, int32_t a, const Levels* levels)
{
    mvfxEmitterState* state = &s->instance->emitters[s->emitter];
    int32_t count = 0;
    for (int32_t level = LevelOf(ReachOf(s, a), levels->base); level < MOST_LEVELS; ++level)
    {
        if ((levels->present >> level & 1u) != 0u)
        {
            count = CandidatesAt(s, a, level, levels, count);
        }
    }
    SortIndexes(state->rigidCandidates, count);
    for (int32_t c = 0; c < count; ++c)
    {
        int32_t b = state->rigidCandidates[c];
        int32_t low = a < b ? a : b;
        int32_t high = a < b ? b : a;
        mvfxBodyPose poseLow;
        mvfxBodyPose poseHigh;
        mvfxBodyShape shapeLow = ShapeAt(s, low, &poseLow);
        mvfxBodyShape shapeHigh = ShapeAt(s, high, &poseHigh);
        mvfxBodyContacts list = {.count = 0};
        mvfxGatherPairContacts(&poseLow, &shapeLow, &poseHigh, &shapeHigh, &list);
        state->rigidContactsCapped += (uint64_t)list.capped;
        AddPair(s, low, high, &list);
    }
}

void mvfxFindBodyPairs(const mvfxRigidSolve* s, int32_t count)
{
    mvfxEmitterState* state = &s->instance->emitters[s->emitter];
    state->rigidPairCount = 0;
    if (s->rigid->bodyContacts == 0 || count < 2)
    {
        return;
    }
    // A hierarchical grid (Mirtich 1996): each body in the cells of its
    // reach's level, so one large body does not make every cell large.
    Levels levels = {.base = INFINITY};
    float widest = 0.0f;
    for (int32_t i = 0; i < count; ++i)
    {
        levels.base = fminf(levels.base, ReachOf(s, i));
        widest = fmaxf(widest, ReachOf(s, i));
    }
    levels.base = fmaxf(levels.base, fmaxf((float)ldexp((double)widest, 1 - MOST_LEVELS), 1e-6f));
    mvfxNeighborGrid* grid = &state->rigidGrid;
    for (int32_t i = 0; i < count; ++i)
    {
        int32_t level = LevelOf(ReachOf(s, i), levels.base);
        float inverseCell = 1.0f / CellOf(levels.base, level);
        int32_t cell[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            cell[k] = mvfxGridCell(s->x[k][i], inverseCell);
        }
        grid->keys[i] = mvfxLevelKey(cell, level, grid->mask);
        levels.present |= 1u << level;
    }
    mvfxBuildKeyedGrid(grid, count);
    for (int32_t a = 0; a < count; ++a)
    {
        PairsOf(s, a, &levels);
    }
}

// A pair's patch of contacts from first to end: their centers on each
// body (as offsets from them at their poses and start poses) and normal
// weighted by their normal multipliers, the multipliers' sum and their
// normal impulses' over the substep; false when none pushed.
typedef struct PairPatch
{
    float ra[3];
    float rb[3];
    float ra0[3];
    float rb0[3];
    float n[3];
    float load;
    float pressed;
    float radius;
    mvfxPatchSpread spreadA;
    mvfxPatchSpread spreadB;
} PairPatch;

static bool PatchOf(const mvfxRigidSolve* s, const mvfxBody* a, const mvfxBody* b, int32_t first,
                    int32_t end, PairPatch* out)
{
    const mvfxPairContact* pairs = s->instance->emitters[s->emitter].rigidPairs;
    mvfxPairContact patch = {.a = 0};
    *out = (PairPatch){.load = 0.0f};
    for (int32_t i = first; i < end; ++i)
    {
        float lambda = fmaxf(pairs[i].lambda, 0.0f);
        out->load += lambda;
        out->pressed += lambda > 0.0f ? lambda / s->h + pairs[i].applied : 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            patch.onA[k] += lambda * pairs[i].onA[k];
            patch.onB[k] += lambda * pairs[i].onB[k];
            patch.normal[k] += lambda * pairs[i].normal[k];
        }
    }
    if (!(out->load > 0.0f) || !mvfxNormalize(patch.normal))
    {
        return false;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        patch.onA[k] /= out->load;
        patch.onB[k] /= out->load;
        out->n[k] = patch.normal[k];
    }
    (void)PairDepth(&patch, a, b, true, out->ra0, out->rb0);
    (void)PairDepth(&patch, a, b, false, out->ra, out->rb);
    // The radius on a, its normal in a's frame.
    float n[3];
    const float inverse[4] = {-a->q[0], -a->q[1], -a->q[2], a->q[3]};
    mvfxRotate(inverse, patch.normal, n);
    for (int32_t i = first; i < end; ++i)
    {
        float lambda = fmaxf(pairs[i].lambda, 0.0f);
        float d[3] = {pairs[i].onA[0] - patch.onA[0], pairs[i].onA[1] - patch.onA[1],
                      pairs[i].onA[2] - patch.onA[2]};
        float along = Dot(d, n);
        out->radius += lambda * sqrtf(fmaxf(Dot(d, d) - along * along, 0.0f));
        if (out->spreadA.count < MVFX_MAX_RIGID_CONTACTS)
        {
            mvfxRotate(a->q, pairs[i].onA, out->spreadA.offsets[out->spreadA.count++]);
            mvfxRotate(b->q, pairs[i].onB, out->spreadB.offsets[out->spreadB.count++]);
        }
    }
    out->radius /= out->load;
    return true;
}

// A pass over a pair's contacts from first to end: their start poses
// pushed out of the depth there with their poses (post-stabilization, as
// the world's contacts) when start, else their poses pushed apart, then
// with friction their static friction.
static void SolvePair(const mvfxRigidSolve* s, int32_t first, int32_t end, bool start,
                      bool friction, bool backward)
{
    mvfxPairContact* pairs = s->instance->emitters[s->emitter].rigidPairs;
    mvfxBody a = mvfxLoadBody(s, pairs[first].a);
    mvfxBody b = mvfxLoadBody(s, pairs[first].b);
    for (int32_t step = first; step < end; ++step)
    {
        mvfxPairContact* c = &pairs[mvfxPassIndex(first, end, step, backward)];
        float ra[3];
        float rb[3];
        float depth = PairDepth(c, &a, &b, start, ra, rb);
        if (!(depth < 0.0f))
        {
            continue;
        }
        float lambda =
            -depth / (mvfxBodyWeight(&a, ra, c->normal) + mvfxBodyWeight(&b, rb, c->normal));
        c->lambda += start ? 0.0f : lambda;
        const float p[3] = {lambda * c->normal[0], lambda * c->normal[1], lambda * c->normal[2]};
        const float back[3] = {-p[0], -p[1], -p[2]};
        mvfxBodyPush(&a, ra, p, start);
        mvfxBodyPush(&b, rb, back, start);
    }
    PairPatch patch;
    if (friction && PatchOf(s, &a, &b, first, end, &patch))
    {
        mvfxRubPatch(s, &a, patch.ra, patch.ra0, &b, patch.rb, patch.rb0, patch.n, patch.load,
                     &patch.spreadA, &patch.spreadB, &pairs[first].rubbed);
        mvfxTwistPatch(s, &a, &b, patch.n, patch.load * patch.radius, &pairs[first].twisted);
    }
    mvfxStoreBody(s, pairs[first].a, &a);
    mvfxStoreBody(s, pairs[first].b, &b);
}

// A pass of restitution over a pair's contacts from first to end, then
// its patch's dynamic friction, as a body's with the world.
static void BouncePair(const mvfxRigidSolve* s, int32_t first, int32_t end, bool backward)
{
    mvfxPairContact* pairs = s->instance->emitters[s->emitter].rigidPairs;
    mvfxBody a = mvfxLoadBody(s, pairs[first].a);
    mvfxBody b = mvfxLoadBody(s, pairs[first].b);
    for (int32_t step = first; step < end; ++step)
    {
        mvfxPairContact* c = &pairs[mvfxPassIndex(first, end, step, backward)];
        if (!(c->lambda > 0.0f))
        {
            continue;
        }
        float ra[3];
        float rb[3];
        (void)PairDepth(c, &a, &b, false, ra, rb);
        float normal = PairSpeed(c, &a, &b, ra, rb);
        float weight = mvfxBodyWeight(&a, ra, c->normal) + mvfxBodyWeight(&b, rb, c->normal);
        float total = fmaxf(c->applied + (mvfxBounceTarget(s, c->speed) - normal) / weight,
                            -c->lambda / s->h);
        float j = total - c->applied;
        c->applied = total;
        const float impulse[3] = {j * c->normal[0], j * c->normal[1], j * c->normal[2]};
        const float back[3] = {-impulse[0], -impulse[1], -impulse[2]};
        mvfxBodyKick(&a, ra, impulse);
        mvfxBodyKick(&b, rb, back);
    }
    PairPatch patch;
    if (PatchOf(s, &a, &b, first, end, &patch))
    {
        mvfxSlowPatch(&a, patch.ra, &b, patch.rb, patch.n, patch.pressed, &patch.spreadA,
                      &patch.spreadB, s->rigid->dynamicFriction, &pairs[first].slowed);
        mvfxUnspinPatch(&a, &b, patch.n, s->rigid->dynamicFriction * patch.pressed * patch.radius,
                        &pairs[first].spun);
    }
    mvfxStoreBody(s, pairs[first].a, &a);
    mvfxStoreBody(s, pairs[first].b, &b);
}

// Pair run from first to end in a pass.
static void PassPair(const mvfxRigidSolve* s, int32_t first, int32_t end, mvfxContactStage stage,
                     bool backward)
{
    if (stage == mvfx_stageBounce)
    {
        BouncePair(s, first, end, backward);
        return;
    }
    SolvePair(s, first, end, stage == mvfx_stageStabilize, stage == mvfx_stagePush, backward);
}

void mvfxPairPass(const mvfxRigidSolve* s, mvfxContactStage stage, bool backward)
{
    const mvfxEmitterState* state = &s->instance->emitters[s->emitter];
    const mvfxPairContact* pairs = state->rigidPairs;
    int32_t count = state->rigidPairCount;
    // Each pair's contacts are a run: forward from the first, backward
    // from the last.
    for (int32_t at = 0; at < count;)
    {
        int32_t first = backward ? count - 1 - at : at;
        int32_t end = first + 1;
        while (!backward && end < count && pairs[end].a == pairs[first].a &&
               pairs[end].b == pairs[first].b)
        {
            ++end;
        }
        while (backward && first > 0 && pairs[first - 1].a == pairs[first].a &&
               pairs[first - 1].b == pairs[first].b)
        {
            --first;
        }
        PassPair(s, first, end, stage, backward);
        at += end - first;
    }
}

void mvfxStrikePairs(const mvfxRigidSolve* s)
{
    const mvfxEmitterState* state = &s->instance->emitters[s->emitter];
    for (int32_t i = 0; i < state->rigidPairCount; ++i)
    {
        const mvfxPairContact* c = &state->rigidPairs[i];
        if (!(c->lambda > 0.0f) || !(c->speed < -s->still))
        {
            continue;
        }
        mvfxBody b = mvfxLoadBody(s, c->b);
        float rb[3];
        float point[3];
        mvfxRotate(b.q, c->onB, rb);
        for (int32_t k = 0; k < 3; ++k)
        {
            point[k] = b.x[k] + rb[k];
        }
        const float back[3] = {-c->normal[0], -c->normal[1], -c->normal[2]};
        mvfxStrikeBody(s, c->a, point, c->normal);
        mvfxStrikeBody(s, c->b, point, back);
    }
}
