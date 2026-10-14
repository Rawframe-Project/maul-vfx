// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Collision with primitives and grids: the checks, the compiled and
// resolved colliders, and the response along a particle's step: the
// segment's first contact, a bounce with restitution and friction, the
// rest of the step reflected likewise, up to four contacts a step.

#ifndef MAUL_VFX_SRC_COLLIDE_H
#define MAUL_VFX_SRC_COLLIDE_H

#include "force.h"

struct mvfxGrid;

#include "maul-vfx/effect.h"

// The most contacts a particle's step is followed through.
#define MVFX_MAX_CONTACTS 4

// A collider as a step takes it, in the particles' space, grown by the
// particle's radius.
typedef struct mvfxResolvedCollider
{
    mvfxColliderKind kind;
    bool inside;
    float center[3];
    float normal[3];
    float radius;
    float halfExtents[3];
    float rotation[4];
    // A grid collider's grid, the scale of its values and the particle's
    // radius it keeps off by.
    const struct mvfxGrid* grid;
    float scale;
    float grow;
} mvfxResolvedCollider;

bool mvfxIsColliderValid(const mvfxCollider* collider);

bool mvfxIsCollisionValid(const mvfxCollision* collision);

// A valid collider as compiled: its normal and rotation normalized.
mvfxCollider mvfxCompileCollider(const mvfxCollider* collider);

// Resolves a compiled collider and its grid (NULL for a primitive) with
// the map from its space, grown by a radius in the particles' space.
mvfxResolvedCollider mvfxResolveCollider(const mvfxCollider* collider, const struct mvfxGrid* grid,
                                         const mvfxSpaceMap* map, float radius);

// Collides a particle that went from start to place over a step and ends
// it with velocity: place and velocity become what the contacts make
// them. Returns the contacts.
int32_t mvfxCollide(const mvfxResolvedCollider* colliders, int32_t count,
                    const mvfxCollision* response, float restSpeed, const float start[3],
                    float place[3], float velocity[3]);

#endif
