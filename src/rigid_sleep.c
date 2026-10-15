// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid_sleep.h"

#include "effect.h"
#include "rigid_wake.h"

#include <math.h>

void mvfxHoldSleeping(const mvfxRigidSolve* s, int32_t first, int32_t end)
{
    // What would build the sleep speed over the sleep time: forces changed
    // by more than it wake the body.
    float change = s->rigid->sleepSpeed / s->rigid->sleepTime;
    for (int32_t i = first; i < end; ++i)
    {
        if (!mvfxBodyAsleep(s, i))
        {
            continue;
        }
        // From rest, the substep's velocity over h is the forces'
        // acceleration.
        float a[3];
        float moved = 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            a[k] = s->v[k][i] / s->h;
            float d = a[k] - s->rest[k][i];
            moved += d * d;
        }
        if (s->sleep[i] == MVFX_SLEEP_NEW)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                s->rest[k][i] = a[k];
            }
            s->sleep[i] = MVFX_SLEEP_HELD;
        }
        else if (!(moved <= change * change))
        {
            // Awake from this substep on, starting from rest.
            s->sleep[i] = 0.0f;
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            s->x[k][i] = s->start[(size_t)k * s->stride + (size_t)i];
            s->v[k][i] = 0.0f;
            s->w[k][i] = 0.0f;
        }
        for (int32_t k = 0; k < 4; ++k)
        {
            s->q[k][i] = s->start[(size_t)(3 + k) * s->stride + (size_t)i];
        }
    }
}

void mvfxWakeAll(const mvfxRigidSolve* s, int32_t count)
{
    for (int32_t i = 0; i < count; ++i)
    {
        s->sleep[i] = mvfxBodyAsleep(s, i) ? 0.0f : s->sleep[i];
    }
}

// Union-find by path halving, the lower index the root: the same islands
// whatever order the pairs join in.
static int32_t Root(int32_t* parent, int32_t i)
{
    while (parent[i] != i)
    {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

static void Join(int32_t* parent, int32_t a, int32_t b)
{
    int32_t ra = Root(parent, a);
    int32_t rb = Root(parent, b);
    if (ra < rb)
    {
        parent[rb] = ra;
    }
    else if (rb < ra)
    {
        parent[ra] = rb;
    }
}

// A body's still time over the step: grown while its farthest point is
// slower than the sleep speed, else back to 0.
static void Still(const mvfxRigidSolve* s, int32_t i, float seconds)
{
    float v = 0.0f;
    float w = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        v += s->v[k][i] * s->v[k][i];
        w += s->w[k][i] * s->w[k][i];
    }
    float speed = sqrtf(v) + sqrtf(w) * mvfxReachOf(s->instance, s->emitter, i);
    s->sleep[i] = speed < s->rigid->sleepSpeed ? s->sleep[i] + seconds : 0.0f;
}

// Islands: each body's root, then each root marked (as -1 less itself)
// while one of its bodies keeps it awake.
static void Islands(const mvfxRigidSolve* s, int32_t count, int32_t* island)
{
    const mvfxEmitterState* state = &s->instance->emitters[s->emitter];
    for (int32_t i = 0; i < count; ++i)
    {
        island[i] = i;
    }
    for (int32_t p = 0; p < state->rigidPairCount; ++p)
    {
        const mvfxPairContact* pair = &state->rigidPairs[p];
        if (pair->a < count && pair->b < count)
        {
            Join(island, pair->a, pair->b);
        }
    }
    for (int32_t i = 0; i < count; ++i)
    {
        island[i] = Root(island, i);
    }
    for (int32_t i = 0; i < count; ++i)
    {
        int32_t root = island[i] < 0 ? -1 - island[i] : island[i];
        bool keeps = s->sleep[i] >= 0.0f && s->sleep[i] < s->rigid->sleepTime;
        if (keeps && island[root] >= 0)
        {
            island[root] = -1 - root;
        }
    }
}

void mvfxSettleBodies(const mvfxRigidSolve* s, int32_t count, float seconds)
{
    for (int32_t i = 0; i < count; ++i)
    {
        if (!mvfxBodyAsleep(s, i))
        {
            Still(s, i, seconds);
        }
    }
    int32_t* island = s->instance->emitters[s->emitter].rigidIslands;
    Islands(s, count, island);
    for (int32_t i = 0; i < count; ++i)
    {
        int32_t root = island[i] < 0 ? -1 - island[i] : island[i];
        bool awake = island[root] < 0;
        if (awake && mvfxBodyAsleep(s, i))
        {
            s->sleep[i] = 0.0f;
        }
        else if (!awake && !mvfxBodyAsleep(s, i))
        {
            s->sleep[i] = MVFX_SLEEP_NEW;
            for (int32_t k = 0; k < 3; ++k)
            {
                s->v[k][i] = 0.0f;
                s->w[k][i] = 0.0f;
            }
        }
    }
}
