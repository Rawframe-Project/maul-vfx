// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "record.h"

#include <math.h>
#include <string.h>

void mvfxRecordMap(const mvfxEmitterView* view, const mvfxEmitterPlan* plan,
                   uint32_t words[MVFX_RECORD_MAP_WORDS])
{
    uint32_t* at = words - MVFX_RECORD_SPACE;
    at[MVFX_RECORD_SPACE] = (uint32_t)view->space;
    memcpy(&at[MVFX_RECORD_SCALE], &plan->to.scale, sizeof(float));
    memcpy(&at[MVFX_RECORD_ANCHOR], plan->anchorAfter, 3 * sizeof(double));
    memcpy(&at[MVFX_RECORD_ORIGIN], plan->to.origin, 3 * sizeof(double));
    memcpy(&at[MVFX_RECORD_ROTATION], plan->to.rotation, 4 * sizeof(float));
}

// The instance's place at the record's step: origin, rotation, scale.
typedef struct Place
{
    double origin[3];
    double rotation[4];
    double scale;
} Place;

static Place PlaceOf(const uint32_t* record)
{
    Place place;
    float rotation[4];
    float scale;
    memcpy(place.origin, &record[MVFX_RECORD_ORIGIN], sizeof place.origin);
    memcpy(rotation, &record[MVFX_RECORD_ROTATION], sizeof rotation);
    memcpy(&scale, &record[MVFX_RECORD_SCALE], sizeof scale);
    for (int32_t k = 0; k < 4; ++k)
    {
        place.rotation[k] = (double)rotation[k];
    }
    place.scale = (double)scale;
    return place;
}

// A point of the instance's space in the world: scaled, turned (v + 2w
// (q x v) + 2 q x (q x v)), moved.
static void ThroughPlace(const Place* place, const float corner[3], double out[3])
{
    const double* q = place->rotation;
    double v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        v[k] = (double)corner[k] * place->scale;
    }
    const double t[3] = {2.0 * (q[1] * v[2] - q[2] * v[1]), 2.0 * (q[2] * v[0] - q[0] * v[2]),
                         2.0 * (q[0] * v[1] - q[1] * v[0])};
    const double u[3] = {q[1] * t[2] - q[2] * t[1], q[2] * t[0] - q[0] * t[2],
                         q[0] * t[1] - q[1] * t[0]};
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = place->origin[k] + v[k] + q[3] * t[k] + u[k];
    }
}

// The world's box around a box's eight corners through the instance's
// place.
static mvfxBounds AroundCorners(const Place* place, const float low[3], const float high[3])
{
    const double far = (double)INFINITY;
    mvfxBounds out = {.min = {far, far, far}, .max = {-far, -far, -far}};
    for (int32_t c = 0; c < 8; ++c)
    {
        const float corner[3] = {(c & 1) != 0 ? high[0] : low[0], (c & 2) != 0 ? high[1] : low[1],
                                 (c & 4) != 0 ? high[2] : low[2]};
        double world[3];
        ThroughPlace(place, corner, world);
        for (int32_t k = 0; k < 3; ++k)
        {
            out.min[k] = fmin(out.min[k], world[k]);
            out.max[k] = fmax(out.max[k], world[k]);
        }
    }
    return out;
}

mvfxBounds mvfxRecordBounds(const mvfxEmitterView* view, const uint32_t* record)
{
    Place place = PlaceOf(record);
    if (view->fixedBounds)
    {
        return AroundCorners(&place, view->boundsMin, view->boundsMax);
    }
    if (record[MVFX_RECORD_COUNT] == 0)
    {
        return (mvfxBounds){.empty = true};
    }
    float growth;
    float low[3];
    float high[3];
    memcpy(&growth, &record[MVFX_RECORD_GROWTH], sizeof growth);
    memcpy(low, &record[MVFX_RECORD_MIN], sizeof low);
    memcpy(high, &record[MVFX_RECORD_MAX], sizeof high);
    for (int32_t k = 0; k < 3; ++k)
    {
        low[k] -= growth;
        high[k] += growth;
    }
    if (record[MVFX_RECORD_SPACE] != (uint32_t)mvfx_spaceWorld)
    {
        return AroundCorners(&place, low, high);
    }
    // In the world, offsets from the anchor.
    double anchor[3];
    memcpy(anchor, &record[MVFX_RECORD_ANCHOR], sizeof anchor);
    mvfxBounds out = {.empty = false};
    for (int32_t k = 0; k < 3; ++k)
    {
        out.min[k] = anchor[k] + (double)low[k];
        out.max[k] = anchor[k] + (double)high[k];
    }
    return out;
}
