// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "box.h"

#include "direction.h"

#include <math.h>

mvfxBoxPlace mvfxPlaceBox(const mvfxInstance* instance, bool local, const float center[3],
                          const double origin[3], const float rotation[4], float scale)
{
    // A local box is where the instance is now.
    const double* at = local ? instance->to.origin : origin;
    const float* turn = local ? instance->to.rotation : rotation;
    float s = local ? instance->to.scale : scale;
    const float offset[3] = {s * center[0], s * center[1], s * center[2]};
    float turned[3];
    mvfxRotate(turn, offset, turned);
    mvfxBoxPlace place = {.scale = s};
    for (int32_t k = 0; k < 3; ++k)
    {
        place.center[k] = at[k] + (double)turned[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        place.rotation[k] = turn[k];
    }
    return place;
}

mvfxSpaceMap mvfxBoxMap(const mvfxInstance* instance, const mvfxBoxPlace* place,
                        mvfxSimulationSpace space)
{
    const float* rotation = place->rotation;
    const float back[4] = {-rotation[0], -rotation[1], -rotation[2], rotation[3]};
    mvfxSpaceMap map = {.sigma = 1.0f / place->scale};
    if (space == mvfx_spaceWorld)
    {
        // Moved to the box's center, turned back, over its scale.
        for (int32_t k = 0; k < 4; ++k)
        {
            map.rotation[k] = back[k];
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            map.before[k] = place->center[k];
        }
        return map;
    }
    // From the instance's frame: scaled and turned into the world, then
    // turned back into the box's, the instance's origin's offset from the
    // box's center added in binary64.
    const mvfxTransform* now = &instance->to;
    mvfxMultiplyRotations(back, now->rotation, map.rotation);
    map.sigma = now->scale / place->scale;
    double offset[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        offset[k] = (now->origin[k] - place->center[k]) / (double)place->scale;
    }
    mvfxRotateDouble(back, offset, map.after);
    return map;
}

mvfxSpaceMap mvfxBoxToSpace(const mvfxBoxPlace* place, const mvfxSpaceMap* fromWorld)
{
    // A box point x is the world's C + R s x, which fromWorld takes to
    // turn(q, sigma (C + R s x - before)) + after: turn(q R, sigma s (x -
    // before')) + after, before' = -R^-1 (C - before) / s.
    mvfxSpaceMap map = {.sigma = fromWorld->sigma * place->scale};
    mvfxMultiplyRotations(fromWorld->rotation, place->rotation, map.rotation);
    const float* rotation = place->rotation;
    const float back[4] = {-rotation[0], -rotation[1], -rotation[2], rotation[3]};
    double d[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = (place->center[k] - fromWorld->before[k]) / (double)place->scale;
        map.after[k] = fromWorld->after[k];
    }
    double turned[3];
    mvfxRotateDouble(back, d, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        map.before[k] = -turned[k];
    }
    return map;
}

mvfxBounds mvfxBoxBounds(const mvfxBoxPlace* place, const float half[3])
{
    mvfxBounds out = {.empty = false};
    // Each world axis's reach of the box's turned half sizes.
    float reach[3] = {0.0f, 0.0f, 0.0f};
    for (int32_t a = 0; a < 3; ++a)
    {
        float axis[3] = {0.0f, 0.0f, 0.0f};
        axis[a] = half[a];
        float turned[3];
        mvfxRotate(place->rotation, axis, turned);
        for (int32_t k = 0; k < 3; ++k)
        {
            reach[k] += fabsf(turned[k]);
        }
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out.min[k] = place->center[k] - (double)reach[k];
        out.max[k] = place->center[k] + (double)reach[k];
    }
    return out;
}
