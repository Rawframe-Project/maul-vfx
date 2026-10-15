// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles' shapes and their contacts with colliders, found at a
// pose.

#ifndef MAUL_VFX_SRC_RIGID_CONTACT_H
#define MAUL_VFX_SRC_RIGID_CONTACT_H

#include "collide.h"

#include "maul-vfx/effect.h"

// A body's place and orientation (a unit quaternion) in the particles'
// space.
typedef struct mvfxBodyPose
{
    float x[3];
    float q[4];
} mvfxBodyPose;

// A body's shape as spheres about points in its frame (a sphere's center,
// a capsule's two ends, a box's eight corners at radius 0), with a box's
// half sizes and a capsule's half length.
typedef struct mvfxBodyShape
{
    mvfxRigidShape kind;
    float points[8][3];
    int32_t count;
    float radius;
    float half[3];
} mvfxBodyShape;

// A contact: the body's point in its frame, the surface point it meets
// and the surface's normal (toward the body's side), its depth when found,
// the multiplier its normal corrections summed and its normal speed then.
typedef struct mvfxBodyContact
{
    float body[3];
    float surface[3];
    float normal[3];
    float depth;
    float lambda;
    float speed;
    // The collider it meets, by its index.
    int32_t collider;
} mvfxBodyContact;

typedef struct mvfxBodyContacts
{
    mvfxBodyContact items[MVFX_MAX_RIGID_CONTACTS];
    int32_t count;
    int32_t capped;
    // The collider the contacts being added meet.
    int32_t collider;
} mvfxBodyContacts;

// The shape a size gives (in the particles' space), and its inverse mass
// and inverse inertias about its own axes from the density (a shape of no
// volume a point of unit mass that does not turn).
mvfxBodyShape mvfxBodyShapeOf(const mvfxRigidDef* rigid, const float size[3], float* inverseMass,
                              float inverseInertia[3]);

struct mvfxResolvedDepth;

// What a body meets: colliders, its own scene plane (NULL for none) and
// a depth image (NULL for none) with the body's pose at the substep's
// start, which its samples' ways through the image run from.
typedef struct mvfxBodySurfaces
{
    const mvfxResolvedCollider* colliders;
    int32_t count;
    const mvfxResolvedCollider* plane;
    const struct mvfxResolvedDepth* depth;
    const mvfxBodyPose* from;
} mvfxBodySurfaces;

// Adds the contacts of a body of a shape at a pose with its surfaces to a
// list, each deeper than 0, at most MVFX_MAX_RIGID_CONTACTS kept (the
// deepest) and those past counted. Each collider is its patches eight
// apart (a box kept inside one a face), then the plane's and the image's.
void mvfxGatherBodyContacts(const mvfxBodySurfaces* surfaces, const mvfxBodyPose* b,
                            const mvfxBodyShape* s, mvfxBodyContacts* list);

#endif
