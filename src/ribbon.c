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
// ribbon in order, and its length in the world.
typedef struct Chain
{
    const mvfxRecordSource* source;
    const uint32_t* particles;
    int32_t count;
    double length;
} Chain;

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

static double ChainLength(const Chain* chain)
{
    double length = 0.0;
    double before[3];
    mvfxWorldPlaceOf(chain->source, (int32_t)chain->particles[0], before);
    for (int32_t j = 1; j < chain->count; ++j)
    {
        double here[3];
        mvfxWorldPlaceOf(chain->source, (int32_t)chain->particles[j], here);
        length += Distance(before, here);
        for (int32_t k = 0; k < 3; ++k)
        {
            before[k] = here[k];
        }
    }
    return length;
}

// The unit tangent at point j: toward the next point from the one before
// (one-sided at the ends); 0 where they meet.
static void TangentAt(const Chain* chain, int32_t j, float out[3])
{
    double from[3];
    double to[3];
    mvfxWorldPlaceOf(chain->source, (int32_t)chain->particles[j > 0 ? j - 1 : 0], from);
    mvfxWorldPlaceOf(chain->source, (int32_t)chain->particles[j + 1 < chain->count ? j + 1 : j],
                     to);
    double span = Distance(from, to);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = span > 0.0 ? (float)((to[k] - from[k]) / span) : 0.0f;
    }
}

// Writes a chain's points from its first record on.
static void WriteChain(const Chain* chain, const mvfxOutputLayout* layout, uint32_t ribbon,
                       unsigned char* records)
{
    const mvfxCompiledEmitter* compiled =
        &chain->source->instance->effect->emitters[chain->source->emitter];
    double walked = 0.0;
    double before[3];
    mvfxWorldPlaceOf(chain->source, (int32_t)chain->particles[0], before);
    for (int32_t j = 0; j < chain->count; ++j)
    {
        double here[3];
        mvfxWorldPlaceOf(chain->source, (int32_t)chain->particles[j], here);
        walked += Distance(before, here);
        // Along the length, or by points where the chain has none.
        float t = chain->length > 0.0 ? (float)(walked / chain->length)
                                      : (float)j / (float)(chain->count - 1);
        mvfxRibbonPoint point = {.length = t,
                                 .along = t,
                                 .width = mvfxLifeAt(&compiled->ribbonWidth, t),
                                 .color = {1.0f, 1.0f, 1.0f, 1.0f},
                                 .ribbon = ribbon};
        TangentAt(chain, j, point.tangent);
        if (compiled->ribbonColor != nullptr)
        {
            mvfxGradientTableAt(compiled->ribbonColor, t, point.color);
        }
        mvfxWriteRecord(chain->source, layout, (int32_t)chain->particles[j], &point,
                        records + (int64_t)j * (int64_t)layout->stride);
        for (int32_t k = 0; k < 3; ++k)
        {
            before[k] = here[k];
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
                totals->pointsWritten + chain->count <= buffers->pointCapacity;
    if (fits)
    {
        WriteChain(chain, layout, (uint32_t)totals->ribbonsWritten,
                   (unsigned char*)buffers->points + totals->pointsWritten * layout->stride);
        buffers->ribbons[totals->ribbonsWritten] =
            (mvfxRibbon){.first = totals->pointsWritten, .count = chain->count, .id = id};
        totals->pointsWritten += chain->count;
        totals->ribbonsWritten += 1;
    }
    totals->full = !fits;
    totals->points += chain->count;
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
            Chain chain = {.source = &source, .particles = indices + start, .count = end - start};
            chain.length = ChainLength(&chain);
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
