// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "appearance.h"
#include "context.h"
#include "direction.h"
#include "gradient.h"
#include "instance_api.h"
#include "layout_api.h"
#include "output.h"
#include "sort.h"
#include "spawn.h"
#include "tube.h"

#include <math.h>

// A chain being written: the source of its records, the particles of its
// ribbon in order, the view, its length in the world and its points.
typedef struct PlaceCache PlaceCache;

typedef struct Chain
{
    const mvfxRecordSource* source;
    const mvfxCompiledEmitter* compiled;
    const mvfxView* view;
    const uint32_t* particles;
    int32_t particleCount;
    // Its points through particles: the particles, and a pinned head
    // after them.
    int32_t count;
    bool head;
    double headPlace[3];
    double length;
    int64_t points;
    // A plexus link: straight, facing the eye, along from 0 to 1, of the
    // plexus width, its alpha times fade.
    bool link;
    float fade;
    // The world places last read, or NULL to read each anew.
    PlaceCache* cache;
} Chain;

// The world places of a chain's points last read, by the point's index
// modulo 8: a walk reads each point's place up to five times in a row (a
// spline reads four, a segment's start one), and the same each time.
struct PlaceCache
{
    int32_t points[8];
    double places[8][3];
};

// The particle point j takes its values from: its own, or for the head the
// youngest's.
static int32_t ParticleAt(const Chain* chain, int32_t j)
{
    return (int32_t)chain->particles[j < chain->particleCount ? j : chain->particleCount - 1];
}

// Point j's place in the world, j within the chain.
static void PointPlace(const Chain* chain, int32_t j, double out[3])
{
    if (j == chain->particleCount)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out[k] = chain->headPlace[k];
        }
        return;
    }
    PlaceCache* cache = chain->cache;
    if (cache == nullptr)
    {
        mvfxWorldPlaceOf(chain->source, ParticleAt(chain, j), out);
        return;
    }
    int32_t slot = j & 7;
    if (cache->points[slot] != j)
    {
        mvfxWorldPlaceOf(chain->source, ParticleAt(chain, j), cache->places[slot]);
        cache->points[slot] = j;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = cache->places[slot][k];
    }
}

// A place along a chain: segment s from particle s to s + 1, piece q of
// its pieces; the last particle as segment count - 1, piece 0.
typedef struct Walk
{
    int32_t s;
    int32_t q;
    int32_t pieces;
} Walk;

static double Distance(const double a[3], const double b[3])
{
    double square = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double d = b[k] - a[k];
        square += d * d;
    }
    return sqrt(square);
}

// Particle j's place in the world; past either end, the end's mirrored
// through it from its neighbour.
static void PlaceOf(const Chain* chain, int32_t j, double out[3])
{
    if (j >= 0 && j < chain->count)
    {
        PointPlace(chain, j, out);
        return;
    }
    int32_t end = j < 0 ? 0 : chain->count - 1;
    int32_t next = j < 0 ? 1 : chain->count - 2;
    double a[3];
    double b[3];
    PointPlace(chain, end, a);
    PointPlace(chain, next, b);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = 2.0 * a[k] - b[k];
    }
}

// The centripetal Catmull-Rom spline's place a fraction f along segment s
// (Barry and Goldman's pyramid with knots at square roots of distance);
// the straight line where two of its points meet.
static void SplinePlace(const Chain* chain, int32_t s, double f, double out[3])
{
    double p[4][3];
    for (int32_t i = 0; i < 4; ++i)
    {
        PlaceOf(chain, s - 1 + i, p[i]);
    }
    double t[4] = {0.0, 0.0, 0.0, 0.0};
    for (int32_t i = 1; i < 4; ++i)
    {
        t[i] = t[i - 1] + sqrt(Distance(p[i - 1], p[i]));
    }
    if (t[1] == t[0] || t[2] == t[1] || t[3] == t[2])
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out[k] = p[1][k] + (p[2][k] - p[1][k]) * f;
        }
        return;
    }
    double u = t[1] + (t[2] - t[1]) * f;
    for (int32_t k = 0; k < 3; ++k)
    {
        double a1 = ((t[1] - u) * p[0][k] + (u - t[0]) * p[1][k]) / (t[1] - t[0]);
        double a2 = ((t[2] - u) * p[1][k] + (u - t[1]) * p[2][k]) / (t[2] - t[1]);
        double a3 = ((t[3] - u) * p[2][k] + (u - t[2]) * p[3][k]) / (t[3] - t[2]);
        double b1 = ((t[2] - u) * a1 + (u - t[0]) * a2) / (t[2] - t[0]);
        double b2 = ((t[3] - u) * a2 + (u - t[1]) * a3) / (t[3] - t[1]);
        out[k] = ((t[2] - u) * b1 + (u - t[1]) * b2) / (t[2] - t[1]);
    }
}

// The pieces segment s is cut into: all of them, or with a bend angle the
// fewest whose turn each allows the turn from the way into s to the way
// out of s + 1.
static int32_t PiecesOf(const Chain* chain, int32_t s)
{
    const mvfxCompiledEmitter* compiled = chain->compiled;
    int32_t most = chain->link ? 1 : compiled->ribbonPieces;
    if (most == 1 || compiled->ribbonCosLimits[0] < -1.5f)
    {
        return most;
    }
    double p[4][3];
    for (int32_t i = 0; i < 4; ++i)
    {
        PlaceOf(chain, s - 1 + i, p[i]);
    }
    double in[3];
    double out[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        in[k] = p[1][k] - p[0][k];
        out[k] = p[3][k] - p[2][k];
    }
    double lengths = Distance(p[0], p[1]) * Distance(p[2], p[3]);
    if (lengths == 0.0)
    {
        return 1;
    }
    float cosine = (float)((in[0] * out[0] + in[1] * out[1] + in[2] * out[2]) / lengths);
    for (int32_t k = 1; k < most; ++k)
    {
        if (cosine >= compiled->ribbonCosLimits[k - 1])
        {
            return k;
        }
    }
    return most;
}

static Walk FirstWalk(const Chain* chain)
{
    return (Walk){.s = 0, .q = 0, .pieces = PiecesOf(chain, 0)};
}

// The next place along; past the last particle, s is count.
static void Advance(const Chain* chain, Walk* walk)
{
    if (walk->s == chain->count - 1)
    {
        walk->s = chain->count;
        return;
    }
    walk->q += 1;
    if (walk->q == walk->pieces)
    {
        walk->s += 1;
        walk->q = 0;
        walk->pieces = walk->s < chain->count - 1 ? PiecesOf(chain, walk->s) : 1;
    }
}

static double FractionOf(const Walk* walk)
{
    return (double)walk->q / (double)walk->pieces;
}

static void WalkPlace(const Chain* chain, const Walk* walk, double out[3])
{
    if (walk->q == 0)
    {
        PlaceOf(chain, walk->s, out);
        return;
    }
    SplinePlace(chain, walk->s, FractionOf(walk), out);
}

// Counts a chain's points and its length over them.
static void MeasureChain(Chain* chain)
{
    chain->points = 0;
    chain->length = 0.0;
    double before[3] = {0.0, 0.0, 0.0};
    double here[3];
    for (Walk walk = FirstWalk(chain); walk.s < chain->count; Advance(chain, &walk))
    {
        WalkPlace(chain, &walk, here);
        chain->length += chain->points > 0 ? Distance(before, here) : 0.0;
        chain->points += 1;
        for (int32_t k = 0; k < 3; ++k)
        {
            before[k] = here[k];
        }
    }
}

static void Normalize(const double v[3], float out[3])
{
    double length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = length > 0.0 ? (float)(v[k] / length) : 0.0f;
    }
}

static void Cross(const double a[3], const double b[3], double out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// A point's tangent (from the place before to the one after), side and
// face normal.
static void FrameAt(const Chain* chain, const double before[3], const double here[3],
                    const double after[3], mvfxRibbonPoint* point)
{
    double way[3];
    double across[3];
    double tangent[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        way[k] = after[k] - before[k];
        across[k] = chain->view->eye[k] - here[k];
    }
    Normalize(way, point->tangent);
    if (!chain->link && chain->compiled->ribbonFacing == mvfx_ribbonFaceNormal)
    {
        float normal[3];
        mvfxUnmapVector(&chain->source->map, chain->compiled->ribbonNormal, normal);
        for (int32_t k = 0; k < 3; ++k)
        {
            across[k] = (double)normal[k];
        }
    }
    double side[3];
    double face[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        tangent[k] = (double)point->tangent[k];
    }
    Cross(tangent, across, side);
    Normalize(side, point->side);
    for (int32_t k = 0; k < 3; ++k)
    {
        side[k] = (double)point->side[k];
    }
    Cross(side, tangent, face);
    Normalize(face, point->faceNormal);
}

// Point j's distance stored at birth; the head's, the youngest's and the
// gap to it in the particles' space.
static float DistanceAt(const Chain* chain, const float* distances, int32_t j)
{
    float distance = distances[ParticleAt(chain, j)];
    if (j < chain->particleCount)
    {
        return distance;
    }
    double youngest[3];
    PointPlace(chain, j - 1, youngest);
    return distance + (float)Distance(youngest, chain->headPlace) * chain->source->map.sigma;
}

// The texture coordinate along at a place a fraction t of the length.
static float AlongAt(const Chain* chain, const Walk* walk, float t)
{
    const mvfxCompiledEmitter* compiled = chain->compiled;
    const mvfxInstance* instance = chain->source->instance;
    float along = t;
    if (chain->link)
    {
        return along;
    }
    if (compiled->ribbonTexture == mvfx_ribbonTilePerSegment)
    {
        along = (float)walk->s + (float)FractionOf(walk);
    }
    else if (compiled->ribbonTexture == mvfx_ribbonTileByDistance)
    {
        const float* distances = mvfxAttributeArray((mvfxInstance*)instance, chain->source->emitter,
                                                    mvfx_attributeRibbonDistance);
        float first = DistanceAt(chain, distances, walk->s);
        float second = walk->q == 0 ? first : DistanceAt(chain, distances, walk->s + 1);
        along = (first + (second - first) * (float)FractionOf(walk)) / compiled->ribbonTileLength;
    }
    double seconds = (double)instance->clock / (double)MVFX_TICKS_PER_SECOND;
    return along + (float)((double)compiled->ribbonScroll * seconds);
}

// Where chains are written: points in a layout with a ribbon list, or
// strips of two vertices a point with the indices of their triangles.
typedef struct Sink
{
    const mvfxOutputLayout* layout;
    bool strips;
    // Tubes of so many sides (0 for points or strips), with caps or not.
    int32_t sides;
    bool caps;
    unsigned char* records;
    int64_t recordCapacity;
    mvfxRibbon* ribbons;
    int64_t ribbonCapacity;
    uint32_t* indices;
    int64_t indexCapacity;
    uint32_t* scratch;
} Sink;

// What the chains of an emitter wrote and count: records (points or
// vertices), ribbons and indices.
typedef struct Totals
{
    int64_t records;
    int64_t ribbons;
    int64_t indices;
    int64_t recordsWritten;
    int64_t ribbonsWritten;
    int64_t indicesWritten;
    bool full;
} Totals;

// A tube's records and indices for a chain of points.
static int64_t TubeRecords(const Sink* sink, int64_t points)
{
    return points * (sink->sides + 1) + (sink->caps ? 2 * (sink->sides + 2) : 0);
}

static int64_t TubeIndices(const Sink* sink, int64_t points)
{
    return (points - 1) * sink->sides * 6 + (sink->caps ? 2 * sink->sides * 3 : 0);
}

// A tube's point g: its ring and, but for the last, the quads to the
// next; at either end its cap after all the rings.
static void WriteTubePoint(const Chain* chain, const Sink* sink, const mvfxTubeFrame* frame,
                           const mvfxTubePoint* point, int64_t g, const Totals* at)
{
    const mvfxTubeSink tube = {
        .source = chain->source, .layout = sink->layout, .sides = sink->sides};
    int64_t stride = (int64_t)sink->layout->stride;
    int64_t ring = at->recordsWritten + g * (sink->sides + 1);
    mvfxWriteTubeRing(&tube, frame, point, sink->records + ring * stride);
    if (g + 1 < chain->points)
    {
        mvfxTubeSideIndices(sink->sides, (uint32_t)ring, (uint32_t)(ring + sink->sides + 1),
                            sink->indices + at->indicesWritten + g * sink->sides * 6);
    }
    bool last = g + 1 == chain->points;
    if (sink->caps && (g == 0 || last))
    {
        int64_t cap =
            at->recordsWritten + chain->points * (sink->sides + 1) + (last ? sink->sides + 2 : 0);
        mvfxWriteTubeCap(&tube, frame, point, last, sink->records + cap * stride);
        mvfxTubeCapIndices(sink->sides, (uint32_t)cap, last,
                           sink->indices + at->indicesWritten +
                               (chain->points - 1) * sink->sides * 6 +
                               (last ? sink->sides * 3 : 0));
    }
}

// A point's record, or in a strip its two vertices across its width (0
// on the side's back, 1 on it) and from all but the last the indices of
// the two triangles to the next; or a tube's ring.
static void WritePoint(const Chain* chain, const Sink* sink, const Walk* walk, const double here[3],
                       mvfxRibbonPoint* point, int64_t g, const Totals* at, mvfxTubeFrame* frame)
{
    const mvfxRecordSource* source = chain->source;
    int32_t first = ParticleAt(chain, walk->s);
    int32_t second = walk->q == 0 ? first : ParticleAt(chain, walk->s + 1);
    float fraction = (float)FractionOf(walk);
    int64_t stride = (int64_t)sink->layout->stride;
    if (!sink->strips)
    {
        mvfxWriteRibbonRecord(source, sink->layout, first, second, fraction, here, point,
                              sink->records + (at->recordsWritten + g) * stride);
        return;
    }
    float a[3];
    float b[3];
    mvfxSizeOf(source->instance, source->emitter, first, a);
    mvfxSizeOf(source->instance, source->emitter, second, b);
    double half =
        0.5 * (double)((a[0] + (b[0] - a[0]) * fraction) / source->map.sigma * point->width);
    if (sink->sides > 0)
    {
        mvfxAdvanceTubeFrame(frame, here, point->tangent);
        const mvfxTubePoint tube = {.first = first,
                                    .second = second,
                                    .fraction = fraction,
                                    .here = here,
                                    .half = half,
                                    .values = point};
        WriteTubePoint(chain, sink, frame, &tube, g, at);
        return;
    }
    int64_t base = at->recordsWritten + 2 * g;
    for (int32_t v = 0; v < 2; ++v)
    {
        double place[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            place[k] = here[k] + (double)point->side[k] * (v == 0 ? -half : half);
        }
        point->corner[0] = point->along;
        point->corner[1] = (float)v;
        mvfxWriteRibbonRecord(source, sink->layout, first, second, fraction, place, point,
                              sink->records + (base + v) * stride);
    }
    if (g + 1 < chain->points)
    {
        static const uint32_t triangles[6] = {0, 1, 2, 2, 1, 3};
        for (int32_t k = 0; k < 6; ++k)
        {
            sink->indices[at->indicesWritten + 6 * g + k] = (uint32_t)base + triangles[k];
        }
    }
}

// Writes a chain's points after those written.
static void WriteChain(const Chain* chain, const Sink* sink, const Totals* at)
{
    const mvfxCompiledEmitter* compiled = chain->compiled;
    Walk walk = FirstWalk(chain);
    Walk ahead = walk;
    Advance(chain, &ahead);
    double before[3];
    double here[3];
    double after[3];
    WalkPlace(chain, &walk, here);
    WalkPlace(chain, &ahead, after);
    for (int32_t k = 0; k < 3; ++k)
    {
        before[k] = here[k];
    }
    double walked = 0.0;
    mvfxTubeFrame frame = {.started = false};
    for (int64_t g = 0; g < chain->points; ++g)
    {
        walked += Distance(before, here);
        // Along the length, or by points where the chain has none.
        float t = chain->length > 0.0 ? (float)(walked / chain->length)
                                      : (float)((double)g / (double)(chain->points - 1));
        mvfxRibbonPoint point = {.length = t,
                                 .along = AlongAt(chain, &walk, t),
                                 .width = chain->link ? compiled->plexus.width
                                                      : mvfxLifeAt(&compiled->ribbonWidth, t),
                                 .color = {1.0f, 1.0f, 1.0f, chain->link ? chain->fade : 1.0f},
                                 .ribbon = (uint32_t)at->ribbonsWritten};
        FrameAt(chain, before, here, after, &point);
        if (!chain->link && compiled->ribbonColor != nullptr)
        {
            mvfxGradientTableAt(compiled->ribbonColor, t, point.color);
        }
        WritePoint(chain, sink, &walk, here, &point, g, at, &frame);
        walk = ahead;
        Advance(chain, &ahead);
        for (int32_t k = 0; k < 3; ++k)
        {
            before[k] = here[k];
            here[k] = after[k];
        }
        if (ahead.s < chain->count)
        {
            WalkPlace(chain, &ahead, after);
        }
    }
}

// Counts a chain and writes it where it fits whole after those written.
static void TakeChain(const Chain* chain, uint32_t id, const Sink* sink, Totals* totals)
{
    int64_t records = sink->sides > 0 ? TubeRecords(sink, chain->points)
                      : sink->strips  ? 2 * chain->points
                                      : chain->points;
    int64_t indices = sink->sides > 0 ? TubeIndices(sink, chain->points)
                      : sink->strips  ? 6 * (chain->points - 1)
                                      : 0;
    bool fits = !totals->full && totals->recordsWritten + records <= sink->recordCapacity &&
                totals->indicesWritten + indices <= sink->indexCapacity &&
                (sink->strips || totals->ribbonsWritten < sink->ribbonCapacity);
    if (fits)
    {
        WriteChain(chain, sink, totals);
        if (!sink->strips)
        {
            sink->ribbons[totals->ribbonsWritten] = (mvfxRibbon){
                .first = totals->recordsWritten, .count = (int32_t)chain->points, .id = id};
        }
        totals->recordsWritten += records;
        totals->indicesWritten += indices;
        totals->ribbonsWritten += 1;
    }
    totals->full = !fits;
    totals->records += records;
    totals->indices += indices;
    totals->ribbons += 1;
}

// Chains an emitter's live particles by ribbon id and takes each chain of
// two or more points.
static Totals WriteChains(const mvfxInstance* instance, int32_t emitter, const mvfxView* view,
                          const Sink* sink)
{
    int32_t live = instance->emitters[emitter].count;
    uint32_t* keys = sink->scratch;
    uint32_t* indices = keys + live;
    const uint32_t* ids =
        mvfxAttributeArray((mvfxInstance*)instance, emitter, mvfx_attributeRibbon);
    for (int32_t i = 0; i < live; ++i)
    {
        keys[i] = ids[i];
        indices[i] = (uint32_t)i;
    }
    mvfxRadixSort(keys, indices, indices + live, indices + 2 * (int64_t)live, live);
    mvfxRecordSource source = mvfxOpenRecordSource(instance, emitter, view->origin);
    Totals totals = {0};
    for (int32_t start = 0; start < live;)
    {
        int32_t end = start + 1;
        while (end < live && keys[end] == keys[start])
        {
            ++end;
        }
        PlaceCache cache = {.points = {-1, -1, -1, -1, -1, -1, -1, -1}};
        Chain chain = {.source = &source,
                       .compiled = &instance->effect->emitters[emitter],
                       .view = view,
                       .particles = indices + start,
                       .particleCount = end - start,
                       .count = end - start,
                       .cache = &cache};
        // The current ribbon by the counter takes the pinned head.
        chain.head = keys[start] == instance->emitters[emitter].ribbonCounter &&
                     mvfxRibbonHeadPlace(instance, emitter, chain.headPlace);
        chain.count += chain.head ? 1 : 0;
        if (chain.count >= 2)
        {
            MeasureChain(&chain);
            TakeChain(&chain, keys[start], sink, &totals);
        }
        start = end;
    }
    return totals;
}

// Takes each of an emitter's plexus links as a chain of its two
// particles.
static Totals WriteLinks(const mvfxInstance* instance, int32_t emitter, const mvfxView* view,
                         const Sink* sink)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxPlexusRoom* room = &instance->emitters[emitter].plexus;
    mvfxRecordSource source = mvfxOpenRecordSource(instance, emitter, view->origin);
    const float* p[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = mvfxAttributeArray((mvfxInstance*)instance, emitter,
                                  (mvfxAttribute)(mvfx_attributePositionX + k));
    }
    Totals totals = {0};
    for (int32_t l = 0; l < room->linkCount; ++l)
    {
        const uint32_t particles[2] = {(uint32_t)room->links[2 * l],
                                       (uint32_t)room->links[2 * l + 1]};
        float square = 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            float d = p[k][particles[1]] - p[k][particles[0]];
            square += d * d;
        }
        // At least 0: a link is shorter than the distance, and the square
        // root is correctly rounded.
        float fade = 1.0f - sqrtf(square) / compiled->plexus.distance;
        Chain chain = {.source = &source,
                       .compiled = compiled,
                       .view = view,
                       .particles = particles,
                       .particleCount = 2,
                       .count = 2,
                       .link = true,
                       .fade = compiled->plexus.fade ? fade : 1.0f};
        MeasureChain(&chain);
        TakeChain(&chain, (uint32_t)l, sink, &totals);
    }
    return totals;
}

// The checks both ribbon outputs share: the instance, emitter and layout
// found, the view in range, scratch for the chains. Success with
// *liveOut NULL for an emitter not drawn as ribbons.
static mvfxResult OpenRibbons(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                              mvfxLayoutId layout, const mvfxView* view, bool buffersValid,
                              const void* scratch, int64_t scratchBytes, bool plexus,
                              mvfxInstance** liveOut, const mvfxOutputLayout** formatOut)
{
    *liveOut = nullptr;
    mvfxInstance* live = nullptr;
    mvfxResult found = mvfxFindEmitter(context, instance, emitter, &live);
    if (found != mvfx_success)
    {
        return found;
    }
    *formatOut = mvfxContextLayout(context, layout);
    if (*formatOut == nullptr)
    {
        return mvfx_errorStale;
    }
    if (!mvfxIsViewValid(view) || !buffersValid || !mvfxIsBufferValid(scratch, scratchBytes, 4))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    if (plexus)
    {
        *liveOut = live->effect->emitters[emitter].plexus.enabled ? live : nullptr;
        return mvfx_success;
    }
    if (live->effect->emitters[emitter].ribbonIds == mvfx_ribbonNone)
    {
        return mvfx_success;
    }
    if (scratchBytes < (int64_t)live->emitters[emitter].count * MVFX_SORT_BYTES_PER_ITEM)
    {
        return mvfx_errorCapacity;
    }
    *liveOut = live;
    return mvfx_success;
}

// The ribbons or plexus links of an emitter as points with a list.
static mvfxResult WritePoints(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                              mvfxLayoutId layout, const mvfxView* view,
                              const mvfxRibbonBuffers* buffers, int64_t* pointCountOut,
                              int64_t* ribbonCountOut, bool plexus)
{
    if (context == nullptr || view == nullptr || buffers == nullptr || pointCountOut == nullptr ||
        ribbonCountOut == nullptr)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *pointCountOut = 0;
    *ribbonCountOut = 0;
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
    }
    bool valid = buffers->pointCapacity >= 0 &&
                 (buffers->pointCapacity == 0 || buffers->points != nullptr) &&
                 mvfxIsBufferValid(buffers->ribbons, buffers->ribbonCapacity, alignof(mvfxRibbon));
    mvfxInstance* live = nullptr;
    const mvfxOutputLayout* format = nullptr;
    mvfxResult opened =
        OpenRibbons(context, instance, emitter, layout, view, valid, buffers->scratch,
                    buffers->scratchBytes, plexus, &live, &format);
    if (opened != mvfx_success || live == nullptr)
    {
        return opened;
    }
    const Sink sink = {.layout = format,
                       .records = buffers->points,
                       .recordCapacity = buffers->pointCapacity,
                       .ribbons = buffers->ribbons,
                       .ribbonCapacity = buffers->ribbonCapacity,
                       .scratch = buffers->scratch};
    Totals totals =
        plexus ? WriteLinks(live, emitter, view, &sink) : WriteChains(live, emitter, view, &sink);
    *pointCountOut = totals.records;
    *ribbonCountOut = totals.ribbons;
    return mvfx_success;
}

// The ribbons or plexus links of an emitter as strips, or as tubes of
// tube's sides (0 for strips).
static mvfxResult WriteStrips(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                              mvfxLayoutId layout, const mvfxView* view,
                              const mvfxExpandBuffers* buffers, int64_t* vertexCountOut,
                              int64_t* indexCountOut, bool plexus, const mvfxTubeDef* tube)
{
    bool tubed = tube == nullptr || tube->sides == 0 ||
                 (tube->sides >= 3 && tube->sides <= MVFX_MAX_TUBE_SIDES);
    if (context == nullptr || view == nullptr || buffers == nullptr || vertexCountOut == nullptr ||
        indexCountOut == nullptr || !tubed)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    *vertexCountOut = 0;
    *indexCountOut = 0;
    if (mvfxRefusesHostCall(context))
    {
        return mvfx_errorState;
    }
    bool valid = buffers->vertexCapacity >= 0 &&
                 (buffers->vertexCapacity == 0 || buffers->vertices != nullptr) &&
                 mvfxIsBufferValid(buffers->indices, buffers->indexCapacity, alignof(uint32_t));
    mvfxInstance* live = nullptr;
    const mvfxOutputLayout* format = nullptr;
    mvfxResult opened =
        OpenRibbons(context, instance, emitter, layout, view, valid, buffers->scratch,
                    buffers->scratchBytes, plexus, &live, &format);
    if (opened != mvfx_success || live == nullptr)
    {
        return opened;
    }
    const Sink sink = {.layout = format,
                       .strips = true,
                       .sides = tube != nullptr ? tube->sides : 0,
                       .caps = tube != nullptr && tube->caps,
                       .records = buffers->vertices,
                       .recordCapacity = buffers->vertexCapacity,
                       .indices = buffers->indices,
                       .indexCapacity = buffers->indexCapacity,
                       .scratch = buffers->scratch};
    Totals totals =
        plexus ? WriteLinks(live, emitter, view, &sink) : WriteChains(live, emitter, view, &sink);
    *vertexCountOut = totals.records;
    *indexCountOut = totals.indices;
    return mvfx_success;
}

mvfxResult mvfxInstance_WriteRibbons(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                     mvfxLayoutId layout, const mvfxView* view,
                                     const mvfxRibbonBuffers* buffers, int64_t* pointCountOut,
                                     int64_t* ribbonCountOut)
{
    return WritePoints(context, instance, emitter, layout, view, buffers, pointCountOut,
                       ribbonCountOut, false);
}

mvfxResult mvfxInstance_ExpandRibbons(mvfxContext* context, mvfxInstanceId instance,
                                      int32_t emitter, mvfxLayoutId layout, const mvfxView* view,
                                      const mvfxExpandBuffers* buffers, int64_t* vertexCountOut,
                                      int64_t* indexCountOut)
{
    return WriteStrips(context, instance, emitter, layout, view, buffers, vertexCountOut,
                       indexCountOut, false, nullptr);
}

mvfxResult mvfxInstance_ExpandTubes(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                    mvfxLayoutId layout, const mvfxView* view,
                                    const mvfxTubeDef* tube, const mvfxExpandBuffers* buffers,
                                    int64_t* vertexCountOut, int64_t* indexCountOut)
{
    if (tube == nullptr || tube->sides == 0)
    {
        if (context != nullptr)
        {
            mvfxCountMisuse(context);
        }
        return mvfx_errorInvalid;
    }
    return WriteStrips(context, instance, emitter, layout, view, buffers, vertexCountOut,
                       indexCountOut, false, tube);
}

mvfxResult mvfxInstance_WritePlexus(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                    mvfxLayoutId layout, const mvfxView* view,
                                    const mvfxRibbonBuffers* buffers, int64_t* pointCountOut,
                                    int64_t* ribbonCountOut)
{
    return WritePoints(context, instance, emitter, layout, view, buffers, pointCountOut,
                       ribbonCountOut, true);
}

mvfxResult mvfxInstance_ExpandPlexus(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                     mvfxLayoutId layout, const mvfxView* view,
                                     const mvfxExpandBuffers* buffers, int64_t* vertexCountOut,
                                     int64_t* indexCountOut)
{
    return WriteStrips(context, instance, emitter, layout, view, buffers, vertexCountOut,
                       indexCountOut, true, nullptr);
}
