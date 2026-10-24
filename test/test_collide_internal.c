// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The collision module's own functions: the signed gaps to each kind of
// collider; the colliders a box's segments may reach, none left out that
// one meets; and the segments a run's tests mark, every one that meets a
// collider among them.

#include "collide.h"
#include "test_harness.h"

#include <math.h>

static void TestGaps(void)
{
    // Signed gaps to resolved colliders: a plane's above and below; a
    // sphere's outside and, kept in, its inside; a box turned about z,
    // outside past a corner and inside, and kept in; a grid's box by its
    // bounds alone, outside it whatever its flag.
    const mvfxResolvedCollider plane = {
        .kind = mvfx_colliderPlane, .center = {0, 1, 0}, .normal = {0, 1, 0}};
    const float above[3] = {5, 3, 0};
    const float below[3] = {5, 0.5f, 0};
    bool planes = fabsf(mvfxColliderGap(&plane, above, false) - 2.0f) <= 1e-6f &&
                  fabsf(mvfxColliderGap(&plane, below, false) + 0.5f) <= 1e-6f;
    mvfxResolvedCollider ball = {.kind = mvfx_colliderSphere, .center = {1, 0, 0}, .radius = 2.0f};
    const float out[3] = {1, 3, 0};
    const float in[3] = {1, 0.5f, 0};
    bool balls = fabsf(mvfxColliderGap(&ball, out, false) - 1.0f) <= 1e-6f &&
                 fabsf(mvfxColliderGap(&ball, in, false) + 1.5f) <= 1e-6f;
    ball.inside = true;
    balls = balls && fabsf(mvfxColliderGap(&ball, in, false) - 1.5f) <= 1e-6f;
    // Half sizes 2 by 1 by 1, turned 30 degrees about z: the points at
    // (3, 2) and (0.5, 0.2) in its frame, 1 and 1 past its corner and 0.8
    // within its nearest face.
    mvfxResolvedCollider box = {.kind = mvfx_colliderBox,
                                .halfExtents = {2, 1, 1},
                                .rotation = {0, 0, 0.25881905f, 0.96592583f}};
    const float corner[3] = {1.5980762f, 3.2320508f, 0};
    const float within[3] = {0.3330127f, 0.4232051f, 0};
    bool boxes = fabsf(mvfxColliderGap(&box, corner, false) - 1.4142136f) <= 1e-5f &&
                 fabsf(mvfxColliderGap(&box, within, false) + 0.8f) <= 1e-5f;
    box.inside = true;
    boxes = boxes && fabsf(mvfxColliderGap(&box, within, false) - 0.8f) <= 1e-5f;
    const mvfxResolvedCollider grid = {.kind = mvfx_colliderHeightfield,
                                       .inside = true,
                                       .center = {0, 0, 0},
                                       .halfExtents = {2, 0, 2},
                                       .rotation = {0, 0, 0, 1}};
    const float over[3] = {1, 0.7f, 1};
    bool grids = fabsf(mvfxColliderGap(&grid, over, true) - 0.7f) <= 1e-6f;
    CHECK(planes && balls && boxes && grids, "gaps to colliders");
}

// A small generator for the reach's trials.
static uint32_t s_seed = 12345u;

static float Uniform(float lo, float hi)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)(s_seed >> 8) * 0x1p-24f;
}

// Whether a segment from a to b meets a collider alone.
static bool Meets(const mvfxResolvedCollider* c, const float a[3], const float b[3])
{
    const mvfxOwnColliders none = {0};
    const mvfxCollision response = {.enabled = true, .restitution = 0.5f};
    float place[3] = {b[0], b[1], b[2]};
    float velocity[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    mvfxContact first = {0};
    return mvfxCollide(c, 1, &none, &response, 0.0f, a, place, velocity, &first) > 0;
}

static void TestReach(void)
{
    // Boxes at random near planes and outside spheres, at scales from 1 to
    // 1000: a collider the reach leaves out meets none of 64 segments
    // between random points of the box and its corners; many boxes graze
    // their colliders, and some are left out within a hair of them. A box,
    // a field and an inside sphere are always kept.
    int32_t out = 0;
    int32_t close = 0;
    bool exact = true;
    for (int32_t trial = 0; trial < 4000; ++trial)
    {
        float scale = trial % 4 == 0 ? 1000.0f : (trial % 4 == 1 ? 10.0f : 1.0f);
        mvfxResolvedCollider c = {.rotation = {0, 0, 0, 1}};
        float n[3] = {Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)};
        float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int32_t k = 0; k < 3; ++k)
        {
            c.center[k] = Uniform(-scale, scale);
            c.normal[k] = n[k] / length;
        }
        bool sphere = trial % 2 == 1;
        c.kind = sphere ? mvfx_colliderSphere : mvfx_colliderPlane;
        c.radius = sphere ? Uniform(0.01f, 1.0f) * scale : 0.0f;
        // The box's near corner a small way off the surface along n.
        float gap = Uniform(-0.01f, 0.05f) * (trial % 8 < 4 ? 1.0f : 0.001f);
        float lo[3];
        float hi[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            float at = c.center[k] + c.normal[k] * (c.radius + gap * scale);
            float size = Uniform(0.0f, 0.5f) * scale;
            lo[k] = c.normal[k] > 0.0f ? at : at - size;
            hi[k] = c.normal[k] > 0.0f ? at + size : at;
        }
        mvfxResolvedCollider kept[1];
        if (mvfxReachable(&c, 1, lo, hi, kept) == 1)
        {
            continue;
        }
        out += 1;
        close += gap * scale < 1e-3f * scale ? 1 : 0;
        for (int32_t s = 0; s < 64; ++s)
        {
            float a[3];
            float b[3];
            for (int32_t k = 0; k < 3; ++k)
            {
                bool corner = s % 4 == 0;
                a[k] = corner ? (Uniform(0, 1) < 0.5f ? lo[k] : hi[k]) : Uniform(lo[k], hi[k]);
                b[k] = corner ? (Uniform(0, 1) < 0.5f ? lo[k] : hi[k]) : Uniform(lo[k], hi[k]);
            }
            exact = exact && !Meets(&c, a, b);
        }
    }
    CHECK(exact, "a collider left out meets no segment in its box");
    CHECK(out > 1000 && close > 100, "many left out, some within a hair");
    const float lo[3] = {100, 100, 100};
    const float hi[3] = {101, 101, 101};
    const mvfxResolvedCollider always[3] = {
        {.kind = mvfx_colliderBox, .halfExtents = {1, 1, 1}, .rotation = {0, 0, 0, 1}},
        {.kind = mvfx_colliderHeightfield, .halfExtents = {1, 1, 1}, .rotation = {0, 0, 0, 1}},
        {.kind = mvfx_colliderSphere, .inside = true, .radius = 1.0f, .rotation = {0, 0, 0, 1}}};
    mvfxResolvedCollider kept[3];
    CHECK(mvfxReachable(always, 3, lo, hi, kept) == 3, "boxes, fields and inside spheres kept");
    // A sphere is left out of a box wholly below it as of one above.
    const mvfxResolvedCollider ball = {
        .kind = mvfx_colliderSphere, .radius = 1.0f, .rotation = {0, 0, 0, 1}};
    const float below[2][3] = {{-5, -5, -5}, {-4, -4, -4}};
    const float above[2][3] = {{4, 4, 4}, {5, 5, 5}};
    CHECK(mvfxReachable(&ball, 1, below[0], below[1], kept) == 0 &&
              mvfxReachable(&ball, 1, above[0], above[1], kept) == 0,
          "a sphere left out on either side");
    // A point in front of a plane exactly, behind it as float arithmetic
    // has it: the plane is kept, the margin past that rounding.
    const mvfxResolvedCollider tilted = {.kind = mvfx_colliderPlane,
                                         .center = {0.1f, 0.2f, 0.3f},
                                         .normal = {0.48f, 0.6f, 0.64f},
                                         .rotation = {0, 0, 0, 1}};
    bool found = false;
    bool keptAll = true;
    for (int32_t trial = 0; trial < 200000; ++trial)
    {
        float s[3] = {Uniform(-100, 100), Uniform(-100, 100), 0};
        // On the plane in z, then a few ulps either way.
        double z = (double)tilted.center[2] -
                   ((double)tilted.normal[0] * ((double)s[0] - (double)tilted.center[0]) +
                    (double)tilted.normal[1] * ((double)s[1] - (double)tilted.center[1])) /
                       (double)tilted.normal[2];
        s[2] = nextafterf((float)z, trial % 2 == 0 ? INFINITY : -INFINITY);
        double distance = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            distance += (double)tilted.normal[k] * ((double)s[k] - (double)tilted.center[k]);
        }
        float a[3] = {s[0] - tilted.center[0], s[1] - tilted.center[1], s[2] - tilted.center[2]};
        float rounded = a[0] * tilted.normal[0] + a[1] * tilted.normal[1] + a[2] * tilted.normal[2];
        if (distance > 0.0 && rounded < 0.0f)
        {
            found = true;
            keptAll = keptAll && mvfxReachable(&tilted, 1, s, s, kept) == 1;
        }
    }
    CHECK(found && keptAll, "a plane kept where rounding alone puts a point behind it");
}

static void TestMayMeet(void)
{
    // Runs of 64 segments at random about a plane and an outside sphere,
    // at scales from 1 to 100: every segment that meets either is marked,
    // and many are not; any other kind marks every segment.
    enum
    {
        RUN = 64
    };
    bool superset = true;
    int32_t unmarked = 0;
    int32_t meeting = 0;
    for (int32_t trial = 0; trial < 200; ++trial)
    {
        float scale = trial % 3 == 0 ? 100.0f : 1.0f;
        mvfxResolvedCollider pair[2] = {{.kind = mvfx_colliderPlane, .rotation = {0, 0, 0, 1}},
                                        {.kind = mvfx_colliderSphere,
                                         .radius = Uniform(0.1f, 1.0f) * scale,
                                         .rotation = {0, 0, 0, 1}}};
        float n[3] = {Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)};
        float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int32_t k = 0; k < 3; ++k)
        {
            pair[0].normal[k] = n[k] / length;
            pair[0].center[k] = Uniform(-1, 1) * scale;
            pair[1].center[k] = Uniform(-1, 1) * scale;
        }
        float s[3][RUN];
        float e[3][RUN];
        for (int32_t j = 0; j < RUN; ++j)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                s[k][j] = Uniform(-2, 2) * scale;
                e[k][j] = s[k][j] + Uniform(-0.5f, 0.5f) * scale;
            }
        }
        const float* const starts[3] = {s[0], s[1], s[2]};
        const float* const ends[3] = {e[0], e[1], e[2]};
        uint8_t marks[RUN];
        mvfxMayMeet(pair, 2, starts, ends, RUN, marks);
        for (int32_t j = 0; j < RUN; ++j)
        {
            const float a[3] = {s[0][j], s[1][j], s[2][j]};
            const float b[3] = {e[0][j], e[1][j], e[2][j]};
            bool meets = Meets(&pair[0], a, b) || Meets(&pair[1], a, b);
            meeting += meets ? 1 : 0;
            superset = superset && (!meets || marks[j] == 1);
            unmarked += marks[j] == 0 ? 1 : 0;
        }
    }
    CHECK(superset && meeting > 1000 && unmarked > 1000,
          "every segment that meets a plane or a sphere marked, many not");
    const mvfxResolvedCollider box = {
        .kind = mvfx_colliderBox, .halfExtents = {1, 1, 1}, .rotation = {0, 0, 0, 1}};
    const float far[3][1] = {{100}, {100}, {100}};
    const float* const ends[3] = {far[0], far[1], far[2]};
    uint8_t mark = 0;
    mvfxMayMeet(&box, 1, ends, ends, 1, &mark);
    CHECK(mark == 1, "any other kind marks every segment");
}

int main(void)
{
    TestGaps();
    TestReach();
    TestMayMeet();
    return s_failures == 0 ? 0 : 1;
}
