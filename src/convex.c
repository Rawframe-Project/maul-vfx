// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "convex.h"

#include "direction.h"

#include <math.h>
#include <string.h>

// Each search's most steps: a fixed bound keeps every platform's steps
// the same (the same bits).
#define MOST_STEPS 64

// The most vertices and faces EPA's polytope grows to.
#define EPA_VERTICES (4 + MOST_STEPS)
#define EPA_FACES    (2 * EPA_VERTICES)

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

static float DotF(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// The box's corners (bit k of i set for +k) and its faces, each
// counterclockwise seen from outside: +X, -X, +Y, -Y, +Z, -Z.
static const uint16_t s_boxStart[7] = {0, 4, 8, 12, 16, 20, 24};
static const uint8_t s_boxLoop[24] = {1, 3, 7, 5, 0, 4, 6, 2, 2, 6, 7, 3,
                                      0, 1, 5, 4, 4, 5, 7, 6, 0, 2, 3, 1};

void mvfxBoxPolytope(const float center[3], const float rotation[4], const float half[3],
                     mvfxPolytope* out)
{
    out->vertexCount = 8;
    out->faceCount = 6;
    out->faceStart = s_boxStart;
    out->loop = s_boxLoop;
    for (int32_t i = 0; i < 8; ++i)
    {
        float corner[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            corner[k] = (i >> k & 1) != 0 ? half[k] : -half[k];
        }
        mvfxRotate(rotation, corner, out->vertices[i]);
        for (int32_t k = 0; k < 3; ++k)
        {
            out->vertices[i][k] += center[k];
        }
    }
    for (int32_t f = 0; f < 6; ++f)
    {
        float axis[3] = {0.0f, 0.0f, 0.0f};
        axis[f / 2] = f % 2 == 0 ? 1.0f : -1.0f;
        mvfxRotate(rotation, axis, out->planes[f]);
        out->planes[f][3] = DotF(out->planes[f], center) + half[f / 2];
    }
}

void mvfxHullPolytope(const mvfxHull* hull, const float scale[3], const float x[3],
                      const float q[4], mvfxPolytope* out)
{
    out->vertexCount = hull->vertexCount;
    out->faceCount = hull->faceCount;
    out->faceStart = hull->faceStart;
    out->loop = hull->loop;
    for (int32_t i = 0; i < hull->vertexCount; ++i)
    {
        float scaled[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            scaled[k] = hull->vertices[i][k] * scale[k];
        }
        mvfxRotate(q, scaled, out->vertices[i]);
        for (int32_t k = 0; k < 3; ++k)
        {
            out->vertices[i][k] += x[k];
        }
    }
    // A plane scales by the inverse of the scale, then turns.
    for (int32_t f = 0; f < hull->faceCount; ++f)
    {
        float n[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            n[k] = hull->planes[f][k] / fmaxf(scale[k], 1e-30f);
        }
        (void)mvfxNormalize(n);
        mvfxRotate(q, n, out->planes[f]);
        out->planes[f][3] = DotF(out->planes[f], out->vertices[hull->loop[hull->faceStart[f]]]);
    }
}

// A vertex of the polytopes' difference, a less b, with the indices of
// a's vertex and b's.
typedef struct Point
{
    double w[3];
    uint8_t a;
    uint8_t b;
} Point;

// The polytope's vertex furthest along d, the lowest on a tie.
static int32_t Furthest(const mvfxPolytope* p, const double d[3])
{
    int32_t best = 0;
    double most = -(double)INFINITY;
    for (int32_t i = 0; i < p->vertexCount; ++i)
    {
        const double v[3] = {(double)p->vertices[i][0], (double)p->vertices[i][1],
                             (double)p->vertices[i][2]};
        double along = Dot(v, d);
        if (along > most)
        {
            most = along;
            best = i;
        }
    }
    return best;
}

// The difference's vertex furthest along d.
static Point Support(const mvfxPolytope* a, const mvfxPolytope* b, const double d[3])
{
    const double back[3] = {-d[0], -d[1], -d[2]};
    int32_t i = Furthest(a, d);
    int32_t j = Furthest(b, back);
    Point p = {.a = (uint8_t)i, .b = (uint8_t)j};
    for (int32_t k = 0; k < 3; ++k)
    {
        p.w[k] = (double)a->vertices[i][k] - (double)b->vertices[j][k];
    }
    return p;
}

// A simplex of the difference and the weights of its point nearest the
// origin.
typedef struct Simplex
{
    Point p[4];
    double weight[4];
    int32_t count;
} Simplex;

// Keeps the simplex's points of indices keep (count n) with weights.
static void Keep(Simplex* s, const int32_t* keep, const double* weight, int32_t n)
{
    Point kept[4];
    for (int32_t i = 0; i < n; ++i)
    {
        kept[i] = s->p[keep[i]];
    }
    for (int32_t i = 0; i < n; ++i)
    {
        s->p[i] = kept[i];
        s->weight[i] = weight[i];
    }
    s->count = n;
}

// The nearest point to the origin of segment i j (Ericson 5.1.2), kept.
static void Segment(Simplex* s, int32_t i, int32_t j)
{
    double ab[3];
    Sub(s->p[j].w, s->p[i].w, ab);
    double length = Dot(ab, ab);
    double t = length > 0.0 ? -Dot(s->p[i].w, ab) / length : 0.0;
    const int32_t both[2] = {i, j};
    if (t <= 0.0)
    {
        Keep(s, &both[0], (const double[]){1.0}, 1);
    }
    else if (t >= 1.0)
    {
        Keep(s, &both[1], (const double[]){1.0}, 1);
    }
    else
    {
        Keep(s, both, (const double[]){1.0 - t, t}, 2);
    }
}

// The nearest point to the origin of triangle i j k by its regions
// (Ericson 5.1.5), kept.
static void Triangle(Simplex* s, int32_t i, int32_t j, int32_t k)
{
    const double* a = s->p[i].w;
    const double* b = s->p[j].w;
    const double* c = s->p[k].w;
    double ab[3];
    double ac[3];
    Sub(b, a, ab);
    Sub(c, a, ac);
    double d1 = -Dot(ab, a);
    double d2 = -Dot(ac, a);
    double d3 = -Dot(ab, b);
    double d4 = -Dot(ac, b);
    double d5 = -Dot(ab, c);
    double d6 = -Dot(ac, c);
    double vc = d1 * d4 - d3 * d2;
    double vb = d5 * d2 - d1 * d6;
    double va = d3 * d6 - d5 * d4;
    if (d1 <= 0.0 && d2 <= 0.0)
    {
        Keep(s, &i, (const double[]){1.0}, 1);
    }
    else if (d3 >= 0.0 && d4 <= d3)
    {
        Keep(s, &j, (const double[]){1.0}, 1);
    }
    else if (d6 >= 0.0 && d5 <= d6)
    {
        Keep(s, &k, (const double[]){1.0}, 1);
    }
    else if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
    {
        Segment(s, i, j);
    }
    else if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
    {
        Segment(s, i, k);
    }
    else if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0)
    {
        Segment(s, j, k);
    }
    else if (!(va + vb + vc > 0.0))
    {
        // No area: an edge stands for it.
        Segment(s, i, j);
    }
    else
    {
        double sum = va + vb + vc;
        const int32_t all[3] = {i, j, k};
        Keep(s, all, (const double[]){va / sum, vb / sum, vc / sum}, 3);
    }
}

// The weighted point of the simplex.
static void PointOf(const Simplex* s, double v[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        v[k] = 0.0;
        for (int32_t i = 0; i < s->count; ++i)
        {
            v[k] += s->weight[i] * s->p[i].w[k];
        }
    }
}

// The nearest point to the origin of the tetrahedron (Ericson 5.1.6):
// of each face the origin is beyond, the nearest's, kept; false when the
// origin is inside.
static bool Tetrahedron(Simplex* s)
{
    static const int32_t faces[4][4] = {{0, 1, 2, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {1, 3, 2, 0}};
    Simplex best = *s;
    double nearest = (double)INFINITY;
    for (int32_t f = 0; f < 4; ++f)
    {
        const double* a = s->p[faces[f][0]].w;
        double ab[3];
        double ac[3];
        double ad[3];
        double n[3];
        Sub(s->p[faces[f][1]].w, a, ab);
        Sub(s->p[faces[f][2]].w, a, ac);
        Sub(s->p[faces[f][3]].w, a, ad);
        Cross(ab, ac, n);
        double origin = -Dot(a, n);
        double other = Dot(ad, n);
        // Beyond the face: the origin and the fourth point on its two
        // sides (or a flat tetrahedron).
        if (origin * other <= 0.0)
        {
            Simplex face = *s;
            Triangle(&face, faces[f][0], faces[f][1], faces[f][2]);
            double v[3];
            PointOf(&face, v);
            double distance = Dot(v, v);
            if (distance < nearest)
            {
                nearest = distance;
                best = face;
            }
        }
    }
    if (nearest == (double)INFINITY)
    {
        return false;
    }
    *s = best;
    return true;
}

// The simplex's nearest point to the origin, the simplex cut to the
// points that weigh in it; false when it holds the origin.
static bool Nearest(Simplex* s, double v[3])
{
    bool apart = true;
    if (s->count == 2)
    {
        Segment(s, 0, 1);
    }
    else if (s->count == 3)
    {
        Triangle(s, 0, 1, 2);
    }
    else if (s->count == 4)
    {
        apart = Tetrahedron(s);
    }
    PointOf(s, v);
    return apart;
}

// GJK: the simplex nearest the origin of a's and b's difference and its
// nearest point v; false when they overlap (the simplex holds the origin).
static bool Gjk(const mvfxPolytope* a, const mvfxPolytope* b, Simplex* s, double v[3])
{
    const double start[3] = {(double)(a->vertices[0][0] - b->vertices[0][0]),
                             (double)(a->vertices[0][1] - b->vertices[0][1]),
                             (double)(a->vertices[0][2] - b->vertices[0][2])};
    s->p[0] = Support(a, b, start);
    s->weight[0] = 1.0;
    s->count = 1;
    memcpy(v, s->p[0].w, 3 * sizeof(double));
    double reach = Dot(v, v);
    for (int32_t step = 0; step < MOST_STEPS; ++step)
    {
        double vv = Dot(v, v);
        if (vv <= 1e-18 * reach)
        {
            return false;
        }
        const double back[3] = {-v[0], -v[1], -v[2]};
        Point p = Support(a, b, back);
        reach = fmax(reach, Dot(p.w, p.w));
        if (vv - Dot(v, p.w) <= 1e-12 * vv)
        {
            return true;
        }
        for (int32_t i = 0; i < s->count; ++i)
        {
            double d[3];
            Sub(p.w, s->p[i].w, d);
            if (Dot(d, d) <= 1e-24 * reach)
            {
                return true;
            }
        }
        s->p[s->count++] = p;
        if (!Nearest(s, v))
        {
            return false;
        }
    }
    return true;
}

// The witnesses of a simplex's weighted point: a's and b's.
static void Witnesses(const mvfxPolytope* a, const mvfxPolytope* b, const Simplex* s, float onA[3],
                      float onB[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        double pa = 0.0;
        double pb = 0.0;
        for (int32_t i = 0; i < s->count; ++i)
        {
            pa += s->weight[i] * (double)a->vertices[s->p[i].a][k];
            pb += s->weight[i] * (double)b->vertices[s->p[i].b][k];
        }
        onA[k] = (float)pa;
        onB[k] = (float)pb;
    }
}

// EPA's polytope: the difference's points and triangles (by the points'
// indices), each with its outward unit normal and distance from the
// origin.
typedef struct Epa
{
    Point points[EPA_VERTICES];
    int32_t pointCount;
    uint8_t faces[EPA_FACES][3];
    double normals[EPA_FACES][3];
    double distances[EPA_FACES];
    int32_t faceCount;
} Epa;

// Adds triangle i j k (counterclockwise from outside); false for no area
// or no room.
static bool AddTriangle(Epa* e, int32_t i, int32_t j, int32_t k)
{
    if (e->faceCount >= EPA_FACES)
    {
        return false;
    }
    double ab[3];
    double ac[3];
    double n[3];
    Sub(e->points[j].w, e->points[i].w, ab);
    Sub(e->points[k].w, e->points[i].w, ac);
    Cross(ab, ac, n);
    double length = sqrt(Dot(n, n));
    if (!(length > 0.0))
    {
        return false;
    }
    int32_t f = e->faceCount++;
    e->faces[f][0] = (uint8_t)i;
    e->faces[f][1] = (uint8_t)j;
    e->faces[f][2] = (uint8_t)k;
    for (int32_t c = 0; c < 3; ++c)
    {
        e->normals[f][c] = n[c] / length;
    }
    e->distances[f] = Dot(e->normals[f], e->points[i].w);
    return true;
}

// Grows a simplex holding the origin to a tetrahedron by points of the
// difference along the axes and across what it has; false when the
// difference is flat.
static bool Blow(const mvfxPolytope* a, const mvfxPolytope* b, Simplex* s)
{
    static const double axes[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                      {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    for (int32_t round = 0; round < 3 && s->count < 4; ++round)
    {
        double ways[6][3];
        int32_t wayCount = 6;
        memcpy(ways, axes, sizeof ways);
        if (s->count == 3)
        {
            double ab[3];
            double ac[3];
            Sub(s->p[1].w, s->p[0].w, ab);
            Sub(s->p[2].w, s->p[0].w, ac);
            Cross(ab, ac, ways[0]);
            for (int32_t k = 0; k < 3; ++k)
            {
                ways[1][k] = -ways[0][k];
            }
            wayCount = 2;
        }
        bool grown = false;
        for (int32_t w = 0; w < wayCount && !grown; ++w)
        {
            Point p = Support(a, b, ways[w]);
            // New when off the simplex's span.
            double off = 0.0;
            if (s->count == 1 || s->count == 3)
            {
                double d[3];
                Sub(p.w, s->p[0].w, d);
                off = s->count == 1 ? Dot(d, d) : fabs(Dot(d, ways[0]));
            }
            else
            {
                double ab[3];
                double d[3];
                double across[3];
                Sub(s->p[1].w, s->p[0].w, ab);
                Sub(p.w, s->p[0].w, d);
                Cross(ab, d, across);
                off = Dot(across, across);
            }
            if (off > 1e-12)
            {
                s->p[s->count++] = p;
                grown = true;
            }
        }
        if (!grown)
        {
            return false;
        }
    }
    return s->count == 4;
}

// Starts EPA's polytope from a tetrahedron, its faces turned outward.
static bool StartEpa(Epa* e, const Simplex* s)
{
    *e = (Epa){.pointCount = 4};
    for (int32_t i = 0; i < 4; ++i)
    {
        e->points[i] = s->p[i];
    }
    double ab[3];
    double ac[3];
    double ad[3];
    double n[3];
    Sub(s->p[1].w, s->p[0].w, ab);
    Sub(s->p[2].w, s->p[0].w, ac);
    Sub(s->p[3].w, s->p[0].w, ad);
    Cross(ab, ac, n);
    bool flip = Dot(n, ad) > 0.0;
    int32_t b = flip ? 2 : 1;
    int32_t c = flip ? 1 : 2;
    return AddTriangle(e, 0, b, c) && AddTriangle(e, 0, 3, b) && AddTriangle(e, b, 3, c) &&
           AddTriangle(e, c, 3, 0);
}

// The face nearest the origin.
static int32_t NearestFace(const Epa* e)
{
    int32_t best = 0;
    for (int32_t f = 1; f < e->faceCount; ++f)
    {
        best = e->distances[f] < e->distances[best] ? f : best;
    }
    return best;
}

// Adds point p: the faces it sees removed and the horizon joined to it;
// false when there is no room.
static bool Expand(Epa* e, const Point* p)
{
    if (e->pointCount >= EPA_VERTICES)
    {
        return false;
    }
    int32_t at = e->pointCount++;
    e->points[at] = *p;
    uint8_t edges[3 * EPA_FACES][2];
    int32_t edgeCount = 0;
    int32_t kept = 0;
    for (int32_t f = 0; f < e->faceCount; ++f)
    {
        if (Dot(e->normals[f], p->w) - e->distances[f] <= 1e-12 * (fabs(e->distances[f]) + 1e-30))
        {
            memcpy(e->faces[kept], e->faces[f], sizeof e->faces[0]);
            memcpy(e->normals[kept], e->normals[f], sizeof e->normals[0]);
            e->distances[kept++] = e->distances[f];
            continue;
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            uint8_t from = e->faces[f][k];
            uint8_t to = e->faces[f][(k + 1) % 3];
            // An edge seen from both sides is inside the hole.
            bool twin = false;
            for (int32_t j = 0; j < edgeCount && !twin; ++j)
            {
                if (edges[j][0] == to && edges[j][1] == from)
                {
                    twin = true;
                    edges[j][0] = edges[--edgeCount][0];
                    edges[j][1] = edges[edgeCount][1];
                }
            }
            if (!twin)
            {
                edges[edgeCount][0] = from;
                edges[edgeCount++][1] = to;
            }
        }
    }
    e->faceCount = kept;
    for (int32_t j = 0; j < edgeCount; ++j)
    {
        if (!AddTriangle(e, edges[j][0], edges[j][1], at))
        {
            return false;
        }
    }
    return true;
}

// EPA (van den Bergen 2003): of the difference holding the origin, the
// face nearest it, grown until its distance holds; how far a must move
// against its normal to part them, the normal and the witnesses.
static void Penetration(const mvfxPolytope* a, const mvfxPolytope* b, Simplex* s,
                        mvfxConvexMeet* out)
{
    Epa e;
    *out = (mvfxConvexMeet){.normal = {0.0f, 1.0f, 0.0f}};
    Witnesses(a, b, s, out->onA, out->onB);
    if (!Blow(a, b, s) || !StartEpa(&e, s))
    {
        return;
    }
    int32_t f = NearestFace(&e);
    for (int32_t step = 0; step < MOST_STEPS; ++step)
    {
        Point p = Support(a, b, e.normals[f]);
        double gain = Dot(e.normals[f], p.w) - e.distances[f];
        if (gain <= 1e-6 * fmax(e.distances[f], 1e-12) || !Expand(&e, &p) || e.faceCount == 0)
        {
            break;
        }
        f = NearestFace(&e);
    }
    // The face's point nearest the origin, by its triangle's weights.
    Simplex face = {.count = 3};
    for (int32_t i = 0; i < 3; ++i)
    {
        face.p[i] = e.points[e.faces[f][i]];
    }
    double v[3];
    (void)Nearest(&face, v);
    Witnesses(a, b, &face, out->onA, out->onB);
    for (int32_t k = 0; k < 3; ++k)
    {
        out->normal[k] = (float)-e.normals[f][k];
    }
    out->distance = (float)-e.distances[f];
}

bool mvfxConvexMeets(const mvfxPolytope* a, const mvfxPolytope* b, float margin,
                     mvfxConvexMeet* out)
{
    Simplex s;
    double v[3];
    if (!Gjk(a, b, &s, v))
    {
        Penetration(a, b, &s, out);
        return true;
    }
    double distance = sqrt(Dot(v, v));
    if (!(distance < (double)margin))
    {
        return false;
    }
    Witnesses(a, b, &s, out->onA, out->onB);
    for (int32_t k = 0; k < 3; ++k)
    {
        out->normal[k] = distance > 0.0 ? (float)(v[k] / distance) : (k == 1 ? 1.0f : 0.0f);
    }
    out->distance = (float)distance;
    return true;
}

float mvfxPolytopeNearest(const mvfxPolytope* a, const float p[3], float out[3])
{
    mvfxPolytope point = {.vertexCount = 1};
    memcpy(point.vertices[0], p, 3 * sizeof(float));
    Simplex s;
    double v[3];
    if (!Gjk(a, &point, &s, v))
    {
        memcpy(out, p, 3 * sizeof(float));
        return 0.0f;
    }
    float onPoint[3];
    Witnesses(a, &point, &s, out, onPoint);
    return (float)sqrt(Dot(v, v));
}

// The face of p whose normal is furthest along d, and how far.
static int32_t FaceAlong(const mvfxPolytope* p, const float d[3], float* along)
{
    int32_t best = 0;
    *along = -INFINITY;
    for (int32_t f = 0; f < p->faceCount; ++f)
    {
        float a = DotF(p->planes[f], d);
        if (a > *along)
        {
            *along = a;
            best = f;
        }
    }
    return best;
}

// Clips a polygon of count points to dot(x, l) <= limit, keeping points on
// the plane and adding no crossing beside them (as boxes' faces).
static int32_t Clip(float polygon[][3], int32_t count, const float l[3], float limit, float on)
{
    float out[MVFX_MAX_MANIFOLD_POINTS][3];
    int32_t kept = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        const float* a = polygon[i];
        const float* b = polygon[(i + 1) % count];
        float da = DotF(a, l) - limit;
        float db = DotF(b, l) - limit;
        if (da <= on && kept < MVFX_MAX_MANIFOLD_POINTS)
        {
            memcpy(out[kept++], a, 3 * sizeof(float));
        }
        if (((da < -on && db > on) || (da > on && db < -on)) && kept < MVFX_MAX_MANIFOLD_POINTS)
        {
            float f = da / (da - db);
            for (int32_t k = 0; k < 3; ++k)
            {
                out[kept][k] = a[k] + f * (b[k] - a[k]);
            }
            ++kept;
        }
    }
    memcpy(polygon, out, (size_t)kept * sizeof out[0]);
    return kept;
}

// Clips the incident face of other by the side planes of ref's face
// through its edges; the points left, counted.
static int32_t ClipFace(const mvfxPolytope* ref, int32_t face, const mvfxPolytope* other,
                        int32_t incident, float polygon[MVFX_MAX_MANIFOLD_POINTS][3])
{
    int32_t count = 0;
    for (int32_t j = other->faceStart[incident]; j < other->faceStart[incident + 1]; ++j)
    {
        memcpy(polygon[count++], other->vertices[other->loop[j]], 3 * sizeof(float));
    }
    const float* n = ref->planes[face];
    int32_t first = ref->faceStart[face];
    int32_t corners = ref->faceStart[face + 1] - first;
    for (int32_t j = 0; j < corners && count > 0; ++j)
    {
        const float* p = ref->vertices[ref->loop[first + j]];
        const float* q = ref->vertices[ref->loop[first + (j + 1) % corners]];
        const float edge[3] = {q[0] - p[0], q[1] - p[1], q[2] - p[2]};
        float side[3] = {edge[1] * n[2] - edge[2] * n[1], edge[2] * n[0] - edge[0] * n[2],
                         edge[0] * n[1] - edge[1] * n[0]};
        float length = sqrtf(DotF(side, side));
        if (!(length > 0.0f))
        {
            continue;
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            side[k] /= length;
        }
        count = Clip(polygon, count, side, DotF(side, p), 1e-4f * sqrtf(DotF(edge, edge)));
    }
    return count;
}

// The contact points of the face of one of a and b most along meet's
// normal clipped by the other's face most against it, nearer than margin;
// their normal set. None where no face is within 45 degrees of the normal
// (edges crossing).
static int32_t FaceContacts(const mvfxPolytope* a, const mvfxPolytope* b,
                            const mvfxConvexMeet* meet, float margin, float normal[3],
                            mvfxConvexPoint points[MVFX_MAX_MANIFOLD_POINTS])
{
    const float back[3] = {-meet->normal[0], -meet->normal[1], -meet->normal[2]};
    float alongB = 0.0f;
    float alongA = 0.0f;
    int32_t faceB = FaceAlong(b, meet->normal, &alongB);
    int32_t faceA = FaceAlong(a, back, &alongA);
    if (!(fmaxf(alongA, alongB) >= 0.7f))
    {
        return 0;
    }
    // A face of b by a clear margin, as boxes' faces.
    bool onB = alongB >= 0.98f * alongA;
    const mvfxPolytope* ref = onB ? b : a;
    const mvfxPolytope* other = onB ? a : b;
    int32_t face = onB ? faceB : faceA;
    const float* n = ref->planes[face];
    const float against[3] = {-n[0], -n[1], -n[2]};
    float along = 0.0f;
    int32_t incident = FaceAlong(other, against, &along);
    float polygon[MVFX_MAX_MANIFOLD_POINTS][3];
    int32_t clipped = ClipFace(ref, face, other, incident, polygon);
    int32_t count = 0;
    for (int32_t k = 0; k < 3; ++k)
    {
        normal[k] = onB ? n[k] : -n[k];
    }
    for (int32_t i = 0; i < clipped; ++i)
    {
        float s = DotF(n, polygon[i]) - ref->planes[face][3];
        if (!(s < margin))
        {
            continue;
        }
        mvfxConvexPoint* c = &points[count++];
        c->depth = s;
        for (int32_t k = 0; k < 3; ++k)
        {
            float under = polygon[i][k] - s * n[k];
            c->onA[k] = onB ? polygon[i][k] : under;
            c->onB[k] = onB ? under : polygon[i][k];
        }
    }
    return count;
}

int32_t mvfxConvexManifold(const mvfxPolytope* a, const mvfxPolytope* b, const mvfxConvexMeet* meet,
                           float margin, float normal[3],
                           mvfxConvexPoint points[MVFX_MAX_MANIFOLD_POINTS])
{
    int32_t count =
        a->faceCount > 0 && b->faceCount > 0 ? FaceContacts(a, b, meet, margin, normal, points) : 0;
    if (count == 0)
    {
        memcpy(normal, meet->normal, 3 * sizeof(float));
        points[0] = (mvfxConvexPoint){.depth = meet->distance};
        memcpy(points[0].onA, meet->onA, sizeof points[0].onA);
        memcpy(points[0].onB, meet->onB, sizeof points[0].onB);
        count = 1;
    }
    return count;
}
