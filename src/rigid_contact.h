// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles' shapes and their contacts with colliders, found at a
// pose.

#ifndef MAUL_VFX_SRC_RIGID_CONTACT_H
#define MAUL_VFX_SRC_RIGID_CONTACT_H

#include "collide.h"
#include "rigid_shape.h"

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

// Adds the contacts of body a with body b of the same kind (an emitter's
// bodies share one) to a list as a's with b's surface: each its point on
// a (in a's frame), b's surface point (in the particles' space) and the
// normal from b toward a. Spheres and boxes meet as a body meets a sphere
// or a box collider; capsules by their cores.
void mvfxGatherPairContacts(const mvfxBodyPose* a, const mvfxBodyShape* sa, const mvfxBodyPose* b,
                            const mvfxBodyShape* sb, mvfxBodyContacts* list);

#endif
