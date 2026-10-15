// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rigid bodies as a substep solves them: their poses, velocities and
// mass, moved by positional and velocity impulses, and their strikes.

#ifndef MAUL_VFX_SRC_RIGID_BODY_H
#define MAUL_VFX_SRC_RIGID_BODY_H

#include "instance.h"
#include "rigid_shape.h"

// A body's flags in its emitter's rigidFlags: its collision event written
// this step; in contact this substep; struck this substep.
enum
{
    MVFX_RIGID_EVENTED = 1,
    MVFX_RIGID_TOUCHED = 2,
    MVFX_RIGID_STRUCK = 4,
};

// A body: its pose and velocities, the pose it started the substep at,
// its inverse mass and its inverse inertia in its frame (xx, yy, zz, xy,
// xz, yz: a hull's is not along its axes).
typedef struct mvfxBody
{
    float x[3];
    float q[4];
    float v[3];
    float w[3];
    float x0[3];
    float q0[4];
    float inverseMass;
    float inverseInertia[6];
} mvfxBody;

// An emitter's rigid particles in a substep: their attributes, their
// start poses (seven arrays of the stride) and mass (its arrays), and
// their hull, for hulls.
typedef struct mvfxRigidSolve
{
    mvfxInstance* instance;
    int32_t emitter;
    const mvfxRigidDef* rigid;
    const mvfxHull* hull;
    float* x[3];
    float* v[3];
    float* q[4];
    float* w[3];
    float* start;
    float* mass;
    uint8_t* flags;
    // For sleeping bodies, their still times and their forces'
    // accelerations asleep (NULL when they never sleep).
    float* sleep;
    float* rest[3];
    size_t stride;
    // Whether they collide, and gravity's size.
    bool collides;
    float gravity;
    float h;
    // Below this speed into a surface a contact does not bounce (twice
    // gravity's speed over a substep).
    float still;
    int64_t tick;
} mvfxRigidSolve;

mvfxBody mvfxLoadBody(const mvfxRigidSolve* s, int32_t i);
void mvfxStoreBody(const mvfxRigidSolve* s, int32_t i, const mvfxBody* b);

// The generalized inverse mass at offset r along unit n.
float mvfxBodyWeight(const mvfxBody* b, const float r[3], const float n[3]);

// Moves the body by a positional impulse p at offset r: its place by p
// over its mass, its orientation by the turn it gives. With both, its
// start pose by the very same step too, so the change between them (its
// velocity to be) is left as it was.
void mvfxBodyPush(mvfxBody* b, const float r[3], const float p[3], bool both);

// Changes the body's velocities by an impulse j at offset r.
void mvfxBodyKick(mvfxBody* b, const float r[3], const float j[3]);

// The velocity of the body's point at offset r.
void mvfxBodyPointVelocity(const mvfxBody* b, const float r[3], float out[3]);

// A patch's loaded contacts as offsets from its center (on the first
// body, in the world): how far it reaches toward a side its body tips to.
typedef struct mvfxPatchSpread
{
    int32_t count;
    float offsets[MVFX_MAX_RIGID_CONTACTS][3];
} mvfxPatchSpread;

// Coulomb friction by patches (PhysX's patch friction): one friction
// constraint for the contacts of a body with one collider or another body,
// at their center on each weighted by their normal multipliers, along
// their weighted normal. Its static part by position, as Muller's: the
// patch's slide over the substep, of body a at offset ra (ra0 at its
// start pose) from body b at rb (rb0), NULL for the world, along unit
// normal n from b toward a, is cancelled while holding it (by the bodies'
// masses), summed over the passes (rubbed), takes at most the static
// coefficient times its normal multipliers' sum, load. Run in each pass with the normal contacts
// (Gauss-Seidel), so the tilt friction below a body's center gives it is
// undone by the contacts in the same substep.
//
// Friction below a body's center tips it, which its patch's normal loads
// resist only once tipped: pushed by position alone, a box held on a
// slope tipped over its front edge each substep, fell back and crept down
// it. While the loads can balance the tipping (the friction times the
// center's height at most the load times the patch's reach toward the
// side the body tips to: its loaded contacts' farthest that way, as the
// load all moves to that edge), the friction acts level with the body's
// center, and a box is held untipped; past that it acts at the patch, the
// contacts' pushes alone resisting, and a tall body tips. Across 10% of
// the balance about it, it moves from one to the other: switched whole, a
// body held at the limit could flip between them each pass. A sphere (no
// spread) rolls. Sharing the friction between the two by the balance
// counted the loads' resistance twice, and a tall box stood on a slope it
// tips on; by the contacts' mean distance (the corners' for a square
// patch) a box tipped 8 degrees late.
void mvfxRubPatch(const mvfxRigidSolve* s, mvfxBody* a, const float ra[3], const float ra0[3],
                  mvfxBody* b, const float rb[3], const float rb0[3], const float n[3], float load,
                  const mvfxPatchSpread* spread, float* rubbed);

// Its dynamic part by velocity, as Muller's, in the restitution's passes:
// the patch's sliding speed cut by impulses summing (slowed) to at most
// the dynamic coefficient times the patch's normal impulse over the
// substep, pressed: its pushes' impulses (their multipliers over h) and
// the restitution's. Taken from the pushes alone, it was the larger for a
// body rocking as it slid, the restitution taking back what the pushes
// gave too much.
void mvfxSlowPatch(mvfxBody* a, const float ra[3], mvfxBody* b, const float rb[3], const float n[3],
                   float pressed, const mvfxPatchSpread* spread, float coefficient, float* slowed);

// The patch's friction against turning about its normal, which friction
// at its center alone does not resist (a box spun on a floor spun on, and
// one held on a slope turned about its normal and wandered across it):
// static by position, the bodies' turn about n over the substep cancelled
// while holding it, summed over the passes (twisted), takes at most the
// static coefficient times limit, the patch's load times its radius (its
// contacts' mean distance from its center, weighted by their normal
// multipliers); dynamic by velocity, the spin about n cut by impulses
// summing (spun) to at most limit, the dynamic coefficient times the
// pressed impulse times the radius.
void mvfxTwistPatch(const mvfxRigidSolve* s, mvfxBody* a, mvfxBody* b, const float n[3],
                    float limit, float* twisted);
void mvfxUnspinPatch(mvfxBody* a, mvfxBody* b, const float n[3], float limit, float* spun);

// What a pass over the contacts does: pushes the start poses out of the
// depth there with the poses (post-stabilization); pushes the poses out
// (press), and adds the static friction (push); restitution and the
// dynamic friction (bounce).
typedef enum mvfxContactStage
{
    mvfx_stageStabilize,
    mvfx_stagePress,
    mvfx_stagePush,
    mvfx_stageBounce,
} mvfxContactStage;

// The step'th of first to end in a pass, backward ones from the end: the
// passes alternate their order (symmetric Gauss-Seidel), so no contact,
// pair or body is always solved last. Each in the same order, a stack of
// boxes turned a little the same way every substep and crept aside.
int32_t mvfxPassIndex(int32_t first, int32_t end, int32_t step, bool backward);

// A body in contact's velocities from its pose's change over the substep.
void mvfxCorrectBody(mvfxBody* b, float h);

// The normal speed restitution takes a contact to, from its speed before.
float mvfxBounceTarget(const mvfxRigidSolve* s, float speed);

// Body i struck a surface at a point along a normal: once a substep it is
// counted, writes its step's first event, and is killed or loses life as
// the collision def says.
void mvfxStrikeBody(const mvfxRigidSolve* s, int32_t i, const float point[3],
                    const float normal[3]);

#endif
