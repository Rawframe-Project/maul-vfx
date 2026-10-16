// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An effect def's gases checked against its emitters: the gases followed
// and splatted into, the gases' forces, and each liquid gas's liquid.

#include "effect_gas.h"

#include "force.h"

// Whether an emitter's splat into a gas is in range for an effect of
// gasCount gases.
static bool IsSplatValid(const mvfxGasSplat* splat, int32_t gasCount)
{
    if (!splat->enabled)
    {
        return true;
    }
    const float rates[3] = {splat->density, splat->temperature, splat->fuel};
    bool valid = splat->gas >= 0 && splat->gas < gasCount && splat->drag >= 0.0f &&
                 splat->drag <= MVFX_MAX_DRAG;
    for (int32_t k = 0; valid && k < 3; ++k)
    {
        valid = rates[k] >= -MVFX_MAX_EXTENT && rates[k] <= MVFX_MAX_EXTENT;
    }
    return valid;
}

int32_t mvfxLiquidOf(const mvfxEffectDef* def, int32_t g)
{
    int32_t found = -1;
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        const mvfxLiquidDef* liquid = &def->emitters[i].liquid;
        if (liquid->enabled && liquid->solver == mvfx_liquidGrid && liquid->gas == g)
        {
            found = found == -1 ? i : -2;
        }
    }
    return found;
}

// Whether every grid liquid names a liquid gas of the effect, each liquid
// gas is named by exactly one, no emitter splats into a liquid or surface
// gas, and each surface gas names an emitter of the effect.
static bool AreLiquidGasesValid(const mvfxEffectDef* def)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        const mvfxEmitterDef* emitter = &def->emitters[i];
        const mvfxLiquidDef* liquid = &emitter->liquid;
        bool grid = liquid->enabled && liquid->solver == mvfx_liquidGrid;
        if (grid && (liquid->gas < 0 || liquid->gas >= def->gasCount ||
                     def->gases[liquid->gas].kind != mvfx_gasLiquid))
        {
            return false;
        }
        if (emitter->gasSplat.enabled && def->gases[emitter->gasSplat.gas].kind != mvfx_gasSmoke)
        {
            return false;
        }
    }
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        const mvfxGasDef* gas = &def->gases[g];
        if ((gas->kind == mvfx_gasLiquid && mvfxLiquidOf(def, g) < 0) ||
            (gas->kind == mvfx_gasSurface &&
             (gas->surface.emitter < 0 || gas->surface.emitter >= def->emitterCount)))
        {
            return false;
        }
    }
    return true;
}

// Whether every gas an emitter's forces follow or it splats into is one
// of the effect's and none a surface, and no gas's force follows a gas.
static bool AreGasLinksValid(const mvfxEffectDef* def)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        for (int32_t f = 0; f < def->emitters[i].forceCount; ++f)
        {
            const mvfxForce* force = &def->emitters[i].forces[f];
            if (force->kind == mvfx_forceGas &&
                (force->gas >= def->gasCount || def->gases[force->gas].kind == mvfx_gasSurface))
            {
                return false;
            }
        }
    }
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        for (int32_t f = 0; f < def->gases[g].forceCount; ++f)
        {
            if (def->gases[g].forces[f].kind == mvfx_forceGas)
            {
                return false;
            }
        }
    }
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        if (!IsSplatValid(&def->emitters[i].gasSplat, def->gasCount))
        {
            return false;
        }
    }
    return AreLiquidGasesValid(def);
}

// Whether a gas's forces are in range for the effect's parameters.
static bool AreGasForcesValid(const mvfxGasDef* gas, const mvfxEffectDef* def)
{
    if (gas->forceCount < 0 || gas->forceCount > MVFX_MAX_FORCES ||
        (gas->forceCount > 0 && gas->forces == nullptr))
    {
        return false;
    }
    int32_t parameters = def->parameterCount >= 0 && def->parameterCount <= MVFX_MAX_PARAMETERS
                             ? def->parameterCount
                             : 0;
    for (int32_t f = 0; f < gas->forceCount; ++f)
    {
        if (!mvfxIsForceValid(&gas->forces[f], parameters))
        {
            return false;
        }
    }
    return true;
}

bool mvfxAreEffectGasesValid(const mvfxEffectDef* def)
{
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        if (!AreGasForcesValid(&def->gases[g], def))
        {
            return false;
        }
    }
    return AreGasLinksValid(def);
}
