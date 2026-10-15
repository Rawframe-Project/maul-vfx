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
struct mvfxResolvedDepth;

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
    // The grid's values' serial as resolved: a collider kept from a step
    // before is compared by it and never reads its grid, which the host
    // may have changed or destroyed since.
    uint64_t gridSerial;
} mvfxResolvedCollider;

bool mvfxIsColliderValid(const mvfxCollider* collider);

bool mvfxIsCollisionValid(const mvfxCollision* collision);

// A valid collider as compiled: its normal and rotation normalized.
mvfxCollider mvfxCompileCollider(const mvfxCollider* collider);

// Resolves a compiled collider and its grid (NULL for a primitive) with
// the map from its space, grown by a radius in the particles' space.
mvfxResolvedCollider mvfxResolveCollider(const mvfxCollider* collider, const struct mvfxGrid* grid,
                                         const mvfxSpaceMap* map, float radius);

// A point's signed distance to a resolved collider's surface, positive on
// its allowed side: above a plane, outside a sphere or a box (inside one
// that keeps particles inside), a grid's field (INFINITY outside its
// box). With bounds, a grid's distance outside its box instead (below 0
// inside), not reading its grid.
float mvfxColliderGap(const mvfxResolvedCollider* c, const float point[3], bool bounds);

// Whether two resolved colliders are the same, field by field (a grid's
// by its serial too).
bool mvfxSameCollider(const mvfxResolvedCollider* a, const mvfxResolvedCollider* b);

// Whether two resolved colliders are the same but for their grid's
// values.
bool mvfxSameColliderShape(const mvfxResolvedCollider* a, const mvfxResolvedCollider* b);

// Whether a grid collider seen as before is now after with only its
// grid's values changed, each change since among the points the live
// grid knows changed (either NULL: false).
bool mvfxOnlyPointsChanged(const mvfxResolvedCollider* before, const mvfxResolvedCollider* after);

// A point's distance outside the part of a grid collider's box its grid's
// changed points reach (the cells about them; all of a heightfield's
// height), below 0 inside; reads the live grid's changed points, never
// its values.
float mvfxChangedGap(const mvfxResolvedCollider* c, const float point[3]);

// A grid collider's signed distance at a point in the particles' space,
// positive on its allowed side (a heightfield's across its surface, by
// its slope), INFINITY outside its box; and the unit way toward the
// allowed side there.
float mvfxFieldDistance(const mvfxResolvedCollider* c, const float point[3], float normal[3]);

// A step's first contact: its share of the segment, its place and the
// surface's normal.
typedef struct mvfxContact
{
    float t;
    float point[3];
    float normal[3];
} mvfxContact;

// What a particle collides with beyond the emitter's colliders: its scene
// plane and the instance's depth image, each NULL for none.
typedef struct mvfxOwnColliders
{
    const mvfxResolvedCollider* plane;
    const struct mvfxResolvedDepth* depth;
} mvfxOwnColliders;

// Collides a particle that went from start to place over a step and ends
// it with velocity, with the colliders and its own: place and velocity
// become what the contacts make them, and first the first contact's.
// Returns the contacts.
int32_t mvfxCollide(const mvfxResolvedCollider* colliders, int32_t count,
                    const mvfxOwnColliders* own, const mvfxCollision* response, float restSpeed,
                    const float start[3], float place[3], float velocity[3], mvfxContact* first);

#endif
