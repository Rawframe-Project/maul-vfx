// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Convex polytopes in the particles' space (a box, a posed hull) and what
// meets them: their nearest points apart by GJK (Gilbert, Johnson and
// Keerthi 1988), their overlap by EPA (van den Bergen 2003), and their
// touching faces clipped into contacts.

#ifndef MAUL_VFX_SRC_CONVEX_H
#define MAUL_VFX_SRC_CONVEX_H

#include "hull.h"

// A polytope: its vertices, and its faces' outward unit normals and
// offsets with their corners (loop[faceStart[f]] up to
// loop[faceStart[f + 1]], counterclockwise seen from outside).
typedef struct mvfxPolytope
{
    int32_t vertexCount;
    float vertices[MVFX_MAX_HULL_VERTICES][3];
    int32_t faceCount;
    float planes[MVFX_MAX_HULL_FACES][4];
    const uint16_t* faceStart;
    const uint8_t* loop;
} mvfxPolytope;

// A box of half sizes half at center turned by rotation.
void mvfxBoxPolytope(const float center[3], const float rotation[4], const float half[3],
                     mvfxPolytope* out);

// A hull scaled along its axes, turned by q and moved to x.
void mvfxHullPolytope(const mvfxHull* hull, const float scale[3], const float x[3],
                      const float q[4], mvfxPolytope* out);

// How two polytopes a and b meet: the unit normal from b toward a, their
// distance along it (below 0 where they overlap, by the least it takes to
// part them) and the nearest or deepest points of each; whether a search
// stopped at its most steps.
typedef struct mvfxConvexMeet
{
    float normal[3];
    float distance;
    float onA[3];
    float onB[3];
    bool capped;
} mvfxConvexMeet;

// The most steps GJK and EPA each take: a fixed bound keeps every
// platform's steps the same (the same bits).
#define MVFX_CONVEX_STEPS 64

// The most points and faces EPA's polytope holds: its tetrahedron's and
// one point a step, and twice as many faces.
#define MVFX_EPA_POINTS (4 + MVFX_CONVEX_STEPS)
#define MVFX_EPA_FACES  (2 * MVFX_EPA_POINTS)

// Whether a and b are nearer than margin, and how they meet.
bool mvfxConvexMeets(const mvfxPolytope* a, const mvfxPolytope* b, float margin,
                     mvfxConvexMeet* out);

// The steps a meeting took: GJK's, and EPA's where they overlapped.
typedef struct mvfxConvexSteps
{
    int32_t gjk;
    int32_t epa;
} mvfxConvexSteps;

// mvfxConvexMeets with each search held to most steps (at most
// MVFX_CONVEX_STEPS), counting them. A search stopped there answers with
// what it has, marked capped: GJK the last simplex's point at a distance
// at least the true one, met whatever the margin (they may still touch,
// so the pair is kept, its manifold measuring its points' depths, rather
// than missed for the step); EPA the nearest face it found, at a depth at
// most the true one.
bool mvfxConvexMeetsWithin(const mvfxPolytope* a, const mvfxPolytope* b, float margin, int32_t most,
                           mvfxConvexMeet* out, mvfxConvexSteps* steps);

// The polytope EPA grows in at most most steps (at most
// MVFX_CONVEX_STEPS) for a and b where GJK finds they overlap, for the test
// hooks: its points (x, y and z each, of the difference a less b) into
// points, their count in pointCount, and its faces (counterclockwise from
// outside, indices into points) into faces; their count, 0 when GJK finds
// them apart or the difference is flat.
int32_t mvfxEpaPolytope(const mvfxPolytope* a, const mvfxPolytope* b, int32_t most,
                        double points[MVFX_EPA_POINTS][3], int32_t* pointCount,
                        uint8_t faces[MVFX_EPA_FACES][3]);

// The point of a polytope nearest p (p itself inside it) and their
// distance (0 inside).
float mvfxPolytopeNearest(const mvfxPolytope* a, const float p[3], float out[3]);

// A contact point of a with b: a's point and b's surface point under it
// along the normal, and their gap along it (below 0 inside).
typedef struct mvfxConvexPoint
{
    float onA[3];
    float onB[3];
    float depth;
} mvfxConvexPoint;

// The most points a manifold has.
#define MVFX_MAX_MANIFOLD_POINTS (2 * MVFX_MAX_HULL_VERTICES)

// The contact points of a and b meeting as meet says: one face of one
// clipped by the other's face most against it, the points nearer than
// margin; meet's own points when no face meets a face. Returns how many,
// and sets the normal (from b toward a) they share.
int32_t mvfxConvexManifold(const mvfxPolytope* a, const mvfxPolytope* b, const mvfxConvexMeet* meet,
                           float margin, float normal[3],
                           mvfxConvexPoint points[MVFX_MAX_MANIFOLD_POINTS]);

#endif
