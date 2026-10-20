// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Particles whose contacts the two paths may decide differently
// (rhi_apart.h): the near particles of each emitter at a step's end and
// the last, in serial order, and those gone apart.

#include "rhi_apart.h"

#include <math.h>
#include <stdlib.h>

// The most particles an emitter of a run holds, gone apart in a run,
// and uncertain events in a run.
#define MOST_HELD      (1 << 17)
#define MOST_APART     256
#define MOST_UNCERTAIN 1024

// Serials in order, and each one's distance to the nearest collider as
// a share of the reach.
typedef struct Near
{
    uint32_t serials[MOST_HELD];
    float shares[MOST_HELD];
    int32_t count;
} Near;

// Each emitter's near particles at this step's end and the last.
static Near s_near[2][2];
static int32_t s_now[2];
static uint32_t s_apart[2][MOST_APART];
static int32_t s_apartCount[2];
static int32_t s_mostNear;
static float s_largestShare;
static int64_t s_uncertain[MOST_UNCERTAIN];
static int32_t s_uncertainCount;

void ApartReset(void)
{
    for (int32_t e = 0; e < 2; ++e)
    {
        s_near[e][0].count = 0;
        s_near[e][1].count = 0;
        s_apartCount[e] = 0;
    }
    s_mostNear = 0;
    s_largestShare = 0.0f;
    s_uncertainCount = 0;
}

// The distance from a place to a box collider's surface, in its frame;
// with within, 0 inside it (a grid's box, whose surface lies within).
static double BoxDistance(const float place[3], const mvfxPlannedCollider* collider, bool within)
{
    const float* q = collider->rotation;
    double d[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = (double)place[k] - (double)collider->center[k];
    }
    // Turned back by the conjugate: v + 2 w (u x v) + 2 u x (u x v), u = -q.
    double u[3] = {-(double)q[0], -(double)q[1], -(double)q[2]};
    double t[3] = {2.0 * (u[1] * d[2] - u[2] * d[1]), 2.0 * (u[2] * d[0] - u[0] * d[2]),
                   2.0 * (u[0] * d[1] - u[1] * d[0])};
    double outside = 0.0;
    double inside = (double)INFINITY;
    for (int32_t k = 0; k < 3; ++k)
    {
        int32_t a = (k + 1) % 3;
        int32_t b = (k + 2) % 3;
        double local = d[k] + (double)q[3] * t[k] + (u[a] * t[b] - u[b] * t[a]);
        // A heightfield's surface lies anywhere along its Y.
        bool along = within && k == 1 && collider->kind == mvfx_colliderHeightfield;
        double past = along ? -(double)INFINITY : fabs(local) - (double)collider->halfExtents[k];
        outside += past > 0.0 ? past * past : 0.0;
        inside = fmin(inside, -past);
    }
    return outside > 0.0 ? sqrt(outside) : (within ? 0.0 : inside);
}

// The distance from a place to a collider's surface, in the particles'
// space; for a rigid body (bodies), a grid's box's as its surface's, which
// lies anywhere within it; infinity for a grid's otherwise, which no
// particle is near.
static double Distance(const float place[3], const mvfxPlannedCollider* collider, bool bodies)
{
    double d = 0.0;
    double length = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double offset = (double)place[k] - (double)collider->center[k];
        d += collider->kind == mvfx_colliderPlane ? offset * (double)collider->normal[k]
                                                  : offset * offset;
        length += (double)collider->normal[k] * (double)collider->normal[k];
    }
    switch (collider->kind)
    {
    case mvfx_colliderPlane:
        return fabs(d) / sqrt(length);
    case mvfx_colliderSphere:
        return fabs(sqrt(d) - (double)collider->radius);
    case mvfx_colliderBox:
        return BoxDistance(place, collider, false);
    default:
        return bodies ? BoxDistance(place, collider, true) : (double)INFINITY;
    }
}

void ApartTakeStep(int32_t emitter, const uint32_t* serials, const float* const places[3],
                   int32_t count, const mvfxEmitterPlan* plan, float tolerance, float radius)
{
    s_now[emitter] = 1 - s_now[emitter];
    Near* near = &s_near[emitter][s_now[emitter]];
    near->count = 0;
    for (int32_t i = 0; i < count && near->count < MOST_HELD; ++i)
    {
        float place[3] = {places[0][i], places[1][i], places[2][i]};
        // A place on each path within the tolerance of each component:
        // the GPU's at most this far from the CPU path's.
        double largest =
            fmax(fabs((double)place[0]), fmax(fabs((double)place[1]), fabs((double)place[2])));
        double reach = sqrt(3.0) * (double)tolerance * (1.0 + largest);
        double nearest = (double)INFINITY;
        for (int32_t c = 0; c < plan->colliderCount; ++c)
        {
            nearest = fmin(nearest, Distance(place, &plan->colliders[c], radius > 0.0f));
        }
        // A body's surface may lie anywhere within its radius.
        nearest = fmax(nearest - (double)radius, 0.0);
        if (nearest <= reach)
        {
            near->shares[near->count] = (float)(nearest / reach);
            near->serials[near->count++] = serials[i];
        }
    }
    s_mostNear = near->count > s_mostNear ? near->count : s_mostNear;
}

static int BySerial(const void* a, const void* b)
{
    uint32_t x = *(const uint32_t*)a;
    uint32_t y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}

// The share of the reach a near particle lay at; above 1 for one not near.
static float ShareOf(const Near* near, uint32_t serial)
{
    const uint32_t* found =
        bsearch(&serial, near->serials, (size_t)near->count, sizeof serial, BySerial);
    return found != NULL ? near->shares[found - near->serials] : 2.0f;
}

bool IsApart(int32_t emitter, uint32_t serial)
{
    for (int32_t a = 0; a < s_apartCount[emitter]; ++a)
    {
        if (s_apart[emitter][a] == serial)
        {
            return true;
        }
    }
    return false;
}

bool GoApart(int32_t emitter, uint32_t serial)
{
    float share = fminf(ShareOf(&s_near[emitter][0], serial), ShareOf(&s_near[emitter][1], serial));
    if (share > 1.0f || s_apartCount[emitter] == MOST_APART)
    {
        return false;
    }
    s_largestShare = share > s_largestShare ? share : s_largestShare;
    s_apart[emitter][s_apartCount[emitter]++] = serial;
    return true;
}

int32_t ApartCount(void)
{
    return s_apartCount[0] + s_apartCount[1];
}

int32_t MostNear(void)
{
    return s_mostNear;
}

float LargestShare(void)
{
    return s_largestShare;
}

void TakeUncertain(int64_t tick)
{
    if (!IsUncertain(tick, 0) && s_uncertainCount < MOST_UNCERTAIN)
    {
        s_uncertain[s_uncertainCount++] = tick;
    }
}

bool IsUncertain(int64_t tick, int64_t slack)
{
    return UncertainBetween(tick - slack - 1, tick + slack, 0);
}

bool UncertainBetween(int64_t after, int64_t upTo, int64_t slack)
{
    for (int32_t u = 0; u < s_uncertainCount; ++u)
    {
        if (s_uncertain[u] > after - slack && s_uncertain[u] <= upTo + slack)
        {
            return true;
        }
    }
    return false;
}

int32_t UncertainCount(void)
{
    return s_uncertainCount;
}
