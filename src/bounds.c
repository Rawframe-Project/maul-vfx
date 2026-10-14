// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "bounds.h"

#include "appearance.h"
#include "direction.h"
#include "ribbon.h"
#include "space.h"

#include <math.h>

void mvfxMeasureBounds(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    state->hasBounds = !compiled->fixedBounds && state->count > 0;
    if (!state->hasBounds)
    {
        return;
    }
    const float* p[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        state->boundsMin[k] = INFINITY;
        state->boundsMax[k] = -INFINITY;
    }
    float largest = 0.0f;
    for (int32_t i = 0; i < state->count; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            state->boundsMin[k] = fminf(state->boundsMin[k], p[k][i]);
            state->boundsMax[k] = fmaxf(state->boundsMax[k], p[k][i]);
        }
        float size[3];
        mvfxSizeOf(instance, emitter, i, size);
        largest = fmaxf(largest, fmaxf(fabsf(size[0]), fmaxf(fabsf(size[1]), fabsf(size[2]))));
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        state->boundsMin[k] -= largest;
        state->boundsMax[k] += largest;
    }
}

// The world's box around a box's eight corners, each mapped by a point
// map into the world.
typedef void CornerMap(const void* how, const float corner[3], double out[3]);

static mvfxBounds AroundCorners(const float low[3], const float high[3], CornerMap* map,
                                const void* how)
{
    const double far = (double)INFINITY;
    mvfxBounds out = {.min = {far, far, far}, .max = {-far, -far, -far}};
    for (int32_t c = 0; c < 8; ++c)
    {
        const float corner[3] = {(c & 1) != 0 ? high[0] : low[0], (c & 2) != 0 ? high[1] : low[1],
                                 (c & 4) != 0 ? high[2] : low[2]};
        double world[3];
        map(how, corner, world);
        for (int32_t k = 0; k < 3; ++k)
        {
            out.min[k] = fmin(out.min[k], world[k]);
            out.max[k] = fmax(out.max[k], world[k]);
        }
    }
    return out;
}

static void ThroughMap(const void* how, const float corner[3], double out[3])
{
    mvfxUnmapPoint(how, corner, out);
}

// A corner in the instance's space into the world: scaled, turned, moved.
static void ThroughTransform(const void* how, const float corner[3], double out[3])
{
    const mvfxTransform* t = how;
    const double scaled[3] = {(double)corner[0] * (double)t->scale,
                              (double)corner[1] * (double)t->scale,
                              (double)corner[2] * (double)t->scale};
    double turned[3];
    mvfxRotateDouble(t->rotation, scaled, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = t->origin[k] + turned[k];
    }
}

mvfxBounds mvfxBoundsOf(const mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxEmitterState* state = &instance->emitters[emitter];
    if (compiled->fixedBounds)
    {
        return AroundCorners(compiled->boundsMin, compiled->boundsMax, ThroughTransform,
                             &instance->to);
    }
    mvfxBounds box = {.empty = true};
    if (state->hasBounds)
    {
        mvfxSpaceMap map = mvfxSpaceMapAt(instance, emitter, mvfx_spaceWorld, &instance->to);
        box = AroundCorners(state->boundsMin, state->boundsMax, ThroughMap, &map);
    }
    // A ribbon's pinned head joins its particles' box.
    double head[3];
    if (box.empty || !mvfxRibbonHeadPlace(instance, emitter, head))
    {
        return box;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        box.min[k] = fmin(box.min[k], head[k]);
        box.max[k] = fmax(box.max[k], head[k]);
    }
    return box;
}
