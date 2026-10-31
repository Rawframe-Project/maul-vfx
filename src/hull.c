// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "hull.h"

#include "allocator.h"

#include <float.h>
#include <math.h>
#include <string.h>

// The faces a build keeps at once. A hull of MVFX_MAX_HULL_VERTICES has
// at most 124; a build that needs more is of a hull with too many
// vertices.
#define BUILD_FACES 1024

// A triangle of the hull being built: its points (indices of the input)
// counterclockwise seen from outside, the face across each edge (edge k
// from v[k] to v[k + 1]), its plane, and its farthest point outside.
typedef struct Face
{
    int32_t v[3];
    int32_t adj[3];
    double n[3];
    double d;
    int32_t far;
    double farDistance;
    bool alive;
    bool visible;
} Face;

// A horizon edge: from a to b as the visible face had it, the face beyond.
typedef struct Edge
{
    int32_t a;
    int32_t b;
    int32_t beyond;
} Edge;

typedef struct Build
{
    const float* points;
    int32_t count;
    // The face each point is outside of, -1 for none.
    int32_t* owner;
    Face faces[BUILD_FACES];
    int32_t faceCount;
    int32_t free[BUILD_FACES];
    int32_t freeCount;
    int32_t visible[BUILD_FACES];
    int32_t visibleCount;
    Edge horizon[BUILD_FACES];
    int32_t horizonCount;
    int32_t made[BUILD_FACES];
    // Within this of a plane is on it.
    double tolerance;
} Build;

static void PointOf(const Build* b, int32_t i, double out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (double)b->points[3 * (size_t)i + (size_t)k];
    }
}

static double Dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Cross(const double a[3], const double b[3], double out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void Sub(const double a[3], const double b[3], double out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = a[k] - b[k];
    }
}

// How far point i is above face f.
static double Above(const Build* b, int32_t f, int32_t i)
{
    double p[3];
    PointOf(b, i, p);
    return Dot(b->faces[f].n, p) - b->faces[f].d;
}

// Sets a face's plane from its points; false for a sliver of no area.
static bool PlaneOf(const Build* b, Face* f)
{
    double p[3][3];
    for (int32_t k = 0; k < 3; ++k)
    {
        PointOf(b, f->v[k], p[k]);
    }
    double e1[3];
    double e2[3];
    Sub(p[1], p[0], e1);
    Sub(p[2], p[0], e2);
    Cross(e1, e2, f->n);
    double length = sqrt(Dot(f->n, f->n));
    if (!(length > 0.0))
    {
        return false;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        f->n[k] /= length;
    }
    f->d = Dot(f->n, p[0]);
    return true;
}

// A new face of points a, b, c; -1 when no slot is left or it has no area.
static int32_t MakeFace(Build* b, int32_t v0, int32_t v1, int32_t v2)
{
    int32_t f = b->freeCount > 0 ? b->free[--b->freeCount] : b->faceCount;
    if (f >= BUILD_FACES)
    {
        return -1;
    }
    b->faceCount += f == b->faceCount ? 1 : 0;
    Face* face = &b->faces[f];
    *face = (Face){.v = {v0, v1, v2}, .adj = {-1, -1, -1}, .far = -1, .alive = true};
    return PlaneOf(b, face) ? f : -1;
}

// The index of face f's edge from a to b, -1 for none.
static int32_t EdgeOf(const Face* f, int32_t a, int32_t z)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        if (f->v[k] == a && f->v[(k + 1) % 3] == z)
        {
            return k;
        }
    }
    return -1;
}

// Gives a point to the first of faces it is outside of.
static void Assign(Build* b, int32_t i, const int32_t* faces, int32_t count)
{
    b->owner[i] = -1;
    for (int32_t j = 0; j < count; ++j)
    {
        double above = Above(b, faces[j], i);
        if (above > b->tolerance)
        {
            Face* f = &b->faces[faces[j]];
            b->owner[i] = faces[j];
            if (f->far < 0 || above > f->farDistance)
            {
                f->far = i;
                f->farDistance = above;
            }
            return;
        }
    }
}

// The input's extremes along each axis, and the tolerance: three float
// epsilons of the largest coordinates' sum (Qhull's, in binary64 over
// binary32 inputs).
static void Extremes(Build* b, int32_t low[3], int32_t high[3])
{
    double largest[3] = {0.0, 0.0, 0.0};
    for (int32_t k = 0; k < 3; ++k)
    {
        low[k] = 0;
        high[k] = 0;
    }
    for (int32_t i = 0; i < b->count; ++i)
    {
        double p[3];
        PointOf(b, i, p);
        for (int32_t k = 0; k < 3; ++k)
        {
            double lowest = (double)b->points[3 * (size_t)low[k] + (size_t)k];
            double highest = (double)b->points[3 * (size_t)high[k] + (size_t)k];
            low[k] = p[k] < lowest ? i : low[k];
            high[k] = p[k] > highest ? i : high[k];
            largest[k] = fmax(largest[k], fabs(p[k]));
        }
    }
    b->tolerance = 3.0 * (double)FLT_EPSILON * (largest[0] + largest[1] + largest[2]);
}

// The point farthest from the line through a and z, or from the plane of
// a, z and c when c is not -1; its distance.
static int32_t Farthest(const Build* b, int32_t a, int32_t z, int32_t c, double* distance)
{
    double pa[3];
    double pz[3];
    double axis[3];
    double normal[3] = {0.0, 0.0, 0.0};
    PointOf(b, a, pa);
    PointOf(b, z, pz);
    Sub(pz, pa, axis);
    if (c >= 0)
    {
        double pc[3];
        double side[3];
        PointOf(b, c, pc);
        Sub(pc, pa, side);
        Cross(axis, side, normal);
    }
    int32_t best = a;
    *distance = 0.0;
    for (int32_t i = 0; i < b->count; ++i)
    {
        double p[3];
        double off[3];
        PointOf(b, i, p);
        Sub(p, pa, off);
        double away = 0.0;
        if (c >= 0)
        {
            away = fabs(Dot(off, normal)) / sqrt(Dot(normal, normal));
        }
        else
        {
            double across[3];
            Cross(axis, off, across);
            away = sqrt(Dot(across, across) / Dot(axis, axis));
        }
        if (away > *distance)
        {
            *distance = away;
            best = i;
        }
    }
    return best;
}

// Links each face's edges to the faces across them, among faces.
static void Link(Build* b, const int32_t* faces, int32_t count)
{
    for (int32_t i = 0; i < count; ++i)
    {
        Face* f = &b->faces[faces[i]];
        for (int32_t k = 0; k < 3; ++k)
        {
            for (int32_t j = 0; j < count; ++j)
            {
                int32_t e = EdgeOf(&b->faces[faces[j]], f->v[(k + 1) % 3], f->v[k]);
                f->adj[k] = e >= 0 ? faces[j] : f->adj[k];
            }
        }
    }
}

// The first tetrahedron, its faces turned outward, and the points outside
// it given to its faces; false when the points span no volume.
static bool Start(Build* b)
{
    int32_t low[3];
    int32_t high[3];
    Extremes(b, low, high);
    int32_t axis = 0;
    double spans[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        spans[k] = (double)b->points[3 * (size_t)high[k] + (size_t)k] -
                   (double)b->points[3 * (size_t)low[k] + (size_t)k];
        axis = spans[k] > spans[axis] ? k : axis;
    }
    double distance = 0.0;
    int32_t v0 = low[axis];
    int32_t v1 = high[axis];
    int32_t v2 = Farthest(b, v0, v1, -1, &distance);
    if (!(spans[axis] > b->tolerance) || !(distance > b->tolerance))
    {
        return false;
    }
    int32_t v3 = Farthest(b, v0, v1, v2, &distance);
    if (!(distance > b->tolerance))
    {
        return false;
    }
    const int32_t corners[4][3] = {{v0, v1, v2}, {v0, v3, v1}, {v1, v3, v2}, {v2, v3, v0}};
    const int32_t apex[4] = {v3, v2, v0, v1};
    int32_t faces[4];
    for (int32_t i = 0; i < 4; ++i)
    {
        faces[i] = MakeFace(b, corners[i][0], corners[i][1], corners[i][2]);
        if (faces[i] < 0)
        {
            return false;
        }
        if (Above(b, faces[i], apex[i]) > 0.0)
        {
            // Turned inward: flip it.
            Face* f = &b->faces[faces[i]];
            int32_t swap = f->v[1];
            f->v[1] = f->v[2];
            f->v[2] = swap;
            (void)PlaneOf(b, f);
        }
    }
    Link(b, faces, 4);
    for (int32_t i = 0; i < b->count; ++i)
    {
        Assign(b, i, faces, 4);
    }
    b->owner[v0] = -1;
    b->owner[v1] = -1;
    b->owner[v2] = -1;
    b->owner[v3] = -1;
    return true;
}

// The live face with the farthest point outside it (the lowest such face
// on a tie), -1 for none.
static int32_t NextFace(const Build* b)
{
    int32_t best = -1;
    for (int32_t f = 0; f < b->faceCount; ++f)
    {
        const Face* face = &b->faces[f];
        if (face->alive && face->far >= 0 &&
            (best < 0 || face->farDistance > b->faces[best].farDistance))
        {
            best = f;
        }
    }
    return best;
}

// The faces the eye sees, from the face first, and the horizon's edges
// around them, unordered.
static void SeeFrom(Build* b, int32_t first, int32_t eye)
{
    b->visibleCount = 0;
    b->horizonCount = 0;
    b->faces[first].visible = true;
    b->visible[b->visibleCount++] = first;
    for (int32_t at = 0; at < b->visibleCount; ++at)
    {
        Face* f = &b->faces[b->visible[at]];
        for (int32_t k = 0; k < 3; ++k)
        {
            Face* next = &b->faces[f->adj[k]];
            if (next->visible)
            {
                continue;
            }
            if (Above(b, f->adj[k], eye) > b->tolerance)
            {
                next->visible = true;
                b->visible[b->visibleCount++] = f->adj[k];
                continue;
            }
            b->horizon[b->horizonCount++] =
                (Edge){.a = f->v[k], .b = f->v[(k + 1) % 3], .beyond = f->adj[k]};
        }
    }
}

// Orders the horizon's edges into a loop, each starting where the one
// before ends; false when they make none.
static bool OrderHorizon(Build* b)
{
    for (int32_t i = 1; i < b->horizonCount; ++i)
    {
        int32_t found = -1;
        for (int32_t j = i; j < b->horizonCount && found < 0; ++j)
        {
            found = b->horizon[j].a == b->horizon[i - 1].b ? j : -1;
        }
        if (found < 0)
        {
            return false;
        }
        Edge swap = b->horizon[i];
        b->horizon[i] = b->horizon[found];
        b->horizon[found] = swap;
    }
    return b->horizonCount >= 3 && b->horizon[b->horizonCount - 1].b == b->horizon[0].a;
}

// Adds the eye to the hull: the faces it sees replaced by a cone from it
// to the horizon, the points they held given to the new faces; false when
// no slot is left or the horizon is broken.
static bool AddPoint(Build* b, int32_t first, int32_t eye)
{
    SeeFrom(b, first, eye);
    if (!OrderHorizon(b))
    {
        return false;
    }
    int32_t n = b->horizonCount;
    for (int32_t i = 0; i < n; ++i)
    {
        b->made[i] = MakeFace(b, b->horizon[i].a, b->horizon[i].b, eye);
        if (b->made[i] < 0)
        {
            return false;
        }
    }
    for (int32_t i = 0; i < n; ++i)
    {
        Face* f = &b->faces[b->made[i]];
        Face* beyond = &b->faces[b->horizon[i].beyond];
        f->adj[0] = b->horizon[i].beyond;
        beyond->adj[EdgeOf(beyond, b->horizon[i].b, b->horizon[i].a)] = b->made[i];
        f->adj[1] = b->made[(i + 1) % n];
        f->adj[2] = b->made[(i + n - 1) % n];
    }
    for (int32_t i = 0; i < b->count; ++i)
    {
        if (b->owner[i] >= 0 && b->faces[b->owner[i]].visible)
        {
            Assign(b, i, b->made, n);
        }
    }
    b->owner[eye] = -1;
    for (int32_t i = 0; i < b->visibleCount; ++i)
    {
        b->faces[b->visible[i]].alive = false;
        b->faces[b->visible[i]].visible = false;
        b->free[b->freeCount++] = b->visible[i];
    }
    return true;
}

// Builds the triangles of the hull of the points; mvfx_errorInvalid when
// they span no volume, mvfx_errorLimit when they need more faces than a
// build keeps.
static mvfxResult Triangles(Build* b)
{
    if (!Start(b))
    {
        return mvfx_errorInvalid;
    }
    for (int32_t f = NextFace(b); f >= 0; f = NextFace(b))
    {
        if (!AddPoint(b, f, b->faces[f].far))
        {
            return mvfx_errorLimit;
        }
    }
    return mvfx_success;
}

// Ascending pseudo-angle of (x, y), from 0 to 4 counterclockwise.
static double PseudoAngle(double x, double y)
{
    double sum = fabs(x) + fabs(y);
    double p = sum > 0.0 ? y / sum : 0.0;
    return x >= 0.0 ? (y >= 0.0 ? p : 4.0 + p) : 2.0 - p;
}

// The hull being gathered: each point's hull vertex (-1 none yet), the
// vertices' points, and the faces, each a plane and a loop of vertices.
typedef struct Gather
{
    int32_t vertexCount;
    int32_t pointOf[MVFX_MAX_HULL_VERTICES];
    int32_t faceCount;
    double normals[MVFX_MAX_HULL_FACES][3];
    int32_t loopCount;
    uint16_t faceStart[MVFX_MAX_HULL_FACES + 1];
    uint8_t loop[MVFX_MAX_HULL_LOOP];
} Gather;

// Point i's hull vertex, added when new; -1 past the most.
static int32_t VertexOf(Gather* g, int32_t i)
{
    for (int32_t v = 0; v < g->vertexCount; ++v)
    {
        if (g->pointOf[v] == i)
        {
            return v;
        }
    }
    if (g->vertexCount >= MVFX_MAX_HULL_VERTICES)
    {
        return -1;
    }
    g->pointOf[g->vertexCount] = i;
    return g->vertexCount++;
}

// Whether triangle f lies in face first's plane: facing its way, each of
// its points within four tolerances of the plane. On a convex hull a plane
// meets one face, so triangles of one plane are one face; by their
// normals alone, two triangles 1e-3 apart in angle made a face with a
// corner 3e-4 off its plane.
static bool SameFace(const Build* b, int32_t first, int32_t f)
{
    const Face* plane = &b->faces[first];
    bool on = Dot(plane->n, b->faces[f].n) > 0.5;
    for (int32_t k = 0; k < 3 && on; ++k)
    {
        double p[3];
        PointOf(b, b->faces[f].v[k], p);
        on = fabs(Dot(plane->n, p) - plane->d) <= 4.0 * b->tolerance;
    }
    return on;
}

// Orders a face's corners counterclockwise about its normal n, by their
// angle about their center.
static void OrderCorners(const Build* b, const Gather* g, const double n[3], int32_t* corners,
                         int32_t count)
{
    if (count < 3)
    {
        return;
    }
    double center[3] = {0.0, 0.0, 0.0};
    double p[MVFX_MAX_HULL_VERTICES][3];
    for (int32_t j = 0; j < count; ++j)
    {
        PointOf(b, g->pointOf[corners[j]], p[j]);
        for (int32_t k = 0; k < 3; ++k)
        {
            center[k] += p[j][k] / (double)count;
        }
    }
    double u[3];
    double w[3];
    Sub(p[0], center, u);
    Cross(n, u, w);
    double angles[MVFX_MAX_HULL_VERTICES];
    for (int32_t j = 0; j < count; ++j)
    {
        double off[3];
        Sub(p[j], center, off);
        angles[j] = PseudoAngle(Dot(off, u), Dot(off, w));
    }
    // In order of angle (insertion: a face has few corners).
    for (int32_t j = 1; j < count; ++j)
    {
        for (int32_t i = j; i > 0 && angles[i] < angles[i - 1]; --i)
        {
            double angle = angles[i];
            angles[i] = angles[i - 1];
            angles[i - 1] = angle;
            int32_t corner = corners[i];
            corners[i] = corners[i - 1];
            corners[i - 1] = corner;
        }
    }
}

// Adds the face of the triangles of face first's plane: their points in
// order about the plane's center; mvfx_errorLimit past the most vertices,
// mvfx_errorInvalid for fewer than three corners.
static mvfxResult AddFace(const Build* b, Gather* g, int32_t first, bool* taken)
{
    int32_t corners[MVFX_MAX_HULL_VERTICES];
    int32_t count = 0;
    for (int32_t f = first; f < b->faceCount; ++f)
    {
        if (!b->faces[f].alive || taken[f] || !SameFace(b, first, f))
        {
            continue;
        }
        taken[f] = true;
        for (int32_t k = 0; k < 3; ++k)
        {
            int32_t v = VertexOf(g, b->faces[f].v[k]);
            if (v < 0)
            {
                return mvfx_errorLimit;
            }
            bool seen = false;
            for (int32_t j = 0; j < count; ++j)
            {
                seen = seen || corners[j] == v;
            }
            corners[count] = v;
            count += seen ? 0 : 1;
        }
    }
    if (count < 3)
    {
        return mvfx_errorInvalid;
    }
    OrderCorners(b, g, b->faces[first].n, corners, count);
    g->faceStart[g->faceCount] = (uint16_t)g->loopCount;
    memcpy(g->normals[g->faceCount], b->faces[first].n, sizeof g->normals[0]);
    g->faceCount += 1;
    for (int32_t j = 0; j < count; ++j)
    {
        g->loop[g->loopCount++] = (uint8_t)corners[j];
    }
    g->faceStart[g->faceCount] = (uint16_t)g->loopCount;
    return mvfx_success;
}

// Eberly's subexpressions of one coordinate's integrals over a triangle.
static void Subexpressions(double w0, double w1, double w2, double f[3], double g[3])
{
    double temp0 = w0 + w1;
    double temp1 = w0 * w0;
    double temp2 = temp1 + w1 * temp0;
    f[0] = temp0 + w2;
    f[1] = temp2 + w2 * f[0];
    f[2] = w0 * temp1 + w1 * temp2 + w2 * f[1];
    g[0] = f[1] + w0 * (f[0] + w0);
    g[1] = f[1] + w1 * (f[0] + w1);
    g[2] = f[1] + w2 * (f[0] + w2);
}

// Adds a triangle's share (counterclockwise from outside) to the
// integrals of 1, x, y, z, xx, yy, zz, xy, yz and zx over the volume.
static void Integrate(const double p0[3], const double p1[3], const double p2[3],
                      double integral[10])
{
    double e1[3];
    double e2[3];
    double d[3];
    Sub(p1, p0, e1);
    Sub(p2, p0, e2);
    Cross(e1, e2, d);
    double f[3][3];
    double g[3][3];
    for (int32_t k = 0; k < 3; ++k)
    {
        Subexpressions(p0[k], p1[k], p2[k], f[k], g[k]);
    }
    integral[0] += d[0] * f[0][0];
    for (int32_t k = 0; k < 3; ++k)
    {
        integral[1 + k] += d[k] * f[k][1];
        integral[4 + k] += d[k] * f[k][2];
        int32_t m = (k + 1) % 3;
        integral[7 + k] += d[k] * (p0[m] * g[k][0] + p1[m] * g[k][1] + p2[m] * g[k][2]);
    }
}

// The hull's volume, centroid and second moments about it (Eberly,
// "Polyhedral Mass Properties (Revisited)", 2009), each face a fan, the
// points taken from ref for precision; false for no volume.
static bool MassOf(const Build* b, const Gather* g, const double ref[3], mvfxHull* out)
{
    double integral[10] = {0.0};
    for (int32_t f = 0; f < g->faceCount; ++f)
    {
        double p[3][3];
        int32_t start = g->faceStart[f];
        PointOf(b, g->pointOf[g->loop[start]], p[0]);
        Sub(p[0], ref, p[0]);
        for (int32_t j = start + 1; j + 1 < g->faceStart[f + 1]; ++j)
        {
            PointOf(b, g->pointOf[g->loop[j]], p[1]);
            PointOf(b, g->pointOf[g->loop[j + 1]], p[2]);
            Sub(p[1], ref, p[1]);
            Sub(p[2], ref, p[2]);
            Integrate(p[0], p[1], p[2], integral);
        }
    }
    static const double scale[10] = {1.0 / 6.0,  1.0 / 24.0, 1.0 / 24.0,  1.0 / 24.0,  1.0 / 60.0,
                                     1.0 / 60.0, 1.0 / 60.0, 1.0 / 120.0, 1.0 / 120.0, 1.0 / 120.0};
    for (int32_t k = 0; k < 10; ++k)
    {
        integral[k] *= scale[k];
    }
    double volume = integral[0];
    if (!(volume > 0.0))
    {
        return false;
    }
    double c[3] = {integral[1] / volume, integral[2] / volume, integral[3] / volume};
    out->volume = (float)volume;
    for (int32_t k = 0; k < 3; ++k)
    {
        out->moments[k] = (float)(integral[4 + k] - volume * c[k] * c[k]);
        out->centroid[k] = (float)(ref[k] + c[k]);
    }
    // xy, xz, yz from the integrals of xy, yz and zx.
    out->moments[3] = (float)(integral[7] - volume * c[0] * c[1]);
    out->moments[4] = (float)(integral[9] - volume * c[0] * c[2]);
    out->moments[5] = (float)(integral[8] - volume * c[1] * c[2]);
    return true;
}

// Gathers the built triangles into the hull about its centroid.
static mvfxResult Finish(const Build* b, mvfxHull* out)
{
    Gather g;
    memset(&g, 0, sizeof g);
    bool taken[BUILD_FACES] = {false};
    for (int32_t f = 0; f < b->faceCount; ++f)
    {
        mvfxResult added = b->faces[f].alive && !taken[f] ? AddFace(b, &g, f, taken) : mvfx_success;
        if (added != mvfx_success)
        {
            return added;
        }
    }
    double ref[3] = {0.0, 0.0, 0.0};
    for (int32_t v = 0; v < g.vertexCount; ++v)
    {
        double p[3];
        PointOf(b, g.pointOf[v], p);
        for (int32_t k = 0; k < 3; ++k)
        {
            ref[k] += p[k] / (double)g.vertexCount;
        }
    }
    *out = (mvfxHull){.vertexCount = g.vertexCount, .faceCount = g.faceCount};
    if (!MassOf(b, &g, ref, out))
    {
        return mvfx_errorInvalid;
    }
    double c[3] = {(double)out->centroid[0], (double)out->centroid[1], (double)out->centroid[2]};
    for (int32_t v = 0; v < g.vertexCount; ++v)
    {
        double p[3];
        PointOf(b, g.pointOf[v], p);
        for (int32_t k = 0; k < 3; ++k)
        {
            out->vertices[v][k] = (float)(p[k] - c[k]);
        }
    }
    for (int32_t f = 0; f < g.faceCount; ++f)
    {
        double p[3];
        PointOf(b, g.pointOf[g.loop[g.faceStart[f]]], p);
        Sub(p, c, p);
        for (int32_t k = 0; k < 3; ++k)
        {
            out->planes[f][k] = (float)g.normals[f][k];
        }
        out->planes[f][3] = (float)Dot(g.normals[f], p);
    }
    memcpy(out->faceStart, g.faceStart, sizeof g.faceStart);
    memcpy(out->loop, g.loop, sizeof g.loop);
    return mvfx_success;
}

// Counts the points the live triangles have, each once: their owner no
// longer needed.
static int32_t CountVertices(Build* b)
{
    for (int32_t i = 0; i < b->count; ++i)
    {
        b->owner[i] = 0;
    }
    int32_t count = 0;
    for (int32_t f = 0; f < b->faceCount; ++f)
    {
        for (int32_t k = 0; b->faces[f].alive && k < 3; ++k)
        {
            count += b->owner[b->faces[f].v[k]] == 0 ? 1 : 0;
            b->owner[b->faces[f].v[k]] = 1;
        }
    }
    return count;
}

mvfxResult mvfxMeasureHull(const float* points, int32_t count, const mvfxAllocator* allocator,
                           mvfxHullMeasure* measure, mvfxHull* out)
{
    *measure = (mvfxHullMeasure){.counted = mvfx_success, .built = mvfx_errorInvalid};
    if (count < 4)
    {
        return mvfx_success;
    }
    Build* b = mvfxAllocate(allocator, sizeof(Build), _Alignof(Build));
    int32_t* owner = mvfxAllocate(allocator, (size_t)count * sizeof(int32_t), _Alignof(int32_t));
    mvfxResult result = mvfx_errorCapacity;
    if (b != nullptr && owner != nullptr)
    {
        memset(b, 0, sizeof *b);
        b->points = points;
        b->count = count;
        b->owner = owner;
        mvfxResult made = Triangles(b);
        // Past a build's faces is past the most vertices too: the
        // mvfx_errorLimit of MVFX_MAX_HULL_VERTICES.
        measure->built = made == mvfx_success ? Finish(b, out) : made;
        measure->counted = made == mvfx_errorLimit ? made : mvfx_success;
        measure->count = made == mvfx_success ? CountVertices(b) : 0;
        result = mvfx_success;
    }
    mvfxRelease(allocator, owner, (size_t)count * sizeof(int32_t), _Alignof(int32_t));
    mvfxRelease(allocator, b, sizeof(Build), _Alignof(Build));
    return result;
}

mvfxResult mvfxBuildHull(const float* points, int32_t count, const mvfxAllocator* allocator,
                         mvfxHull* out)
{
    mvfxHullMeasure measure;
    mvfxResult result = mvfxMeasureHull(points, count, allocator, &measure, out);
    return result == mvfx_success ? measure.built : result;
}
