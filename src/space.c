// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "space.h"

mvfxSpaceMap mvfxSpaceMapAt(const mvfxInstance* instance, int32_t emitter,
                            mvfxSimulationSpace space, const mvfxTransform* transform)
{
    const double* anchor = instance->emitters[emitter].anchor;
    bool world = instance->effect->emitters[emitter].space == mvfx_spaceWorld;
    mvfxSpaceMap map = {.rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .sigma = 1.0f};
    const float* q = transform->rotation;
    if (world && space == mvfx_spaceWorld)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            map.after[k] = -anchor[k];
        }
    }
    else if (world)
    {
        // From the instance's space: scaled, turned, moved to its origin.
        for (int32_t k = 0; k < 4; ++k)
        {
            map.rotation[k] = q[k];
        }
        map.sigma = transform->scale;
        for (int32_t k = 0; k < 3; ++k)
        {
            map.after[k] = transform->origin[k] - anchor[k];
        }
    }
    else if (space == mvfx_spaceWorld)
    {
        // From the world into the instance's space: moved, turned back,
        // unscaled.
        map.rotation[0] = -q[0];
        map.rotation[1] = -q[1];
        map.rotation[2] = -q[2];
        map.rotation[3] = q[3];
        map.sigma = 1.0f / transform->scale;
        for (int32_t k = 0; k < 3; ++k)
        {
            map.before[k] = transform->origin[k];
        }
    }
    return map;
}
