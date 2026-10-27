// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The contact module's own output, which no public call shows: rigid
// particles' contacts with boxes, found at crafted poses: two edges
// crossing off their middles meeting at their crossing; a box turned on a
// smaller box's face meeting it at the eight corners of their overlap; a
// box's face on a turned box's edge meeting it there, on the collider's
// face toward the body; a capsule over a box's corner meeting it by its
// deepest point, and lying flat on a box's face by its ends only; boxes
// stacked face to face meeting at their four corners once each, square or
// a little turned; a box just above a plane met within the margin and not
// past it; capsules crossed meeting at their
// crossing, and side by side at both ends; a box's and a hull's edge laid
// across a heightfield's crest meeting it there; passes alternating their
// order; a twist held by static friction while it can; two bodies on a
// patch each tipping the way friction pushes it; a pair's twist and spin
// against a body of a turning unit 2^140 past its own, in the smaller;
// a needle's inverse inertia along (1, 1, 1) held at 2^40.
// What the contacts make bodies do (stacking, tipping, resting, an edge
// held up on a crest) is tested through the public interface in
// test_rigid.c, test_rigid_hull.c and test_rigid_sleep.c.

#include "grid_api.h"
#include "hull.h"
#include "rigid_body.h"
#include "rigid_contact.h"
#include "test_harness.h"

#include <float.h>
#include <math.h>

static const mvfxRigidDef s_box = {.enabled = true, .shape = mvfx_rigidBox, .density = 1.0f};

// A unit quaternion turning by angle about axis k.
static void TurnAbout(int32_t k, float angle, float out[4])
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[k] = sinf(0.5f * angle);
    out[3] = cosf(0.5f * angle);
}

static mvfxResolvedCollider BoxCollider(float x, float y, float z, const float rotation[4])
{
    mvfxResolvedCollider c = {.kind = mvfx_colliderBox, .halfExtents = {x, y, z}};
    for (int32_t k = 0; k < 4; ++k)
    {
        c.rotation[k] = rotation[k];
    }
    return c;
}

// A hull for hull shapes, set by the test that uses them.
static mvfxHull s_hull;

static mvfxBodyContacts Gather(const mvfxResolvedCollider* c, const mvfxBodyPose* pose,
                               const mvfxRigidDef* rigid, const float size[3])
{
    mvfxBodyMass mass;
    mvfxBodyShape shape = mvfxBodyShapeOf(rigid, &s_hull, size, &mass);
    mvfxBodyContacts list = {.count = 0};
    const mvfxBodySurfaces surfaces = {.colliders = c, .count = 1};
    mvfxGatherBodyContacts(&surfaces, pose, &shape, &list);
    return list;
}

static void TestCrossingEdges(void)
{
    // A beam along x on an edge (turned an eighth about x), its top at
    // 0.2 root 2; a unit box on an edge along z (an eighth about z), 0.01
    // into it, its center at x 0.2 and z 0.3: they meet at x 0.2, z 0, a
    // body point 0.3 back along its edge.
    float beam[4];
    TurnAbout(0, 0.78539816f, beam);
    mvfxResolvedCollider c = BoxCollider(3, 0.2f, 0.2f, beam);
    float top = 0.2f * 1.41421356f;
    float corner = 0.5f * 1.41421356f;
    mvfxBodyPose pose = {.x = {0.2f, top + corner - 0.01f, 0.3f}};
    TurnAbout(2, 0.78539816f, pose.q);
    static const float unit[3] = {1, 1, 1};
    mvfxBodyContacts list = Gather(&c, &pose, &s_box, unit);
    bool met = list.count == 1;
    if (met)
    {
        const mvfxBodyContact* contact = &list.items[0];
        met = fabsf(contact->depth + 0.01f) <= 1e-4f && fabsf(contact->normal[1] - 1.0f) <= 1e-4f &&
              fabsf(contact->surface[0] - 0.2f) <= 1e-4f &&
              fabsf(contact->surface[1] - top) <= 1e-4f && fabsf(contact->surface[2]) <= 1e-4f &&
              fabsf(contact->body[2] + 0.3f) <= 1e-4f;
    }
    CHECK(met, "edges meet at their crossing");
}

static void TestOverlap(void)
{
    // A 2 by 1 by 2 box turned an eighth about y on a box of 1.6 by 1 by
    // 1.6, 0.01 into its top: their faces overlap in an octagon, met at
    // its eight corners.
    static const float identity[4] = {0, 0, 0, 1};
    mvfxResolvedCollider c = BoxCollider(0.8f, 0.5f, 0.8f, identity);
    mvfxBodyPose pose = {.x = {0, 0.99f, 0}};
    TurnAbout(1, 0.78539816f, pose.q);
    static const float slab[3] = {2, 1, 2};
    mvfxBodyContacts list = Gather(&c, &pose, &s_box, slab);
    bool met = list.count == 8;
    for (int32_t i = 0; met && i < list.count; ++i)
    {
        const mvfxBodyContact* contact = &list.items[i];
        float x = fabsf(contact->surface[0]);
        float z = fabsf(contact->surface[2]);
        met = fabsf(contact->depth + 0.01f) <= 1e-4f &&
              fabsf(contact->surface[1] - 0.5f) <= 1e-4f && x <= 0.8f + 1e-4f &&
              z <= 0.8f + 1e-4f && x + z <= 1.41421356f + 1e-4f &&
              (fabsf(x - 0.8f) <= 1e-4f || fabsf(z - 0.8f) <= 1e-4f);
    }
    CHECK(met, "an octagon's eight corners");
}

static void TestFaceOnEdge(void)
{
    // A unit box's bottom face 0.01 into the top edge of a box of 1 by 1
    // by 2 turned 150 degrees about z (its -y face up): met on that edge.
    float turned[4];
    TurnAbout(2, 2.6179939f, turned);
    mvfxResolvedCollider c = BoxCollider(0.5f, 0.5f, 1, turned);
    float edge = 0.5f * 0.5f + 0.5f * 0.8660254f;
    mvfxBodyPose pose = {.x = {0, edge + 0.49f, 0}, .q = {0, 0, 0, 1}};
    static const float unit[3] = {1, 1, 1};
    mvfxBodyContacts list = Gather(&c, &pose, &s_box, unit);
    bool met = list.count >= 1;
    for (int32_t i = 0; met && i < list.count; ++i)
    {
        const mvfxBodyContact* contact = &list.items[i];
        met = fabsf(contact->surface[1] - edge) <= 1e-3f &&
              fabsf(contact->depth + 0.01f) <= 1e-3f && fabsf(contact->normal[1] - 1.0f) <= 1e-4f;
    }
    CHECK(met, "a face on an edge");
}

static void TestCapsuleCorner(void)
{
    // A capsule lying along x (turned a quarter about z), its core 0.4
    // above a unit box's top corner at (0.5, 0.5, 0.5) less 0.01, its
    // ends far past the corner: met by its deepest point, over the corner.
    static const mvfxRigidDef capsule = {
        .enabled = true, .shape = mvfx_rigidCapsule, .density = 1.0f};
    mvfxBodyPose pose = {.x = {0.5f, 0.5f + 0.39f, 0.5f}};
    TurnAbout(2, 1.5707963f, pose.q);
    static const float identity[4] = {0, 0, 0, 1};
    mvfxResolvedCollider c = BoxCollider(0.5f, 0.5f, 0.5f, identity);
    c.center[0] = -0.4f;
    c.center[2] = 0.0f;
    static const float rod[3] = {0.8f, 4, 0.8f};
    mvfxBodyContacts list = Gather(&c, &pose, &capsule, rod);
    float deepest = 0.0f;
    for (int32_t i = 0; i < list.count; ++i)
    {
        deepest = fminf(deepest, list.items[i].depth);
    }
    CHECK(list.count >= 1 && fabsf(deepest + 0.01f) <= 1e-3f, "a capsule over a corner");
}

static void TestCapsuleFlat(void)
{
    // A capsule of length 2 lying along x on a wide box's top, 0.01 into
    // it: its core is as deep all along, so it meets the box at its ends
    // only, never at a middle point rounding would place.
    static const mvfxRigidDef capsule = {
        .enabled = true, .shape = mvfx_rigidCapsule, .density = 1.0f};
    mvfxBodyPose pose = {.x = {0.1f, 0.5f + 0.25f - 0.01f, 0.2f}};
    TurnAbout(2, 1.5707963f, pose.q);
    static const float identity[4] = {0, 0, 0, 1};
    mvfxResolvedCollider c = BoxCollider(2.0f, 0.5f, 2.0f, identity);
    static const float rod[3] = {0.5f, 2, 0.5f};
    mvfxBodyContacts list = Gather(&c, &pose, &capsule, rod);
    bool ends = list.count == 2;
    for (int32_t i = 0; ends && i < list.count; ++i)
    {
        ends = fabsf(list.items[i].depth + 0.01f) <= 1e-4f &&
               fabsf(fabsf(list.items[i].surface[0] - 0.1f) - 0.75f) <= 1e-4f;
    }
    CHECK(ends, "a capsule lying flat by its ends");
}

// A heightfield of three points a side over x and z from -2 to 2: flat,
// or a bump of 0.3 at its middle.
static mvfxResolvedCollider Heights(mvfxContext* context, bool bump)
{
    static const float flat[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    static const float raised[9] = {0, 0, 0, 0, 0.3f, 0, 0, 0, 0};
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 3;
    def.size[2] = 3;
    def.values = bump ? raised : flat;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(context, &def, &grid) == mvfx_success, "heights");
    return (mvfxResolvedCollider){.kind = mvfx_colliderHeightfield,
                                  .halfExtents = {2, 0, 2},
                                  .rotation = {0, 0, 0, 1},
                                  .grid = mvfxContextGrid(context, grid),
                                  .scale = 1.0f};
}

static void TestHeights(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = nullptr;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    // A box 0.01 into flat heights meets them at its four lower corners,
    // its bottom face's center left out (no bulge under it).
    mvfxResolvedCollider flat = Heights(context, false);
    const mvfxBodyPose level = {.x = {0.3f, 0.49f, 0.2f}, .q = {0, 0, 0, 1}};
    static const float unit[3] = {1, 1, 1};
    mvfxBodyContacts box = Gather(&flat, &level, &s_box, unit);
    bool corners = box.count == 4;
    for (int32_t i = 0; corners && i < box.count; ++i)
    {
        corners = fabsf(box.items[i].depth + 0.01f) <= 1e-4f &&
                  fabsf(fabsf(box.items[i].body[0]) - 0.5f) <= 1e-6f;
    }
    CHECK(corners, "flat heights by the corners");
    // A capsule of length 2 lying along x, 0.01 into the bump's top by
    // its core's middle, its ends above the slopes: met in the middle.
    static const mvfxRigidDef capsule = {
        .enabled = true, .shape = mvfx_rigidCapsule, .density = 1.0f};
    mvfxResolvedCollider bump = Heights(context, true);
    mvfxBodyPose lying = {.x = {0, 0.3f + 0.25f - 0.01f, 0}};
    TurnAbout(2, 1.5707963f, lying.q);
    static const float rod[3] = {0.5f, 2, 0.5f};
    mvfxBodyContacts core = Gather(&bump, &lying, &capsule, rod);
    float deepest = 0.0f;
    for (int32_t i = 0; i < core.count; ++i)
    {
        deepest = fminf(deepest, core.items[i].depth);
    }
    CHECK(fabsf(deepest + 0.01f) <= 2e-3f, "a capsule on a bump by its middle");
    // A cube's hull scaled as the box meets them likewise, at its four
    // lower corners; over the bump by its bottom face's center as well.
    static const float cube[24] = {-0.5f, -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,
                                   -0.5f, 0.5f,  0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,  0.5f,
                                   -0.5f, 0.5f,  -0.5f, 0.5f,  0.5f,  0.5f,  0.5f,  0.5f};
    CHECK(mvfxBuildHull(cube, 8, &(mvfxAllocator){0}, &s_hull) == mvfx_success, "a cube's hull");
    static const mvfxRigidDef hull = {.enabled = true, .shape = mvfx_rigidHull, .density = 1.0f};
    mvfxBodyContacts hullCorners = Gather(&flat, &level, &hull, unit);
    bool four = hullCorners.count == 4;
    for (int32_t i = 0; four && i < hullCorners.count; ++i)
    {
        four = fabsf(hullCorners.items[i].depth + 0.01f) <= 1e-4f &&
               fabsf(hullCorners.items[i].body[1] + 0.5f) <= 1e-6f;
    }
    const mvfxBodyPose perched = {.x = {0, 0.79f, 0}, .q = {0, 0, 0, 1}};
    mvfxBodyContacts top = Gather(&bump, &perched, &hull, unit);
    bool center = false;
    for (int32_t i = 0; i < top.count; ++i)
    {
        center = center || (fabsf(top.items[i].body[0]) <= 1e-6f &&
                            fabsf(top.items[i].body[2]) <= 1e-6f && top.items[i].depth < 0.0f);
    }
    CHECK(four && center, "a hull on heights by its corners and its face over a bump");
    // A ball of radius 0.1 whose center the cube's hull swallows 0.2 in
    // from its +X face: pushed back out through that face, 0.3 deep, the
    // face's point against the ball's.
    const mvfxResolvedCollider ball = {
        .kind = mvfx_colliderSphere, .center = {0.3f, 0.05f, -0.1f}, .radius = 0.1f};
    const mvfxBodyPose around = {.q = {0, 0, 0, 1}};
    mvfxBodyContacts swallowed = Gather(&ball, &around, &hull, unit);
    bool out = swallowed.count == 1;
    if (out)
    {
        const mvfxBodyContact* contact = &swallowed.items[0];
        out = fabsf(contact->depth + 0.3f) <= 1e-5f && fabsf(contact->normal[0] + 1.0f) <= 1e-6f &&
              fabsf(contact->surface[0] - 0.2f) <= 1e-5f && fabsf(contact->body[0] - 0.5f) <= 1e-5f;
    }
    CHECK(out, "a ball's center inside a hull");
    mvfxDestroyContext(context);
}

static void TestFaceToFace(void)
{
    // A unit box 0.001 into the top of another, square or turned 1e-4
    // about x or z: four contacts, at four distinct corners.
    static const float unit[3] = {1, 1, 1};
    mvfxBodyMass mass;
    mvfxBodyShape shape = mvfxBodyShapeOf(&s_box, nullptr, unit, &mass);
    bool four = true;
    for (int32_t turn = 0; turn < 3; ++turn)
    {
        const mvfxBodyPose lower = {.x = {0, 0.5f, 0}, .q = {0, 0, 0, 1}};
        mvfxBodyPose upper = {.x = {0, 1.499f, 0}};
        TurnAbout(turn == 2 ? 2 : 0, turn == 0 ? 0.0f : 1e-4f, upper.q);
        mvfxBodyContacts list = {.count = 0};
        mvfxGatherPairContacts(&lower, &shape, &upper, &shape, &list);
        four = four && list.count == 4;
        for (int32_t i = 0; four && i < list.count; ++i)
        {
            for (int32_t j = 0; j < i; ++j)
            {
                float dx = list.items[i].body[0] - list.items[j].body[0];
                float dz = list.items[i].body[2] - list.items[j].body[2];
                four = four && dx * dx + dz * dz > 0.5f;
            }
        }
    }
    CHECK(four, "faces meet at four corners once each");
}

static void TestMargin(void)
{
    // A unit box 0.005 above a plane is met at its four lower corners (the
    // margin 0.02 of its reach is 0.017), not 0.05 above.
    const mvfxResolvedCollider plane = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};
    static const float unit[3] = {1, 1, 1};
    const mvfxBodyPose near = {.x = {0, 0.505f, 0}, .q = {0, 0, 0, 1}};
    const mvfxBodyPose far = {.x = {0, 0.55f, 0}, .q = {0, 0, 0, 1}};
    mvfxBodyContacts met = Gather(&plane, &near, &s_box, unit);
    mvfxBodyContacts none = Gather(&plane, &far, &s_box, unit);
    bool corners = met.count == 4;
    for (int32_t i = 0; corners && i < met.count; ++i)
    {
        corners = fabsf(met.items[i].depth - 0.005f) <= 1e-5f;
    }
    CHECK(corners && none.count == 0, "met within the margin");
}

static void TestCapsulePairs(void)
{
    // Capsules of radius 0.25 and core 1.5: one along x, one along z over
    // it 0.01 into it meeting at the crossing; two along x side by side
    // 0.01 into each other meeting at both ends.
    static const mvfxRigidDef capsule = {
        .enabled = true, .shape = mvfx_rigidCapsule, .density = 1.0f};
    static const float rod[3] = {0.5f, 2, 0.5f};
    mvfxBodyMass mass;
    mvfxBodyShape shape = mvfxBodyShapeOf(&capsule, nullptr, rod, &mass);
    mvfxBodyPose lower = {.x = {0, 0, 0}};
    TurnAbout(2, 1.5707963f, lower.q);
    mvfxBodyPose over = {.x = {0, 0.49f, 0}};
    TurnAbout(0, 1.5707963f, over.q);
    mvfxBodyContacts crossed = {.count = 0};
    mvfxGatherPairContacts(&over, &shape, &lower, &shape, &crossed);
    float deepest = 0.0f;
    int32_t inside = 0;
    for (int32_t i = 0; i < crossed.count; ++i)
    {
        deepest = fminf(deepest, crossed.items[i].depth);
        inside += crossed.items[i].depth < 0.0f ? 1 : 0;
    }
    CHECK(inside == 1 && fabsf(deepest + 0.01f) <= 1e-4f &&
              fabsf(crossed.items[0].normal[1] - 1.0f) <= 1e-4f,
          "crossed capsules at the crossing");
    mvfxBodyPose beside = {.x = {0, 0, 0.49f}};
    TurnAbout(2, 1.5707963f, beside.q);
    mvfxBodyContacts parallel = {.count = 0};
    mvfxGatherPairContacts(&beside, &shape, &lower, &shape, &parallel);
    int32_t ends = 0;
    for (int32_t i = 0; i < parallel.count; ++i)
    {
        bool end = fabsf(fabsf(parallel.items[i].surface[0]) - 0.75f) <= 1e-3f;
        ends += end && fabsf(parallel.items[i].depth + 0.01f) <= 1e-4f ? 1 : 0;
    }
    CHECK(ends == 2, "capsules side by side at both ends");
}

static void TestCrest(void)
{
    // A ridge 0.3 high along x, 0.5 wide at its foot (heights 0.25 apart
    // in z), and a unit box and a cube's hull turned an eighth about z, the
    // edge below along z laid across the ridge 0.01 into its crest, its
    // corners 0.29 above the flat on either side: met at the crest by the
    // edge's middle, not left to sink until a corner meets the flat; and
    // with the crest under a quarter of the edge, there.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = nullptr;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    float heights[3 * 17] = {0};
    for (int32_t x = 0; x < 3; ++x)
    {
        heights[8 * 3 + x] = 0.3f;
    }
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 3;
    def.size[2] = 17;
    def.values = heights;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(context, &def, &grid) == mvfx_success, "ridge");
    const mvfxResolvedCollider ridge = {.kind = mvfx_colliderHeightfield,
                                        .halfExtents = {2, 0, 2},
                                        .rotation = {0, 0, 0, 1},
                                        .grid = mvfxContextGrid(context, grid),
                                        .scale = 1.0f};
    mvfxBodyPose across = {.x = {0, 0.29f + 0.70710678f, 0}};
    TurnAbout(2, 0.78539816f, across.q);
    static const float unit[3] = {1, 1, 1};
    static const float cube[24] = {-0.5f, -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,
                                   -0.5f, 0.5f,  0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,  0.5f,
                                   -0.5f, 0.5f,  -0.5f, 0.5f,  0.5f,  0.5f,  0.5f,  0.5f};
    CHECK(mvfxBuildHull(cube, 8, &(mvfxAllocator){0}, &s_hull) == mvfx_success, "a cube's hull");
    static const mvfxRigidDef hull = {.enabled = true, .shape = mvfx_rigidHull, .density = 1.0f};
    const mvfxRigidDef* shapes[2] = {&s_box, &hull};
    bool met = true;
    // The edge across the crest at its middle, then at a quarter along.
    for (int32_t i = 0; i < 4; ++i)
    {
        across.x[2] = i < 2 ? 0.0f : 0.25f;
        mvfxBodyContacts list = Gather(&ridge, &across, shapes[i % 2], unit);
        bool crest = false;
        for (int32_t c = 0; c < list.count; ++c)
        {
            const mvfxBodyContact* contact = &list.items[c];
            crest = crest || (fabsf(contact->depth + 0.01f) <= 1e-3f &&
                              fabsf(contact->surface[2]) <= 0.01f &&
                              fabsf(contact->surface[1] - 0.3f) <= 0.01f);
        }
        met = met && crest;
    }
    CHECK(met, "an edge across a crest");
    mvfxDestroyContext(context);
}

static void TestSolverRules(void)
{
    // Passes alternate: a backward pass visits first to end from its end.
    bool order = mvfxPassIndex(2, 6, 2, true) == 5 && mvfxPassIndex(2, 6, 5, true) == 2 &&
                 mvfxPassIndex(2, 6, 3, false) == 3;
    CHECK(order, "passes alternate their order");
    // A unit box turned 0.01 about y over a substep: a patch holding up to
    // its static coefficient times the limit cancels the turn; one past it
    // leaves it to dynamic friction.
    const mvfxRigidDef rigid = {.enabled = true, .staticFriction = 0.6f};
    const mvfxRigidSolve solve = {.rigid = &rigid, .h = 1.0f / 480.0f};
    static const float up[3] = {0, 1, 0};
    bool held = false;
    bool kept = false;
    for (int32_t strong = 0; strong < 2; ++strong)
    {
        mvfxBody body = {.q = {0, sinf(0.005f), 0, cosf(0.005f)},
                         .q0 = {0, 0, 0, 1},
                         .inverseMass = 1.0f,
                         .inverseInertia = {6, 6, 6, 0, 0, 0}};
        float twisted = 0.0f;
        mvfxTwistPatch(&solve, &body, NULL, up, strong != 0 ? 100.0f : 1e-6f, &twisted);
        if (strong != 0)
        {
            held = fabsf(body.q[1]) <= 1e-6f && twisted > 0.0f;
        }
        else
        {
            kept = fabsf(body.q[1] - sinf(0.005f)) <= 1e-7f && twisted == 0.0f;
        }
    }
    CHECK(held && kept, "a twist held while friction can");
    // A patch between two bodies, its loaded contacts 0.5 along the slide
    // from their centers and 0.1 against it, its load 1, the upper body slid
    // 0.6 aside over it (held: 0.3 by their masses): each tips the way it
    // is pushed, the upper along the slide, balanced by the 0.5 side, and
    // does not turn; the lower against it, past the 0.1 side's balance,
    // and turns.
    const mvfxRigidDef rubbing = {.enabled = true, .staticFriction = 1.0f};
    const mvfxRigidSolve slide = {.rigid = &rubbing, .h = 1.0f / 480.0f};
    mvfxBody upper = {.x = {0.6f, 0, 0},
                      .q = {0, 0, 0, 1},
                      .q0 = {0, 0, 0, 1},
                      .inverseMass = 1.0f,
                      .inverseInertia = {6, 6, 6, 0, 0, 0}};
    mvfxBody lower = {.q = {0, 0, 0, 1},
                      .q0 = {0, 0, 0, 1},
                      .inverseMass = 1.0f,
                      .inverseInertia = {6, 6, 6, 0, 0, 0}};
    static const float below[3] = {0, -0.5f, 0};
    static const float above[3] = {0, 0.5f, 0};
    const mvfxPatchSpread spreadUpper = {.count = 2,
                                         .offsets = {{0.5f, -0.5f, 0}, {-0.1f, -0.5f, 0}}};
    const mvfxPatchSpread spreadLower = {.count = 2,
                                         .offsets = {{0.5f, 0.5f, 0}, {-0.1f, 0.5f, 0}}};
    float rubbed = 0.0f;
    mvfxRubPatch(&slide, &upper, below, below, &lower, above, above, up, 1.0f, &spreadUpper,
                 &spreadLower, &rubbed);
    CHECK(fabsf(rubbed - 0.3f) <= 1e-6f && fabsf(upper.q[2]) <= 1e-7f && fabsf(lower.q[2]) > 1e-4f,
          "each body tips the way it is pushed");
}

static void TestTurningUnits(void)
{
    // A pair's turning is taken in the smaller of its bodies' turning
    // units: a unit body turned 0.01 about y over a substep, and spinning
    // at 2 about it, against one alike but of a length unit 2^70 (its
    // turning unit 2^140 past the first's), held by static friction up to
    // mvfxTurnLimit's and slowed by dynamic friction: the first's twist is
    // cancelled and its spin stopped, as against the world, the second
    // turned by a share below binary32's range, both finite.
    const mvfxRigidDef rigid = {.enabled = true, .staticFriction = 0.6f};
    const mvfxRigidSolve solve = {.rigid = &rigid, .h = 1.0f / 480.0f};
    static const float up[3] = {0, 1, 0};
    mvfxBody light = {.q = {0, sinf(0.005f), 0, cosf(0.005f)},
                      .q0 = {0, 0, 0, 1},
                      .w = {0, 2, 0},
                      .inverseMass = 1.0f,
                      .inverseInertia = {6, 6, 6, 0, 0, 0}};
    mvfxBody heavy = {.q = {0, 0, 0, 1},
                      .q0 = {0, 0, 0, 1},
                      .inverseMass = 1.0f,
                      .inverseInertia = {6, 6, 6, 0, 0, 0},
                      .lengthUnit = 70};
    float twisted = 0.0f;
    float spun = 0.0f;
    mvfxTwistPatch(&solve, &light, &heavy, up, mvfxTurnLimit(&light, &heavy, 1.0f, 1.0f), &twisted);
    mvfxUnspinPatch(&light, &heavy, up, mvfxTurnLimit(&light, &heavy, 1.0f, 1.0f), &spun);
    bool finite = true;
    for (int32_t k = 0; k < 4; ++k)
    {
        finite = finite && isfinite(light.q[k]) && isfinite(heavy.q[k]) &&
                 (k == 3 || (isfinite(light.w[k]) && isfinite(heavy.w[k])));
    }
    CHECK(finite && fabsf(light.q[1]) <= 1e-6f && fabsf(light.w[1]) <= 1e-6f &&
              fabsf(heavy.q[1]) < FLT_MIN && fabsf(heavy.w[1]) < FLT_MIN,
          "a pair's turning in its smaller turning unit");
}

static void TestNeedleInertia(void)
{
    // A body's inverse inertia is held by its trace: a hull of a cube's
    // vertices and volume whose second moments are 2^-20 along each axis
    // and 2^-20 - 3 2^-43 across each pair has, in its units, an inertia
    // of 3 2^-43 about (1, 1, 1), an inverse of 2^43 / 3 about it, and
    // each of the inverse's diagonals 0.89 2^40, below the most: held, it
    // is at most 2^40 about (1, 1, 1), and still turns there. A built
    // hull's moments are binary32 and cannot make such a needle along
    // (1, 1, 1), so the test sets them.
    static const float cube[24] = {-0.5f, -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,
                                   -0.5f, 0.5f,  0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,  0.5f,
                                   -0.5f, 0.5f,  -0.5f, 0.5f,  0.5f,  0.5f,  0.5f,  0.5f};
    CHECK(mvfxBuildHull(cube, 8, &(mvfxAllocator){0}, &s_hull) == mvfx_success, "a cube's hull");
    for (int32_t k = 0; k < 3; ++k)
    {
        s_hull.moments[k] = 0x1p-20f;
        s_hull.moments[3 + k] = 0x1p-20f - 0x3p-43f;
    }
    static const mvfxRigidDef hull = {.enabled = true, .shape = mvfx_rigidHull, .density = 1.0f};
    static const float unit[3] = {1, 1, 1};
    mvfxBodyMass mass;
    (void)mvfxBodyShapeOf(&hull, &s_hull, unit, &mass);
    const float* m = mass.inverseInertia;
    float about = (m[0] + m[1] + m[2] + 2.0f * (m[3] + m[4] + m[5])) / 3.0f;
    CHECK(about <= 0x1p40f && about >= 0x1p39f, "a needle along (1, 1, 1) held at 2^40");
}

int main(void)
{
    TestHeights();
    TestCrossingEdges();
    TestOverlap();
    TestFaceOnEdge();
    TestCapsuleCorner();
    TestCapsuleFlat();
    TestFaceToFace();
    TestMargin();
    TestCapsulePairs();
    TestCrest();
    TestSolverRules();
    TestTurningUnits();
    TestNeedleInertia();
    return s_failures == 0 ? 0 : 1;
}
