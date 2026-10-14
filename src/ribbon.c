// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "ribbon.h"

#include "context.h"
#include "gradient.h"
#include "instance_api.h"
#include "layout_api.h"
#include "output.h"
#include "sort.h"

#include <math.h>

// A chain being written: the source of its records, the particles of its
// ribbon in order, the view, its length in the world and its points.
typedef struct Chain
{
    const mvfxRecordSource* source;
    const mvfxCompiledEmitter* compiled;
    const mvfxView* view;
    const uint32_t* particles;
    int32_t count;
    double length;
    int64_t points;
} Chain;

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
        mvfxWorldPlaceOf(chain->source, (int32_t)chain->particles[j], out);
        return;
    }
    int32_t end = j < 0 ? 0 : chain->count - 1;
    int32_t next = j < 0 ? 1 : chain->count - 2;
    double a[3];
    double b[3];
    mvfxWorldPlaceOf(chain->source, (int32_t)chain->particles[end], a);
    mvfxWorldPlaceOf(chain->source, (int32_t)chain->particles[next], b);
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
    int32_t most = compiled->ribbonPieces;
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
    double before[3];
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
    if (chain->compiled->ribbonFacing == mvfx_ribbonFaceNormal)
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

// The texture coordinate along at a place a fraction t of the length.
static float AlongAt(const Chain* chain, const Walk* walk, float t)
{
    const mvfxCompiledEmitter* compiled = chain->compiled;
    const mvfxInstance* instance = chain->source->instance;
    float along = t;
    if (compiled->ribbonTexture == mvfx_ribbonTilePerSegment)
    {
        along = (float)walk->s + (float)FractionOf(walk);
    }
    else if (compiled->ribbonTexture == mvfx_ribbonTileByDistance)
    {
        const float* distances = mvfxAttributeArray((mvfxInstance*)instance, chain->source->emitter,
                                                    mvfx_attributeRibbonDistance);
        float first = distances[chain->particles[walk->s]];
        float second = walk->q == 0 ? first : distances[chain->particles[walk->s + 1]];
        along = (first + (second - first) * (float)FractionOf(walk)) / compiled->ribbonTileLength;
    }
    double seconds = (double)instance->clock / (double)MVFX_TICKS_PER_SECOND;
    return along + (float)((double)compiled->ribbonScroll * seconds);
}

// Writes a chain's points from its first record on.
static void WriteChain(const Chain* chain, const mvfxOutputLayout* layout, uint32_t ribbon,
                       unsigned char* records)
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
    for (int64_t g = 0; g < chain->points; ++g)
    {
        walked += Distance(before, here);
        // Along the length, or by points where the chain has none.
        float t = chain->length > 0.0 ? (float)(walked / chain->length)
                                      : (float)((double)g / (double)(chain->points - 1));
        mvfxRibbonPoint point = {.length = t,
                                 .along = AlongAt(chain, &walk, t),
                                 .width = mvfxLifeAt(&compiled->ribbonWidth, t),
                                 .color = {1.0f, 1.0f, 1.0f, 1.0f},
                                 .ribbon = ribbon};
        FrameAt(chain, before, here, after, &point);
        if (compiled->ribbonColor != nullptr)
        {
            mvfxGradientTableAt(compiled->ribbonColor, t, point.color);
        }
        int32_t first = (int32_t)chain->particles[walk.s];
        int32_t second = walk.q == 0 ? first : (int32_t)chain->particles[walk.s + 1];
        mvfxWriteRibbonRecord(chain->source, layout, first, second, (float)FractionOf(&walk), here,
                              &point, records + g * (int64_t)layout->stride);
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

// What the chains of an emitter wrote and count.
typedef struct Totals
{
    int64_t points;
    int64_t ribbons;
    int64_t pointsWritten;
    int64_t ribbonsWritten;
    bool full;
} Totals;

// Counts a chain and writes it where it fits whole after those written.
static void TakeChain(const Chain* chain, uint32_t id, const mvfxOutputLayout* layout,
                      const mvfxRibbonBuffers* buffers, Totals* totals)
{
    bool fits = !totals->full && totals->ribbonsWritten < buffers->ribbonCapacity &&
                totals->pointsWritten + chain->points <= buffers->pointCapacity;
    if (fits)
    {
        WriteChain(chain, layout, (uint32_t)totals->ribbonsWritten,
                   (unsigned char*)buffers->points + totals->pointsWritten * layout->stride);
        buffers->ribbons[totals->ribbonsWritten] =
            (mvfxRibbon){.first = totals->pointsWritten, .count = (int32_t)chain->points, .id = id};
        totals->pointsWritten += chain->points;
        totals->ribbonsWritten += 1;
    }
    totals->full = !fits;
    totals->points += chain->points;
    totals->ribbons += 1;
}

// Chains an emitter's live particles by ribbon id and takes each chain of
// two or more.
static Totals WriteChains(const mvfxInstance* instance, int32_t emitter,
                          const mvfxOutputLayout* layout, const mvfxView* view,
                          const mvfxRibbonBuffers* buffers)
{
    int32_t live = instance->emitters[emitter].count;
    uint32_t* keys = buffers->scratch;
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
        if (end - start >= 2)
        {
            Chain chain = {.source = &source,
                           .compiled = &instance->effect->emitters[emitter],
                           .view = view,
                           .particles = indices + start,
                           .count = end - start};
            MeasureChain(&chain);
            TakeChain(&chain, keys[start], layout, buffers, &totals);
        }
        start = end;
    }
    return totals;
}

mvfxResult mvfxInstance_WriteRibbons(mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                                     mvfxLayoutId layout, const mvfxView* view,
                                     const mvfxRibbonBuffers* buffers, int64_t* pointCountOut,
                                     int64_t* ribbonCountOut)
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
    mvfxInstance* live = nullptr;
    mvfxResult found = mvfxFindEmitter(context, instance, emitter, &live);
    if (found != mvfx_success)
    {
        return found;
    }
    const mvfxOutputLayout* format = mvfxContextLayout(context, layout);
    if (format == nullptr)
    {
        return mvfx_errorStale;
    }
    if (!mvfxIsViewValid(view) || buffers->pointCapacity < 0 ||
        (buffers->pointCapacity > 0 && buffers->points == nullptr) ||
        !mvfxIsBufferValid(buffers->ribbons, buffers->ribbonCapacity, alignof(mvfxRibbon)) ||
        !mvfxIsBufferValid(buffers->scratch, buffers->scratchBytes, 4))
    {
        mvfxCountMisuse(context);
        return mvfx_errorInvalid;
    }
    if (live->effect->emitters[emitter].ribbonIds == mvfx_ribbonNone)
    {
        return mvfx_success;
    }
    if (buffers->scratchBytes < (int64_t)live->emitters[emitter].count * MVFX_SORT_BYTES_PER_ITEM)
    {
        return mvfx_errorCapacity;
    }
    Totals totals = WriteChains(live, emitter, format, view, buffers);
    *pointCountOut = totals.points;
    *ribbonCountOut = totals.ribbons;
    return mvfx_success;
}
