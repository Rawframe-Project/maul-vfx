// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas of an effect as compiled, for a simulation elsewhere.

#include "effect.h"
#include "effect_api.h"
#include "gas.h"

#include "maul-vfx/plan.h"

mvfxResult mvfxEffect_GetGasView(const mvfxContext* context, mvfxEffectId effect, int32_t gas,
                                 mvfxGasView* viewOut)
{
    if (viewOut != nullptr)
    {
        *viewOut = (mvfxGasView){0};
    }
    if (context == nullptr || viewOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxEffect* live = mvfxContextEffect(context, effect);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (gas < 0 || gas >= live->gasCount)
    {
        return mvfx_errorInvalid;
    }
    const mvfxCompiledGas* g = &live->gases[gas];
    *viewOut = (mvfxGasView){
        .def = g->def,
        .cells = g->cells,
        .levelCount = g->levelCount,
        .emitter = g->emitter,
    };
    viewOut->def.sources = g->def.sourceCount > 0 ? g->sources : nullptr;
    viewOut->def.forces = g->def.forceCount > 0 ? g->forces : nullptr;
    viewOut->def.obstacles = g->def.obstacleCount > 0 ? g->obstacles : nullptr;
    for (int32_t k = 0; k < 3; ++k)
    {
        viewOut->faces[k] = g->faces[k];
    }
    for (int32_t l = 0; l < g->levelCount; ++l)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            viewOut->levels[l][k] = g->levels[l][k];
        }
    }
    return mvfx_success;
}
