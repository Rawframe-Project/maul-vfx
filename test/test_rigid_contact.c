// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles' contacts with boxes, found at crafted poses: two edges
// crossing off their middles meeting at their crossing; a box turned on a
// smaller box's face meeting it at the eight corners of their overlap; a
// box's face on a turned box's edge meeting it there, on the collider's
// face toward the body; a capsule over a box's corner meeting it by its
// deepest point.

#include "rigid_contact.h"
#include "test_harness.h"

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

static mvfxBodyContacts Gather(const mvfxResolvedCollider* c, const mvfxBodyPose* pose,
                               const mvfxRigidDef* rigid, const float size[3])
{
    float inverseMass = 0.0f;
    float inverseInertia[3];
    mvfxBodyShape shape = mvfxBodyShapeOf(rigid, size, &inverseMass, inverseInertia);
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

int main(void)
{
    TestCrossingEdges();
    TestOverlap();
    TestFaceOnEdge();
    TestCapsuleCorner();
    return s_failures == 0 ? 0 : 1;
}
