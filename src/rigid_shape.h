// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid particles' shapes, their mass and inertia, and the contacts they
// gather.

#ifndef MAUL_VFX_SRC_RIGID_SHAPE_H
#define MAUL_VFX_SRC_RIGID_SHAPE_H

#include "hull.h"

#include "maul-vfx/effect.h"

// A body's place and orientation (a unit quaternion) in the particles'
// space.
typedef struct mvfxBodyPose
{
    float x[3];
    float q[4];
} mvfxBodyPose;

// A body's shape as count spheres about points in its frame (a sphere's
// center, a capsule's two ends, a box's eight corners and a hull's
// vertices at radius 0, by mvfxShapePoint), with a box's half sizes, a
// capsule's half length, and a hull and its scale.
typedef struct mvfxBodyShape
{
    mvfxRigidShape kind;
    int32_t count;
    float radius;
    float half[3];
    const mvfxHull* hull;
    float scale[3];
} mvfxBodyShape;

// A shape's point i (a box's corner i: bit k set for +k).
void mvfxShapePoint(const mvfxBodyShape* s, int32_t i, float out[3]);

// A body's mass arrays, each of the stride: its inverse mass, its inverse
// inertia's six, its reach, its mass unit's and its length unit's
// exponents.
enum
{
    MVFX_RIGID_MASS_ARRAYS = 10,
    MVFX_RIGID_REACH = 7,
    MVFX_RIGID_MASS_UNIT = 8,
    MVFX_RIGID_LENGTH_UNIT = 9,
};

// A body's inverse mass, and its inertia and inverse inertia about its
// center in its frame (xx, yy, zz, xy, xz, yz), in its own units: a mass
// of 2^massUnit, the power of two that leaves its mass from 1/2 to 1 in
// it, and a length of 2^lengthUnit, the one that leaves its extent from
// 1/2 to 1 (its inertia in the mass unit times the length unit squared).
// Its extent is the largest of the sizes its shape takes: a sphere's
// diameter, a capsule's diameter and length, a box's three sizes, a hull's
// vertices' coordinates at its size. The GPU part takes the same, the
// largest of binary32 values, so both pick the same units (for an extent
// in binary32's normal range: a GPU may flush a subnormal one to 0). A
// body's mass and inertia span binary32's range and past it (a body's
// size is from 0 to MVFX_MAX_EXTENT, its density above 0 to
// MVFX_MAX_EXTENT, and its inertia goes as the density times the fifth
// power of its size), while in its units they are near 1 for any shape
// not much longer one way than another. Every scaling is by a power of
// two, exact within binary32's normal range: an XPBD step's correction
// from its multiplier is the same bits in any mass unit.
typedef struct mvfxBodyMass
{
    float inverseMass;
    float inertia[6];
    float inverseInertia[6];
    int32_t massUnit;
    int32_t lengthUnit;
} mvfxBodyMass;

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
    // The restitution's impulses summed over its passes.
    float applied;
    // On a patch's first contact, its static friction's multipliers and
    // its dynamic friction's impulses summed over the passes, and against
    // its turning likewise.
    float rubbed;
    float slowed;
    float twisted;
    float spun;
    // The collider it meets, by its index.
    int32_t collider;
} mvfxBodyContact;

typedef struct mvfxBodyContacts
{
    mvfxBodyContact items[MVFX_MAX_RIGID_CONTACTS];
    int32_t count;
    int32_t capped;
    // Convex searches stopped at their most steps.
    int32_t searchesCapped;
    // The collider the contacts being added meet.
    int32_t collider;
    // How far apart a point may be and still be a contact.
    float margin;
} mvfxBodyContacts;

// A body's contacts are its points nearer the surfaces than this share of
// its reach (of the smaller one's, between two bodies), not only those
// inside them (speculative contacts, as PhysX's contact offset): a contact
// pushes only once inside, in any pass, so a body tipping in a pass is
// caught by its far side in the same pass. Of points inside alone, a box
// slightly tilted on another met it at two or three, its patch jumped to
// an edge, and a stack rocked.
#define MVFX_CONTACT_MARGIN 0.02f

// A contact between two bodies a and b (by index): its points in each
// one's frame, the normal from b toward a in the particles' space, and as
// a body's contact its normal multiplier, its speed before, its
// restitution's impulses and on a pair's first its friction's. Its
// multipliers and impulses are in the lighter body's mass unit, those
// against turning (twisted, spun) in the smaller of the bodies' mass units
// times their length units squared (rigid_body.h's mvfxTwistPatch).
typedef struct mvfxPairContact
{
    int32_t a;
    int32_t b;
    float onA[3];
    float onB[3];
    float normal[3];
    float lambda;
    float speed;
    float applied;
    float rubbed;
    float slowed;
    float twisted;
    float spun;
} mvfxPairContact;

// The shape a size gives (in the particles' space; a hull's from hull),
// and its mass from the density in its units (a shape of no volume a point
// of unit mass that does not turn, in units of 1).
mvfxBodyShape mvfxBodyShapeOf(const mvfxRigidDef* rigid, const mvfxHull* hull, const float size[3],
                              mvfxBodyMass* mass);

struct mvfxResolvedDepth;

// Adds a contact deeper than 0 to a list; past the cap the shallowest is
// dropped and counted, and each takes the list's patch.
void mvfxAddContact(mvfxBodyContacts* list, const mvfxBodyContact* c);

// Adds the contact of the body's point (in its frame, its place world)
// meeting a surface along unit n at depth, when deeper than 0 (past the
// cap the shallowest dropped).
void mvfxAddBodyContact(mvfxBodyContacts* list, const float point[3], const float world[3],
                        const float n[3], float depth);

// The radius of the least sphere about a shape's center that holds it.
float mvfxBodyReach(const mvfxBodyShape* s);

#endif
